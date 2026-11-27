// SPDX-License-Identifier: GPL-3.0-or-later
// Entry point for Camillia on the CardputerZero: the firmware's own setup()
// and loop() (src/main_lvgl.cpp) as a launcher app. SIGINT/SIGTERM -- the
// launcher's Home long-press, or systemd -- persist state and leave. Started
// from the desktop (a Wayland or X display), it runs in a window instead of on
// the LCD; see czero_window.h.
#include <Arduino.h>

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/file.h>
#include <unistd.h>

#include <atomic>

#include "czero_app.h"
#include "czero_paths.h"
#include "czero_system.h"
#include "czero_window.h"

void setup();
void loop();
void linuxAppFlushBeforeExit();

static char **s_argv = nullptr;
static std::atomic<int> s_signal{0};

void czeroSetArgv(int argc, char **argv) {
    (void)argc;
    s_argv = argv;
}

static void onSignal(int sig) {
    // A second signal while the first is being handled means "now".
    if (s_signal.exchange(sig) != 0) _exit(128 + sig);
}

void czeroExit(int code) {
    linuxAppFlushBeforeExit();
    // The launcher comes back to whatever the backlight was left at, which
    // may be off (asleep) or the lock screen's dim level.
    czeroSystemRestoreBacklight();
    fflush(stdout);
    fflush(stderr);
    // Not exit(): worker threads are still running, and static destructors
    // would pull objects out from under them.
    _exit(code);
}

void czeroRestart() {
    fflush(stdout);
    fflush(stderr);
    // The new image must be able to claim the radio's GPIO lines and spidev
    // again, so nothing may be inherited.
    if (DIR *d = opendir("/proc/self/fd")) {
        const int keep = dirfd(d);
        while (struct dirent *e = readdir(d)) {
            const int fd = atoi(e->d_name);
            if (fd > 2 && fd != keep) close(fd);
        }
        closedir(d);
    }
    if (s_argv) execv("/proc/self/exe", s_argv);
    perror("[app] restart: execv");
    _exit(1);
}

// One Camillia at a time: the launcher's and the desktop's would share one
// radio and one set of settings. The lock lasts as long as the process.
static bool lockInstance() {
    const int fd = open((czeroDataDir() + "/.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    // A lock file that can't be made: run unlocked rather than not at all.
    if (fd < 0) return true;
    if (flock(fd, LOCK_EX | LOCK_NB) == 0) return true;
    close(fd);
    return false;
}

int main(int argc, char **argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    czeroSetArgv(argc, argv);
    czeroMkdirs(czeroDataDir());

    struct sigaction sa = {};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);

    printf("[app] Camillia for CardputerZero, data in %s\n", czeroDataDir().c_str());
    if (!lockInstance()) {
        printf("[app] already running\n");
        return 0;
    }
    czeroInstallMenuEntry();
    if (czeroWindowed()) {
        if (!czeroWindowOpen()) return 1;
    } else if (access("/dev/fb_lcd", R_OK | W_OK) != 0 && access("/dev/fb0", R_OK | W_OK) != 0) {
        // LovyanGFX's Panel_fb falls back to scanning sysfs, and crashes, when
        // the framebuffer it was given cannot be opened.
        fprintf(stderr, "[app] no usable framebuffer (/dev/fb_lcd, /dev/fb0)\n");
        return 1;
    }
    setup();
    for (;;) {
        loop();
        if (const int sig = s_signal.load()) {
            printf("[app] signal %d, saving and exiting\n", sig);
            czeroExit(0);
        }
        if (czeroWindowClosed()) {
            printf("[app] window closed, saving and exiting\n");
            czeroExit(0);
        }
    }
}
