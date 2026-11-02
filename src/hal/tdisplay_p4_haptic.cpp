// T-Display P4 vibration motor (AW86224). See tdisplay_p4_haptic.h.
//
// The register sequence is Wadamesh's (variants/tdisplay_p4/P4Haptic.cpp),
// itself adapted from LilyGO's cpp_bus_driver Aw862xx driver (GPL-3.0,
// github.com/Llgok/cpp_bus_driver, src/chip/i2c/aw862xx.cpp): RAM-mode playback
// of one waveform. What differs is the timing. Wadamesh runs each buzz on its
// own FreeRTOS task and sleeps through it, holding its Wire1 mutex for the
// start and stop writes. Camillia's Wire1 sharing has no mutex -- it is safe
// only because every user is on the main loop -- so here the pattern is a
// small state machine that service() advances from the loop.
#include "tdisplay_p4_haptic.h"

#include <Arduino.h>
#include <Wire.h>

#include "tdisplay_p4_haptic_waveform.h"
#include "tdisplay_p4_io.h"

namespace {

// lilygo_device_driver t_display_p4/v1/config.h: AW86224 on IIC_2 (20/21).
constexpr uint8_t kAddr = 0x58;

constexpr uint8_t AW_ID       = 0x00;   // chip id (read)
constexpr uint8_t AW_PLAYCFG2 = 0x07;   // RAM/RTP playback gain
constexpr uint8_t AW_PLAYCFG3 = 0x08;   // [1:0] play mode (0 = RAM), [2] auto brake
constexpr uint8_t AW_PLAYCFG4 = 0x09;   // [0] go, [1] stop
constexpr uint8_t AW_WAVCFG1  = 0x0A;   // sequencer slot 1: waveform number
constexpr uint8_t AW_WAVCFG2  = 0x0B;   // sequencer slot 2 (0 = end of sequence)
constexpr uint8_t AW_WAVCFG9  = 0x12;   // [7:4] slot 1 loop count (15 = forever)
constexpr uint8_t AW_RTPCFG1  = 0x2D;   // [3:0] RAM base address high
constexpr uint8_t AW_RTPCFG2  = 0x2E;   // RAM base address low
constexpr uint8_t AW_RTPCFG3  = 0x2F;   // FIFO almost-empty/full thresholds (3 bytes)
constexpr uint8_t AW_GLBRD5   = 0x3F;   // [3:0] global state (0 = standby)
constexpr uint8_t AW_RAMADDRH = 0x40;   // SRAM pointer (2 bytes)
constexpr uint8_t AW_RAMDATA  = 0x42;   // SRAM data port
constexpr uint8_t AW_SYSCTRL1 = 0x43;   // [3] RAM init (SRAM access clock)
constexpr uint8_t AW_SYSCTRL2 = 0x44;   // [1:0] sample rate (2 = 12 kHz), [6] force standby
constexpr uint8_t AW_SYSCTRL7 = 0x49;   // [6] playing-changed gain bypass

constexpr uint16_t kBase  = 0x0800;     // from the waveform header
constexpr uint8_t  kWave  = 1;          // ~17.6 ms buzz (211 samples at 12 kHz)
constexpr uint16_t kWaveMs10 = 176;     // its length in tenths of a millisecond
constexpr size_t   kChunk = 32;         // SRAM bytes per transaction

constexpr uint8_t kGainDefault = 0x80;  // LilyGO's default; Wadamesh found it right
constexpr uint8_t kGainSoft    = 0x50;

// Every access points Wire1 back at the codec pair first: the keyboard poll
// re-pins it to 46/45 in between.
TwoWire &bus() { return tdisplayP4I2c1(TDISPLAY_P4_I2C1_AUDIO); }

bool awWrite(uint8_t reg, uint8_t val) {
    TwoWire &w = bus();
    w.beginTransmission(kAddr);
    w.write(reg);
    w.write(val);
    return w.endTransmission() == 0;
}
bool awWriteN(uint8_t reg, const uint8_t *data, size_t len) {
    TwoWire &w = bus();
    w.beginTransmission(kAddr);
    w.write(reg);
    w.write(data, len);
    return w.endTransmission() == 0;
}
bool awReadN(uint8_t reg, uint8_t *data, size_t len) {
    TwoWire &w = bus();
    w.beginTransmission(kAddr);
    w.write(reg);
    if (w.endTransmission(false) != 0) return false;
    if (w.requestFrom(kAddr, (size_t)len) != len) return false;
    for (size_t i = 0; i < len; i++) data[i] = (uint8_t)w.read();
    return true;
}
bool awRead(uint8_t reg, uint8_t *val) { return awReadN(reg, val, 1); }
// Read-modify-write: clear `mask`, set `bits`.
bool awUpdate(uint8_t reg, uint8_t mask, uint8_t bits) {
    uint8_t v;
    return awRead(reg, &v) && awWrite(reg, (uint8_t)((v & ~mask) | bits));
}
bool awSetRamAddr(uint16_t addr) {
    const uint8_t a[2] = {(uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF)};
    return awWriteN(AW_RAMADDRH, a, sizeof(a));
}

bool awIdle() {
    uint8_t st;
    return awRead(AW_GLBRD5, &st) && (st & 0x0F) == 0;
}

// Stop flag + forced standby until the state machine reports standby. Returns
// at once when the chip is already idle, which is the usual case: a beat that
// fits the chip's loop count has stopped by itself by the time this runs.
bool awStop() {
    if (awIdle()) return true;
    if (!awUpdate(AW_PLAYCFG4, 0x02, 0x02)) return false;
    if (!awUpdate(AW_SYSCTRL2, 0x40, 0x40)) return false;
    bool idle = false;
    for (int i = 0; i < 100 && !idle; i++) {
        idle = awIdle();
        if (!idle) delay(1);
    }
    return awUpdate(AW_SYSCTRL2, 0x40, 0x00) && idle;
}

// The chip keeps its SRAM across a P4 reset while it stays powered, so compare
// first and only write when it differs. Pointer set per chunk, since it is not
// guaranteed to survive between transactions.
bool awSramMatches() {
    uint8_t buf[kChunk];
    for (size_t off = 0; off < sizeof(kP4HapticWaveform); off += kChunk) {
        const size_t n = min(kChunk, sizeof(kP4HapticWaveform) - off);
        if (!awSetRamAddr(kBase + off) || !awReadN(AW_RAMDATA, buf, n)) return false;
        if (memcmp(buf, kP4HapticWaveform + off, n) != 0) return false;
    }
    return true;
}
bool awSramLoad() {
    for (size_t off = 0; off < sizeof(kP4HapticWaveform); off += kChunk) {
        const size_t n = min(kChunk, sizeof(kP4HapticWaveform) - off);
        if (!awSetRamAddr(kBase + off) ||
            !awWriteN(AW_RAMDATA, kP4HapticWaveform + off, n)) return false;
    }
    return true;
}

// Sample rate, then (with SRAM access on) base address + FIFO thresholds +
// waveform data.
bool awInit(bool *reloaded) {
    *reloaded = false;
    if (!awUpdate(AW_SYSCTRL2, 0x03, 0x02)) return false;   // 12 kHz waveform data
    if (!awStop()) return false;
    if (!awUpdate(AW_SYSCTRL1, 0x08, 0x08)) return false;   // RAM init on
    delayMicroseconds(500);
    const uint16_t ae = kBase >> 1, af = kBase - (kBase >> 2);   // vendor formula
    const uint8_t fifo[3] = {(uint8_t)(((ae >> 8) << 4) | ((af >> 8) & 0x0F)),
                             (uint8_t)(ae & 0xFF), (uint8_t)(af & 0xFF)};
    bool ok = awUpdate(AW_RTPCFG1, 0x0F, (uint8_t)((kBase >> 8) & 0x0F)) &&
              awWrite(AW_RTPCFG2, (uint8_t)(kBase & 0xFF)) &&
              awWriteN(AW_RTPCFG3, fifo, sizeof(fifo));
    if (ok && !awSramMatches()) {
        *reloaded = true;
        ok = awSramLoad() && awSramMatches();
    }
    const bool ramOff = awUpdate(AW_SYSCTRL1, 0x08, 0x00);  // RAM init off
    return ok && ramOff;
}

// Starts the motor for ~ms. A beat short enough for the chip's loop counter
// (up to 15 plays, ~264 ms) is timed by the chip and brakes by itself; a
// longer one loops forever and relies on service() stopping it. service()
// issues the stop either way, so a miscount here can only ever cut a beat
// short, never leave the motor running.
bool awStart(uint16_t ms, uint8_t gain) {
    uint32_t plays = ((uint32_t)ms * 10 + kWaveMs10 - 1) / kWaveMs10;
    if (plays < 1) plays = 1;
    const uint8_t loops = plays <= 15 ? (uint8_t)(plays - 1) : 0x0F;
    return awWrite(AW_WAVCFG1, kWave) && awWrite(AW_WAVCFG2, 0) &&
           awUpdate(AW_WAVCFG9, 0xF0, (uint8_t)(loops << 4)) &&
           awUpdate(AW_PLAYCFG3, 0x07, 0x04) &&        // RAM mode, auto brake
           awUpdate(AW_SYSCTRL7, 0x40, 0x40) &&
           awWrite(AW_PLAYCFG2, gain) &&
           awUpdate(AW_PLAYCFG4, 0x01, 0x01);          // go
}

// The four styles Wadamesh offers (Settings > Vibration there). on2 == 0 is a
// single buzz per beat.
struct Style {
    uint16_t on1, gap1, on2, beatGap;
    uint8_t  gain;
};
const Style kStyles[HAPTIC_STYLE_COUNT] = {
    {  0,  0,   0,   0, 0},              // Off
    {120,  0,   0, 120, kGainDefault},   // Pulse
    { 60,  0,   0, 140, kGainSoft},      // Gentle: short and softer
    {350,  0,   0, 200, kGainDefault},   // Long
    { 60, 90, 120, 300, kGainSoft},      // Heartbeat: lub-dub
};

// The pattern flattened into steps: {duration, motor on?}.
struct Step {
    uint16_t ms;
    bool     on;
};
constexpr int kMaxBeats = 3;
Step    s_steps[kMaxBeats * 4];
int     s_stepCount = 0;
int     s_step = -1;           // -1 = idle
uint8_t s_gain = kGainDefault;
uint32_t s_stepStartMs = 0;

bool s_ready = false;
bool s_failed = false;

void beginStep(int i) {
    s_step = i;
    s_stepStartMs = millis();
    if (s_steps[i].on && !awStart(s_steps[i].ms, s_gain)) {
        Serial.println("[p4-haptic] start failed");
    }
}

} // namespace

bool tdisplayP4HapticReady() {
    if (s_ready) return true;
    if (s_failed) return false;
    uint8_t id = 0xFF;
    bool reloaded = false;
    const bool present = awRead(AW_ID, &id);
    const bool inited = present && awInit(&reloaded);
    if (!inited) {
        Serial.printf("[p4-haptic] AW86224 %s\n", present ? "init failed" : "not found @0x58");
        s_failed = true;
        return false;
    }
    Serial.printf("[p4-haptic] AW86224 up (id 0x%02X, waveform RAM %s)\n", id,
                  reloaded ? "loaded" : "retained");
    s_ready = true;
    return true;
}

void tdisplayP4HapticPlay(uint8_t style, uint8_t beats) {
    if (style == HAPTIC_STYLE_OFF || style >= HAPTIC_STYLE_COUNT) return;
    if (s_step >= 0) return;                  // already buzzing -- don't queue
    if (!tdisplayP4HapticReady()) return;
    if (beats < 1) beats = 1;
    if (beats > kMaxBeats) beats = kMaxBeats;
    const Style &st = kStyles[style];
    s_stepCount = 0;
    for (uint8_t b = 0; b < beats; b++) {
        if (b) s_steps[s_stepCount++] = {st.beatGap, false};
        s_steps[s_stepCount++] = {st.on1, true};
        if (st.on2) {
            s_steps[s_stepCount++] = {st.gap1, false};
            s_steps[s_stepCount++] = {st.on2, true};
        }
    }
    s_gain = st.gain;
    beginStep(0);
}

void tdisplayP4HapticTick() {
    if (s_step >= 0) return;
    if (!tdisplayP4HapticReady()) return;
    // One play of the waveform: the shortest buzz the chip makes from it, and
    // well short of every message style, so it never reads as a new message.
    s_steps[0] = {(uint16_t)((kWaveMs10 + 9) / 10), true};
    s_stepCount = 1;
    s_gain = kGainDefault;
    beginStep(0);
}

void tdisplayP4HapticService() {
    if (s_step < 0) return;
    if ((uint32_t)(millis() - s_stepStartMs) < s_steps[s_step].ms) return;
    if (s_steps[s_step].on) {
        // Must not be skipped: a long beat loops forever until told to stop.
        bool stopped = false;
        for (int i = 0; i < 3 && !stopped; i++) stopped = awStop();
        if (!stopped) Serial.println("[p4-haptic] stop failed");
    }
    if (s_step + 1 >= s_stepCount) {
        s_step = -1;
        return;
    }
    beginStep(s_step + 1);
}
