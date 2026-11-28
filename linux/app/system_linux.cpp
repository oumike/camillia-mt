// SPDX-License-Identifier: GPL-3.0-or-later
// The CardputerZero's system settings: see czero_system.h.
#include "czero_system.h"

#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <thread>
#include <vector>

#include "czero_window.h"

extern char **environ;

namespace {

// A top-level number from the launcher's settings file. The file is small,
// flat where these keys are, and written by one program, so a key search is
// enough; no JSON parser for three integers.
bool launcherSetting(const char *key, long &out) {
    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;
    const std::string path = std::string(home) + "/.config/cardputerzero/config.json";
    FILE *f = fopen(path.c_str(), "r");
    if (!f) return false;
    std::string text;
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);

    const std::string needle = std::string("\"") + key + "\"";
    const size_t at = text.find(needle);
    if (at == std::string::npos) return false;
    const char *p = text.c_str() + at + needle.size();
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p++ != ':') return false;
    char *end = nullptr;
    const long v = strtol(p, &end, 10);
    if (end == p) return false;
    out = v;
    return true;
}

// The first backlight class device, as the display driver uses
// (cardputer_zero_display.h).
bool backlightPaths(std::string &level, int &max) {
    DIR *d = opendir("/sys/class/backlight");
    if (!d) return false;
    bool found = false;
    while (struct dirent *e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string dir = std::string("/sys/class/backlight/") + e->d_name;
        if (FILE *f = fopen((dir + "/max_brightness").c_str(), "r")) {
            if (fscanf(f, "%d", &max) != 1) max = 0;
            fclose(f);
        }
        level = dir + "/brightness";
        found = max > 0;
        break;
    }
    closedir(d);
    return found;
}

bool writeWav(int fd, const int16_t *samples, size_t count, unsigned rate) {
    const uint32_t dataBytes = (uint32_t)(count * sizeof(int16_t));
    uint8_t h[44];
    auto put32 = [&](int at, uint32_t v) {
        for (int i = 0; i < 4; i++) h[at + i] = (uint8_t)(v >> (8 * i));
    };
    auto put16 = [&](int at, uint16_t v) {
        h[at] = (uint8_t)v;
        h[at + 1] = (uint8_t)(v >> 8);
    };
    memcpy(h, "RIFF", 4);
    put32(4, 36 + dataBytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);              // PCM
    put16(22, 1);              // mono
    put32(24, rate);
    put32(28, rate * 2);
    put16(32, 2);
    put16(34, 16);
    memcpy(h + 36, "data", 4);
    put32(40, dataBytes);
    // Little-endian samples, which is what the CardputerZero (arm64) holds.
    return write(fd, h, sizeof(h)) == (ssize_t)sizeof(h)
        && write(fd, samples, dataBytes) == (ssize_t)dataBytes;
}

// Runs one player to completion; true when it played.
bool runPlayer(const char *const argv[]) {
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, argv[0], &fa, nullptr, const_cast<char *const *>(argv), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return false;
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}  // namespace

int czeroSystemBrightnessPct() {
    // A window on the desktop is lit by the monitor; dimming it would only
    // grey out the app.
    if (czeroWindowed()) return 100;
    long v = 0;
    if (launcherSetting("brightness", v) && v > 0) return v > 100 ? 100 : (int)v;
    // No launcher setting: keep whatever the panel is at, unless that is dark
    // (a run that ended asleep), which would read as a dead screen.
    std::string level;
    int max = 0;
    if (backlightPaths(level, max)) {
        if (FILE *f = fopen(level.c_str(), "r")) {
            int raw = 0;
            const bool ok = fscanf(f, "%d", &raw) == 1;
            fclose(f);
            if (ok && raw > 0) {
                const int pct = (raw * 100 + max / 2) / max;
                return pct < 1 ? 1 : (pct > 100 ? 100 : pct);
            }
        }
    }
    return 60;
}

uint32_t czeroSystemScreenOffSecs() {
    // On the desktop, blanking is the desktop's screensaver's job.
    if (czeroWindowed()) return 0;
    long v = 0;
    // The launcher's "Never" is not a positive number of seconds.
    if (launcherSetting("dark_time", v)) return v > 0 ? (uint32_t)v : 0;
    return 300;
}

void czeroSystemRestoreBacklight() {
    if (czeroWindowed()) return;
    std::string level;
    int max = 0;
    if (!backlightPaths(level, max)) return;
    const int raw = (czeroSystemBrightnessPct() * max + 50) / 100;
    if (FILE *f = fopen(level.c_str(), "w")) {
        fprintf(f, "%d\n", raw < 1 ? 1 : raw);
        fclose(f);
    }
}

void czeroSystemPlayPcm(const int16_t *samples, size_t count, unsigned rate) {
    if (!samples || !count || !rate) return;
    const char *dir = getenv("XDG_RUNTIME_DIR");
    std::string path = std::string(dir && dir[0] ? dir : "/tmp") + "/camillia-sound-XXXXXX";
    std::vector<char> tmpl(path.begin(), path.end());
    tmpl.push_back('\0');
    const int fd = mkstemp(tmpl.data());
    if (fd < 0) return;
    path = tmpl.data();
    const bool ok = writeWav(fd, samples, count, rate);
    close(fd);
    if (!ok) {
        unlink(path.c_str());
        return;
    }
    // A thread per sound, so the caller never waits on the player. PipeWire
    // first, as the launcher plays through it; then the PulseAudio and plain
    // ALSA players, for an image without it.
    std::thread([path]() {
        const char *const players[][4] = {
            {"pw-play", path.c_str(), nullptr, nullptr},
            {"paplay", path.c_str(), nullptr, nullptr},
            {"aplay", "-q", path.c_str(), nullptr},
        };
        for (const auto &argv : players) {
            if (runPlayer(argv)) break;
        }
        unlink(path.c_str());
    }).detach();
}
