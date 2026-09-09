#pragma once

#include <stdint.h>
#include <stddef.h>

#include "config.h"   // HAS_VNC_HOST, via hal/board.h

// Browser-accessible screen mirror, built for any board with HAS_VNC_HOST. This
// is a compact custom WebSocket transport, not an RFB server: the device serves
// its own viewer and streams RGB565 dirty regions to it.
//
// Nothing here assumes a panel size — the dimensions passed to vncHostInit()
// are what the viewer sizes its canvas to, over the wire.
void vncHostInit(uint16_t width, uint16_t height);
bool vncHostSetEnabled(bool enabled);
bool vncHostEnabled();
bool vncHostRunning();
bool vncHostClientConnected();
const char *vncHostIP();
uint16_t vncHostPort();

// Called from the LVGL flush callback. Pixels are native little-endian RGB565.
void vncHostCaptureFlush(int32_t x, int32_t y, int32_t width, int32_t height,
                         const uint16_t *pixels);

// The same, for a panel LVGL drives at LV_COLOR_FORMAT_I1 (the T-Deck Pro's
// e-paper). `bits` is the pixel data with LVGL's two-entry palette already
// skipped: rows of `strideBytes`, MSB first, 0 = black and 1 = white — the same
// bits the panel driver is handed, read with the same assumption.
//
// Expanding to RGB565 here rather than in the caller is what keeps this cheap:
// the bits go straight into the mirror's own framebuffer, so a 1 bpp board
// needs no full-frame scratch buffer to be mirrored in colour.
void vncHostCaptureFlushI1(int32_t x, int32_t y, int32_t width, int32_t height,
                           const uint8_t *bits, size_t strideBytes);

// A new browser or a dropped update requests a complete LVGL repaint so the
// remote framebuffer can heal without retaining stale regions.
bool vncHostTakeFullRepaintRequest();

// Virtual pointer and keyboard input produced by the browser. Pointer state is
// sampled by an LVGL indev; keys are drained through the normal T-Deck handler.
bool vncHostReadPointer(int16_t *x, int16_t *y, bool *pressed);
bool vncHostPopKey(uint16_t *codepoint);

// When the browser last sent a key (millis, 0 if it never has) and how many are
// still queued. These mirror keyboardLastKeyMs()/keyboardPendingKeys() so a
// panel that paces its redraws against typing can see remote typing too — the
// e-paper build's refresh hold reads both sources and takes the newer.
uint32_t vncHostLastKeyMs();
uint8_t vncHostPendingKeys();
