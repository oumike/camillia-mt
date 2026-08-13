#pragma once

#include <stddef.h>
#include <stdint.h>

// Browser-accessible screen mirror for the T-Deck. This is a compact custom
// WebSocket transport, not an RFB server: the device serves its own viewer and
// streams RGB565 dirty regions to it.
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

// A new browser or a dropped update requests a complete LVGL repaint so the
// remote framebuffer can heal without retaining stale regions.
bool vncHostTakeFullRepaintRequest();

// Virtual pointer and keyboard input produced by the browser. Pointer state is
// sampled by an LVGL indev; keys are drained through the normal T-Deck handler.
bool vncHostReadPointer(int16_t *x, int16_t *y, bool *pressed);
bool vncHostPopKey(uint16_t *codepoint);
