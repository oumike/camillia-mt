// SPDX-License-Identifier: GPL-3.0-or-later
#include "keyboard.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

const char kKeymapPath[] = "/usr/share/keymaps/tca8418_keypad_m5stack_keymap.map";
const char kByPath[] = "/dev/input/by-path/platform-3f804000.i2c-event";

// Sym-layer keycodes as the stock keymap assigns them.
const struct { uint16_t code; const char *sym; } kSymFallback[] = {
    {26, "!"}, {27, "@"}, {39, "#"}, {40, "$"}, {41, "%"}, {43, "^"}, {51, "&"}, {52, "*"},
    {53, "("}, {94, ")"}, {55, "~"}, {69, "`"}, {70, "_"}, {71, "-"}, {72, "+"}, {73, "="},
    {74, "["}, {75, "]"}, {76, "{"}, {77, "}"}, {79, ";"}, {80, ":"}, {81, "'"}, {82, "\""},
    {83, "<"}, {85, ">"}, {86, "\\"}, {89, "|"}, {90, ","}, {91, "."}, {92, "/"}, {93, "?"},
};

// X keysym names the keymap file uses for those symbols.
const struct { const char *name; const char *sym; } kKeysyms[] = {
    {"exclam", "!"}, {"at", "@"}, {"numbersign", "#"}, {"dollar", "$"}, {"percent", "%"},
    {"asciicircum", "^"}, {"ampersand", "&"}, {"asterisk", "*"}, {"parenleft", "("},
    {"parenright", ")"}, {"asciitilde", "~"}, {"grave", "`"}, {"underscore", "_"}, {"minus", "-"},
    {"plus", "+"}, {"equal", "="}, {"bracketleft", "["}, {"bracketright", "]"}, {"braceleft", "{"},
    {"braceright", "}"}, {"semicolon", ";"}, {"colon", ":"}, {"apostrophe", "'"}, {"quoteright", "'"},
    {"quotedbl", "\""}, {"less", "<"}, {"greater", ">"}, {"backslash", "\\"}, {"bar", "|"},
    {"comma", ","}, {"period", "."}, {"slash", "/"}, {"question", "?"},
};

const char kRowQ[] = "qwertyuiop", kRowA[] = "asdfghjkl", kRowZ[] = "zxcvbnm";

char letterFor(uint16_t code) {
    if (code >= KEY_Q && code <= KEY_P) return kRowQ[code - KEY_Q];
    if (code >= KEY_A && code <= KEY_L) return kRowA[code - KEY_A];
    if (code >= KEY_Z && code <= KEY_M) return kRowZ[code - KEY_Z];
    return 0;
}

bool isKeypad(int fd) {
    char name[128] = {};
    ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
    return strstr(name, "tca8418") != nullptr;
}

}  // namespace

bool Keyboard::open() {
    loadKeymap();
    const char *env = getenv("CAMILLIA_KBD");
    if (env && *env) {
        _fd = ::open(env, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        return _fd >= 0;
    }
    if ((_fd = ::open(kByPath, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) >= 0) return true;
    if (DIR *d = opendir("/dev/input")) {
        while (dirent *e = readdir(d)) {
            if (strncmp(e->d_name, "event", 5)) continue;
            std::string path = std::string("/dev/input/") + e->d_name;
            int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) continue;
            if (isKeypad(fd)) { _fd = fd; break; }
            close(fd);
        }
        closedir(d);
    }
    return _fd >= 0;
}

void Keyboard::loadKeymap() {
    for (auto &s : kSymFallback) _sym[s.code] = s.sym;
    FILE *f = fopen(kKeymapPath, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        unsigned code;
        char name[64];
        if (sscanf(line, " keycode %u = %63s", &code, name) != 2 || code >= 256) continue;
        // Letters, digits and space are decoded below, with shift and caps lock.
        if (letterFor((uint16_t)code) || (code >= KEY_1 && code <= KEY_0) || code == KEY_SPACE) continue;
        for (auto &k : kKeysyms)
            if (!strcmp(name, k.name) || (name[0] == '+' && !strcmp(name + 1, k.name))) _sym[code] = k.sym;
    }
    fclose(f);
}

KeyEvent Keyboard::read() {
    input_event ev;
    while (_fd >= 0 && ::read(_fd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
        if (ev.type != EV_KEY) continue;
        const uint16_t c = ev.code;
        if (c == KEY_LEFTSHIFT || c == KEY_RIGHTSHIFT) {
            if (ev.value == 1) { _shift = true; _shiftUsed = false; }
            else if (ev.value == 0) {
                _shift = false;
                // Tapped on its own: applies to the next key, as on the Cardputer.
                if (!_shiftUsed) _oneShot = !_oneShot;
            }
            continue;
        }
        if (ev.value == 0) continue;            // releases
        if (c == KEY_CAPSLOCK) { _caps = !_caps; continue; }

        KeyEvent k;
        switch (c) {
        case KEY_ENTER: case KEY_KPENTER: k.key = Key::Enter; break;
        case KEY_BACKSPACE: k.key = Key::Backspace; break;
        case KEY_DELETE: k.key = Key::Delete; break;
        case KEY_ESC: k.key = Key::Esc; break;
        case KEY_TAB: k.key = Key::Tab; break;
        case KEY_UP: k.key = Key::Up; break;
        case KEY_DOWN: k.key = Key::Down; break;
        case KEY_LEFT: k.key = Key::Left; break;
        case KEY_RIGHT: k.key = Key::Right; break;
        case KEY_PAGEUP: k.key = Key::PageUp; break;
        case KEY_PAGEDOWN: k.key = Key::PageDown; break;
        case KEY_HOME: k.key = Key::Home; break;
        case KEY_END: k.key = Key::End; break;
        case KEY_SPACE: k.key = Key::Char; k.text = " "; break;
        default:
            if (char l = letterFor(c)) {
                bool upper = (_shift || _oneShot) != _caps;
                k.key = Key::Char;
                k.text = std::string(1, upper ? (char)(l - 32) : l);
            } else if (c >= KEY_1 && c <= KEY_0) {
                k.key = Key::Char;
                k.text = std::string(1, "1234567890"[c - KEY_1]);
            } else if (c < 256 && !_sym[c].empty()) {
                k.key = Key::Char;
                k.text = _sym[c];
            }
        }
        if (k.key == Key::None) continue;
        _shiftUsed = true;
        _oneShot = false;
        return k;
    }
    return {};
}
