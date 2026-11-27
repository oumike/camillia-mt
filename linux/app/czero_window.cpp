// SPDX-License-Identifier: GPL-3.0-or-later
#include "czero_window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <sstream>
#include <string>

#include "config.h"
#include "czero_paths.h"

#if CZERO_DESKTOP
#include <SDL2/SDL.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

// The window opens at the panel's starting size (hw_cardputer_zero.h) and can
// grow to CZERO_WINDOW_MAX_W x _H.
static constexpr int kW = DEVICE_LCD_LANDSCAPE_W;
static constexpr int kH = DEVICE_LCD_LANDSCAPE_H;
static constexpr int kMaxW = CZERO_WINDOW_MAX_W;
static constexpr int kMaxH = CZERO_WINDOW_MAX_H;
// How long a size has to hold before the UI is laid out for it, so dragging a
// corner does not rebuild it on every step.
static constexpr uint32_t kResizeSettleMs = 150;

static uint16_t s_frame[kMaxW * kMaxH];
static std::atomic<uint8_t> s_level{255};
static std::atomic<bool> s_closed{false};
static std::atomic<bool> s_escDown{false};
static std::mutex s_keyMx;
static std::deque<KeyEvent> s_keys;
// Sizes packed as w << 16 | h, so each is read whole.
static std::atomic<uint32_t> s_shown{(uint32_t)kW << 16 | kH};
static std::atomic<uint32_t> s_want{(uint32_t)kW << 16 | kH};
static std::atomic<uint32_t> s_wantMs{0};
// Mouse: x << 16 | y, and the left button.
static std::atomic<uint32_t> s_mouse{0};
static std::atomic<bool> s_mouseDown{false};
static std::atomic<int> s_wheel{0};

uint16_t *czeroWindowFrame() { return s_frame; }
void czeroWindowSetLevel(uint8_t level) { s_level = level; }
bool czeroWindowClosed() { return s_closed; }
bool czeroWindowEscDown() { return s_escDown; }
void czeroWindowShow(int w, int h) { s_shown = (uint32_t)w << 16 | (uint32_t)h; }

bool czeroWindowSize(int &w, int &h) {
    if (SDL_GetTicks() - s_wantMs < kResizeSettleMs) return false;
    const uint32_t v = s_want;
    w = (int)(v >> 16);
    h = (int)(v & 0xFFFF);
    return true;
}

bool czeroWindowPointer(int32_t &x, int32_t &y) {
    const uint32_t v = s_mouse;
    x = (int32_t)(v >> 16);
    y = (int32_t)(v & 0xFFFF);
    return s_mouseDown;
}

int czeroWindowTakeWheel() { return s_wheel.exchange(0); }

bool czeroWindowTakeKey(KeyEvent &out) {
    std::lock_guard<std::mutex> lock(s_keyMx);
    if (s_keys.empty()) return false;
    out = std::move(s_keys.front());
    s_keys.pop_front();
    return true;
}

static void pushKey(Key k, const char *text = nullptr) {
    KeyEvent e;
    e.key = k;
    if (text) e.text = text;
    std::lock_guard<std::mutex> lock(s_keyMx);
    s_keys.push_back(std::move(e));
}

static void onKeyDown(const SDL_KeyboardEvent &k) {
    if (k.keysym.sym == SDLK_ESCAPE) {
        // Released or held, as on the keypad: keys_linux.cpp decides.
        if (!k.repeat) {
            s_escDown = true;
            pushKey(Key::Esc);
        }
        return;
    }
    switch (k.keysym.sym) {
        case SDLK_RETURN: case SDLK_KP_ENTER: pushKey(Key::Enter); break;
        case SDLK_BACKSPACE: pushKey(Key::Backspace); break;
        case SDLK_DELETE: pushKey(Key::Delete); break;
        case SDLK_TAB: pushKey(Key::Tab); break;
        case SDLK_UP: pushKey(Key::Up); break;
        case SDLK_DOWN: pushKey(Key::Down); break;
        case SDLK_LEFT: pushKey(Key::Left); break;
        case SDLK_RIGHT: pushKey(Key::Right); break;
        case SDLK_PAGEUP: pushKey(Key::PageUp); break;
        case SDLK_PAGEDOWN: pushKey(Key::PageDown); break;
        case SDLK_HOME: pushKey(Key::Home); break;
        case SDLK_END: pushKey(Key::End); break;
        default: break;
    }
}

static void noteMouse(int x, int y) {
    x = x < 0 ? 0 : (x > 0xFFFF ? 0xFFFF : x);
    y = y < 0 ? 0 : (y > 0xFFFF ? 0xFFFF : y);
    s_mouse = (uint32_t)x << 16 | (uint32_t)y;
}

static void noteWindowSize(int w, int h) {
    w = (w < kW ? kW : (w > kMaxW ? kMaxW : w)) & ~1;
    h = (h < kH ? kH : (h > kMaxH ? kMaxH : h)) & ~1;
    const uint32_t v = (uint32_t)w << 16 | (uint32_t)h;
    if (v == s_want) return;
    s_wantMs = SDL_GetTicks();
    s_want = v;
}

static void windowThread(SDL_Window *win, SDL_Renderer *ren) {
    static uint16_t shown[kMaxW * kMaxH];
    SDL_Texture *tex = nullptr;
    int texW = 0, texH = 0;
    uint8_t shownLevel = 0;
    bool first = true;
    SDL_StartTextInput();
    while (!s_closed) {
        SDL_Event ev;
        if (SDL_WaitEventTimeout(&ev, 33)) {
            do {
                switch (ev.type) {
                    case SDL_QUIT: s_closed = true; break;
                    case SDL_KEYDOWN: onKeyDown(ev.key); break;
                    case SDL_KEYUP:
                        if (ev.key.keysym.sym == SDLK_ESCAPE) s_escDown = false;
                        break;
                    case SDL_TEXTINPUT: pushKey(Key::Char, ev.text.text); break;
                    case SDL_MOUSEMOTION: noteMouse(ev.motion.x, ev.motion.y); break;
                    case SDL_MOUSEBUTTONDOWN:
                    case SDL_MOUSEBUTTONUP:
                        if (ev.button.button != SDL_BUTTON_LEFT) break;
                        noteMouse(ev.button.x, ev.button.y);
                        s_mouseDown = ev.type == SDL_MOUSEBUTTONDOWN;
                        break;
                    case SDL_MOUSEWHEEL:
                        s_wheel += ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -ev.wheel.y : ev.wheel.y;
                        break;
                    case SDL_WINDOWEVENT:
                        if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                            // No key-up or button-up arrives for one let go in
                            // another window.
                            s_escDown = false;
                            s_mouseDown = false;
                        }
                        if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                            noteWindowSize(ev.window.data1, ev.window.data2);
                        }
                        first = true;  // exposed or resized: draw again
                        break;
                    default: break;
                }
            } while (SDL_PollEvent(&ev));
        }
        const uint32_t size = s_shown;
        const int w = (int)(size >> 16), h = (int)(size & 0xFFFF);
        const size_t bytes = (size_t)w * h * 2;
        const uint8_t level = s_level;
        if (w != texW || h != texH) {
            if (tex) SDL_DestroyTexture(tex);
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w, h);
            texW = w;
            texH = h;
            first = true;
        }
        if (!tex) continue;
        if (!first && level == shownLevel && memcmp(shown, s_frame, bytes) == 0) continue;
        memcpy(shown, s_frame, bytes);
        shownLevel = level;
        first = false;
        SDL_UpdateTexture(tex, nullptr, shown, w * 2);
        SDL_SetTextureColorMod(tex, level, level, level);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        // One frame pixel to one window pixel, from the top left; the frame
        // catches up with a new window size a moment after it settles.
        const SDL_Rect dst{0, 0, w, h};
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        SDL_RenderPresent(ren);
    }
    (void)win;
}

bool czeroWindowOpen() {
    // SDL is wholly owned by its thread; this one waits for the outcome.
    std::atomic<int> ok{0};
    std::thread([&ok] {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        SDL_Window *win = nullptr;
        SDL_Renderer *ren = nullptr;
        if (SDL_Init(SDL_INIT_VIDEO) == 0
            && (win = SDL_CreateWindow("Camillia", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       kW, kH, SDL_WINDOW_RESIZABLE))
            && (ren = SDL_CreateRenderer(win, -1, 0))) {
            SDL_SetWindowMinimumSize(win, kW, kH);
            ok = 1;
            windowThread(win, ren);
            return;
        }
        fprintf(stderr, "[app] window: %s\n", SDL_GetError());
        ok = -1;
    }).detach();
    while (ok == 0) SDL_Delay(5);
    return ok > 0;
}

#else  // the LCD build: no window

uint16_t *czeroWindowFrame() { return nullptr; }
bool czeroWindowOpen() { return false; }
void czeroWindowSetLevel(uint8_t) {}
bool czeroWindowClosed() { return false; }
bool czeroWindowTakeKey(KeyEvent &) { return false; }
bool czeroWindowEscDown() { return false; }
bool czeroWindowSize(int &, int &) { return false; }
void czeroWindowShow(int, int) {}
bool czeroWindowPointer(int32_t &, int32_t &) { return false; }
int czeroWindowTakeWheel() { return 0; }

#endif  // CZERO_DESKTOP

static bool envSet(const char *name) {
    const char *v = getenv(name);
    return v && *v;
}

static bool readAll(const std::string &path, std::string &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

void czeroInstallMenuEntry() {
    static const char *kPackageEntry = "/usr/share/camillia/camillia.desktop";
    std::string dir;
    if (envSet("XDG_DATA_HOME")) dir = getenv("XDG_DATA_HOME");
    else if (envSet("HOME")) dir = std::string(getenv("HOME")) + "/.local/share";
    else return;
    dir += "/applications";
    std::string want, have;
    // Nothing to copy when run from a build rather than the package.
    if (!readAll(kPackageEntry, want)) return;
    const std::string dst = dir + "/camillia.desktop";
    if (readAll(dst, have) && have == want) return;
    if (!czeroMkdirs(dir)) return;
    const std::string tmp = dst + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out || !(out << want) || !out.flush()) return;
    }
    rename(tmp.c_str(), dst.c_str());
}
