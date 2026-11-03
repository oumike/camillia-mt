#pragma once
// T-Display P4 vibration motor: an LRA driven by an AW86224 haptic driver on
// I2C controller 1's audio route (pins 20/21, 0x58), the same pair as the
// ES8311 codec. Ported from Wadamesh (variants/tdisplay_p4/P4Haptic.cpp).
//
// Everything here runs on the main loop task, because that is the only thing
// that makes sharing Wire1 between the keyboard and the codec safe (see
// tdisplayP4I2c1()). Nothing blocks: play() writes the start registers and
// returns, and service() -- called every loop pass -- starts the next beat and
// stops the motor when a beat is due to end. The chip plays the waveform out
// of its own SRAM in between, so the motor keeps running while a blocking
// notification tone holds up the loop.

#include <stdint.h>

// Vibration styles, as stored in the config (cfgHapticStyle()). Off is a style
// so the whole setting is one row that cycles.
enum HapticStyle : uint8_t {
    HAPTIC_STYLE_OFF = 0,
    HAPTIC_STYLE_PULSE,
    HAPTIC_STYLE_GENTLE,
    HAPTIC_STYLE_LONG,
    HAPTIC_STYLE_HEARTBEAT,
    HAPTIC_STYLE_COUNT,
};

// Probe the chip and load its waveform RAM (idempotent; lazy on first play).
// False when it never answered.
bool tdisplayP4HapticReady();

// Play `style` `beats` times (1 = channel message, 2 = DM). Ignored while a
// pattern is already playing, so a burst of messages is one buzz, not a queue.
void tdisplayP4HapticPlay(uint8_t style, uint8_t beats);

// One short, light tap (~18 ms): the "your message was ACKed" cue. Ignored
// while a pattern is playing, like play().
void tdisplayP4HapticTick();

// Call every loop pass. Cheap when idle.
void tdisplayP4HapticService();
