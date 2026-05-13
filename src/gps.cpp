#include "gps.h"
#include "config.h"
#include "debug_flags.h"
#include <TinyGPSPlus.h>
#include <Wire.h>

#if defined(DEVICE_TLORA_PAGER_TFT)
namespace {
constexpr uint8_t kXl9555RegOut0 = 0x02;
constexpr uint8_t kXl9555RegOut1 = 0x03;
constexpr uint8_t kXl9555RegCfg0 = 0x06;
constexpr uint8_t kXl9555RegCfg1 = 0x07;

constexpr uint8_t kExpGpsEn  = 4;
constexpr uint8_t kExpGpsRst = 7;

static bool xl9555WriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static bool xl9555ReadReg(uint8_t addr, uint8_t reg, uint8_t &val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, 1) != 1) return false;
    val = Wire.read();
    return true;
}

static void xl9555SetOutput(uint8_t pin, bool level, bool invertDirSense,
                            uint8_t &out0, uint8_t &out1,
                            uint8_t &cfg0, uint8_t &cfg1) {
    uint8_t bit = (uint8_t)(1U << (pin & 0x07));
    if (pin < 8) {
        if (invertDirSense) cfg0 |= bit;
        else                cfg0 &= (uint8_t)~bit;
        if (level) out0 |= bit;
        else       out0 &= (uint8_t)~bit;
    } else {
        if (invertDirSense) cfg1 |= bit;
        else                cfg1 &= (uint8_t)~bit;
        if (level) out1 |= bit;
        else       out1 &= (uint8_t)~bit;
    }
}

static int pagerFindExpanderAddr() {
    for (uint8_t a = 0x20; a <= 0x27; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) return (int)a;
    }
    return -1;
}

static bool pagerPrimeGpsRails(bool invertDirSense) {
    Wire.begin(KB_SDA, KB_SCL);
    int expAddr = pagerFindExpanderAddr();
    if (expAddr < 0) {
        Serial.println("[gps] pager expander not found (0x20-0x27)");
        return false;
    }

    uint8_t out0 = 0xFF, out1 = 0xFF, cfg0 = 0xFF, cfg1 = 0xFF;
    if (!xl9555ReadReg((uint8_t)expAddr, kXl9555RegOut0, out0)
        || !xl9555ReadReg((uint8_t)expAddr, kXl9555RegOut1, out1)
        || !xl9555ReadReg((uint8_t)expAddr, kXl9555RegCfg0, cfg0)
        || !xl9555ReadReg((uint8_t)expAddr, kXl9555RegCfg1, cfg1)) {
        Serial.printf("[gps] pager expander read failed addr=0x%02X\n", expAddr);
        return false;
    }

    // Ensure GPS rail is on, then pulse reset low->high to recover modules
    // that boot into a stale UART/output state.
    xl9555SetOutput(kExpGpsEn, true, invertDirSense, out0, out1, cfg0, cfg1);
    xl9555SetOutput(kExpGpsRst, false, invertDirSense, out0, out1, cfg0, cfg1);
    if (!xl9555WriteReg((uint8_t)expAddr, kXl9555RegOut0, out0)
        || !xl9555WriteReg((uint8_t)expAddr, kXl9555RegOut1, out1)
        || !xl9555WriteReg((uint8_t)expAddr, kXl9555RegCfg0, cfg0)
        || !xl9555WriteReg((uint8_t)expAddr, kXl9555RegCfg1, cfg1)) {
        Serial.printf("[gps] pager expander write failed addr=0x%02X invert=%d\n",
                      expAddr, invertDirSense ? 1 : 0);
        return false;
    }

    delay(20);
    xl9555SetOutput(kExpGpsRst, true, invertDirSense, out0, out1, cfg0, cfg1);
    (void)xl9555WriteReg((uint8_t)expAddr, kXl9555RegOut0, out0);
    (void)xl9555WriteReg((uint8_t)expAddr, kXl9555RegOut1, out1);
    Serial.printf("[gps] pager rails primed addr=0x%02X invert=%d\n",
                  expAddr, invertDirSense ? 1 : 0);
    return true;
}
} // namespace
#endif

// Minimum ms after GPS start before we trust fix data.
// The L76K's hot-start cache emits stale GGA with quality=1 and
// previous-session sats immediately; this blanking window filters it.
static const uint32_t GPS_WARMUP_MS = 10000;
static const uint32_t GPS_SATS_MAX_AGE_MS = 5000;
static const uint32_t GPS_SATS_HOLD_MS = 12000;
#if defined(DEVICE_HELTEC_V4_EXPANSION)
static const uint32_t GPS_BAUD_PROBE_START_MS = 3000;
static const uint32_t GPS_BAUD_PROBE_INTERVAL_MS = 2000;
static const uint32_t GPS_CHECKSUM_STALE_REPROBE_MS = 8000;
static const uint32_t GPS_MIN_CHECKSUM_FOR_STREAM = 2;
#elif defined(DEVICE_TLORA_PAGER_TFT)
static const uint32_t GPS_BAUD_PROBE_START_MS = 3000;
static const uint32_t GPS_BAUD_PROBE_INTERVAL_MS = 3500;
static const uint32_t GPS_CHECKSUM_STALE_REPROBE_MS = 10000;
static const uint32_t GPS_MIN_CHECKSUM_FOR_STREAM = 1;
#else
static const uint32_t GPS_BAUD_PROBE_START_MS = 9000;
static const uint32_t GPS_BAUD_PROBE_INTERVAL_MS = 7000;
static const uint32_t GPS_CHECKSUM_STALE_REPROBE_MS = 10000;
static const uint32_t GPS_MIN_CHECKSUM_FOR_STREAM = 1;
#endif
static const uint32_t GPS_STREAM_STALL_MS = 15000;

static const uint32_t GPS_BAUD_PROBE_LIST[] = {
    (uint32_t)GPS_BAUD,
    9600,
    38400,
    115200,
};
static const size_t GPS_BAUD_PROBE_COUNT = sizeof(GPS_BAUD_PROBE_LIST) / sizeof(GPS_BAUD_PROBE_LIST[0]);

struct GpsProbePort {
    int8_t rx;
    int8_t tx;
};

static const GpsProbePort GPS_PORT_PROBE_LIST[] = {
    { GPS_RX, GPS_TX },
#if defined(DEVICE_TLORA_PAGER_TFT)
    // Some pager units expose RX reliably but may not have GPS TX routed.
    { GPS_RX, -1 },
    // Also try reversed labeling in case the module UART is swapped.
    { GPS_TX, GPS_RX },
    { GPS_TX, -1 },
#endif
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    // Some Heltec revisions/modules are labeled opposite from effective UART wiring.
    { GPS_RX, -1 },
    { GPS_TX, GPS_RX },
    { GPS_TX, -1 },
    { 44, 43 },
    { 44, -1 },
    { 43, 44 },
    { 43, -1 },
    { 38, 39 },
    { 38, -1 },
    { 39, 38 },
    { 39, -1 },
#endif
};
static const size_t GPS_PORT_PROBE_COUNT = sizeof(GPS_PORT_PROBE_LIST) / sizeof(GPS_PORT_PROBE_LIST[0]);

static TinyGPSPlus    _gps;
static HardwareSerial _serial(1);   // UART1
static bool           _enabled       = false;
static uint32_t       _startMs       = 0;     // millis() when GPS was started
static uint32_t       _firstFixMs    = 0;     // millis() when first real fix arrived (0 = none yet)
static uint32_t       _prevSentences = 0;     // sentencesWithFix at last check
static uint32_t       _totalBytes    = 0;
static uint8_t        _lastSats      = 0;
static uint32_t       _lastSatsMs    = 0;
static uint8_t        _baudProbeIdx  = 0;
static uint8_t        _portProbeIdx  = 0;
static uint32_t       _activeBaud    = GPS_BAUD;
static int8_t         _activeRx      = GPS_RX;
static int8_t         _activeTx      = GPS_TX;
#if defined(DEVICE_TLORA_PAGER_TFT)
static bool           _pagerRailInverted = false;
static bool           _pagerRailRetried = false;
#endif
static uint32_t       _lastProbeMs   = 0;
static uint32_t       _lastByteMs    = 0;
static uint32_t       _lastChecksumMs = 0;
static uint32_t       _lastPassedChecksum = 0;
static bool           _nmeaSeen      = false;

// Some firmwares update GSA regularly while GGA satellite fields can remain stale.
// Track both GN and GP talkers and prefer fresh GSA "satellites used" counts.
static TinyGPSCustom  _gngsaSat01(_gps, "GNGSA", 3);
static TinyGPSCustom  _gngsaSat02(_gps, "GNGSA", 4);
static TinyGPSCustom  _gngsaSat03(_gps, "GNGSA", 5);
static TinyGPSCustom  _gngsaSat04(_gps, "GNGSA", 6);
static TinyGPSCustom  _gngsaSat05(_gps, "GNGSA", 7);
static TinyGPSCustom  _gngsaSat06(_gps, "GNGSA", 8);
static TinyGPSCustom  _gngsaSat07(_gps, "GNGSA", 9);
static TinyGPSCustom  _gngsaSat08(_gps, "GNGSA", 10);
static TinyGPSCustom  _gngsaSat09(_gps, "GNGSA", 11);
static TinyGPSCustom  _gngsaSat10(_gps, "GNGSA", 12);
static TinyGPSCustom  _gngsaSat11(_gps, "GNGSA", 13);
static TinyGPSCustom  _gngsaSat12(_gps, "GNGSA", 14);
static TinyGPSCustom* _gngsaSats[12] = {
    &_gngsaSat01, &_gngsaSat02, &_gngsaSat03, &_gngsaSat04,
    &_gngsaSat05, &_gngsaSat06, &_gngsaSat07, &_gngsaSat08,
    &_gngsaSat09, &_gngsaSat10, &_gngsaSat11, &_gngsaSat12
};

static TinyGPSCustom  _gpgsaSat01(_gps, "GPGSA", 3);
static TinyGPSCustom  _gpgsaSat02(_gps, "GPGSA", 4);
static TinyGPSCustom  _gpgsaSat03(_gps, "GPGSA", 5);
static TinyGPSCustom  _gpgsaSat04(_gps, "GPGSA", 6);
static TinyGPSCustom  _gpgsaSat05(_gps, "GPGSA", 7);
static TinyGPSCustom  _gpgsaSat06(_gps, "GPGSA", 8);
static TinyGPSCustom  _gpgsaSat07(_gps, "GPGSA", 9);
static TinyGPSCustom  _gpgsaSat08(_gps, "GPGSA", 10);
static TinyGPSCustom  _gpgsaSat09(_gps, "GPGSA", 11);
static TinyGPSCustom  _gpgsaSat10(_gps, "GPGSA", 12);
static TinyGPSCustom  _gpgsaSat11(_gps, "GPGSA", 13);
static TinyGPSCustom  _gpgsaSat12(_gps, "GPGSA", 14);
static TinyGPSCustom* _gpgsaSats[12] = {
    &_gpgsaSat01, &_gpgsaSat02, &_gpgsaSat03, &_gpgsaSat04,
    &_gpgsaSat05, &_gpgsaSat06, &_gpgsaSat07, &_gpgsaSat08,
    &_gpgsaSat09, &_gpgsaSat10, &_gpgsaSat11, &_gpgsaSat12
};

// GGA field 7 carries satellites used in current fix solution.
static TinyGPSCustom _gnggaSatsUsed(_gps, "GNGGA", 7);
static TinyGPSCustom _gpggaSatsUsed(_gps, "GPGGA", 7);

// GSV field 3 carries satellites in view, useful before the first full fix.
static TinyGPSCustom _gngsvSatsView(_gps, "GNGSV", 3);
static TinyGPSCustom _gpgsvSatsView(_gps, "GPGSV", 3);
static TinyGPSCustom _glgsvSatsView(_gps, "GLGSV", 3);
static TinyGPSCustom _gagsvSatsView(_gps, "GAGSV", 3);
static TinyGPSCustom _bdgsvSatsView(_gps, "BDGSV", 3);
static TinyGPSCustom _gbgsvSatsView(_gps, "GBGSV", 3);
static TinyGPSCustom _gqgsvSatsView(_gps, "GQGSV", 3);
static TinyGPSCustom _qzgsvSatsView(_gps, "QZGSV", 3);

static uint8_t gsaSatsUsed(TinyGPSCustom* const sats[12], bool &fresh) {
    fresh = false;
    uint8_t used = 0;
    for (int i = 0; i < 12; i++) {
        TinyGPSCustom *term = sats[i];
        if (!term) continue;
        if (term->isValid() && term->age() < GPS_SATS_MAX_AGE_MS) fresh = true;
        const char *prn = term->value();
        if (prn && prn[0] != '\0') used++;
    }
    return used;
}

static uint8_t parseCustomU8(TinyGPSCustom &term, bool &fresh) {
    fresh = false;
    if (!term.isValid()) return 0;
    if (term.age() >= GPS_SATS_MAX_AGE_MS) return 0;
    fresh = true;
    int v = atoi(term.value());
    if (v < 0) v = 0;
    if (v > 99) v = 99;
    return (uint8_t)v;
}

static void gpsApplyPortAndBaud(int8_t rx, int8_t tx, uint32_t baud) {
    _serial.end();
    _serial.begin(baud, SERIAL_8N1, rx, tx);
    _activeRx = rx;
    _activeTx = tx;
    _activeBaud = baud;
}

static bool gpsNextProbeConfig() {
    if (GPS_BAUD_PROBE_COUNT == 0 || GPS_PORT_PROBE_COUNT == 0) return false;

    size_t combos = GPS_BAUD_PROBE_COUNT * GPS_PORT_PROBE_COUNT;
    for (size_t attempt = 0; attempt < combos; attempt++) {
        _baudProbeIdx = (uint8_t)((_baudProbeIdx + 1) % GPS_BAUD_PROBE_COUNT);
        if (_baudProbeIdx == 0) {
            _portProbeIdx = (uint8_t)((_portProbeIdx + 1) % GPS_PORT_PROBE_COUNT);
        }

        const GpsProbePort &p = GPS_PORT_PROBE_LIST[_portProbeIdx];
        uint32_t candidateBaud = GPS_BAUD_PROBE_LIST[_baudProbeIdx];
        if (p.rx < 0) continue;
        if (candidateBaud == _activeBaud && p.rx == _activeRx && p.tx == _activeTx) continue;

        gpsApplyPortAndBaud(p.rx, p.tx, candidateBaud);
    #if defined(DEVICE_TLORA_PAGER_TFT)
        Serial.printf("[gps] probing baud=%lu (rx=%d tx=%d)\n",
                  (unsigned long)candidateBaud, (int)p.rx, (int)p.tx);
    #else
        debugLogGps("[gps] probing baud=%lu (rx=%d tx=%d)\n",
                    (unsigned long)candidateBaud, (int)p.rx, (int)p.tx);
    #endif
        return true;
    }
    return false;
}

void gpsBegin() {
#if defined(DEVICE_TLORA_PAGER_TFT)
    _pagerRailInverted = false;
    _pagerRailRetried = false;
    (void)pagerPrimeGpsRails(_pagerRailInverted);
#endif
    _baudProbeIdx  = 0;
    _portProbeIdx  = 0;
    _activeRx      = GPS_PORT_PROBE_LIST[0].rx;
    _activeTx      = GPS_PORT_PROBE_LIST[0].tx;
    _activeBaud    = GPS_BAUD;
    gpsApplyPortAndBaud(_activeRx, _activeTx, _activeBaud);
    _enabled       = true;
    _startMs       = millis();
    _firstFixMs    = 0;
    _prevSentences = 0;
    _totalBytes    = 0;
    _lastSats      = 0;
    _lastSatsMs    = 0;
    _lastProbeMs   = _startMs;
    _lastByteMs    = _startMs;
    _lastChecksumMs = _startMs;
    _lastPassedChecksum = _gps.passedChecksum();
    _nmeaSeen      = false;
#if defined(DEVICE_TLORA_PAGER_TFT)
    Serial.printf("[gps] started on UART1 baud=%lu rx=%d tx=%d\n",
                  (unsigned long)_activeBaud, (int)_activeRx, (int)_activeTx);
#else
    debugLogGps("[gps] started on UART1 baud=%lu rx=%d tx=%d\n",
                (unsigned long)_activeBaud, (int)_activeRx, (int)_activeTx);
#endif
}

void gpsEnd() {
    _serial.end();
    _enabled = false;
    debugLogGps("[gps] stopped\n");
}

void gpsLoop() {
    if (!_enabled) return;
    static uint32_t _lastDbg = 0;
    uint32_t now = millis();

    bool sawBytes = false;
    while (_serial.available()) {
        char c = (char)_serial.read();
        _gps.encode(c);
        _totalBytes++;
        sawBytes = true;
    }
    if (sawBytes) {
        _lastByteMs = now;
    }

    uint32_t passed = _gps.passedChecksum();
    if (passed != _lastPassedChecksum) {
        _lastPassedChecksum = passed;
        _lastChecksumMs = now;
    }

    if (!_nmeaSeen && passed >= GPS_MIN_CHECKSUM_FOR_STREAM) {
        _nmeaSeen = true;
#if defined(DEVICE_TLORA_PAGER_TFT)
        Serial.printf("[gps] valid NMEA stream detected at baud=%lu\n", (unsigned long)_activeBaud);
#else
        debugLogGps("[gps] valid NMEA stream detected at baud=%lu\n", (unsigned long)_activeBaud);
#endif
    }

    // Boards can occasionally latch onto a noisy UART config that yields
    // sporadic checksum passes once, then no real sentence progress. Re-probe.
    if (_nmeaSeen
        && !gpsHasFix()
        && passed > 0
        && (now - _lastChecksumMs) >= GPS_CHECKSUM_STALE_REPROBE_MS
        && (now - _lastProbeMs) >= GPS_BAUD_PROBE_INTERVAL_MS) {
#if defined(DEVICE_TLORA_PAGER_TFT)
        Serial.printf("[gps] checksum stale (%lums) without fix on baud=%lu rx=%d tx=%d, probing next\n",
                      (unsigned long)(now - _lastChecksumMs),
                      (unsigned long)_activeBaud, (int)_activeRx, (int)_activeTx);
#else
        debugLogGps("[gps] checksum stale (%lums) without fix on baud=%lu rx=%d tx=%d, probing next\n",
                    (unsigned long)(now - _lastChecksumMs),
                    (unsigned long)_activeBaud, (int)_activeRx, (int)_activeTx);
#endif
        _nmeaSeen = false;
        _lastProbeMs = now;
        gpsNextProbeConfig();
    }

    if (!_nmeaSeen
        && (now - _startMs) >= GPS_BAUD_PROBE_START_MS
        && (now - _lastProbeMs) >= GPS_BAUD_PROBE_INTERVAL_MS) {
        _lastProbeMs = now;
#if defined(DEVICE_TLORA_PAGER_TFT)
        if (!_pagerRailRetried) {
            _pagerRailRetried = true;
            _pagerRailInverted = true;
            (void)pagerPrimeGpsRails(_pagerRailInverted);
            gpsApplyPortAndBaud(_activeRx, _activeTx, _activeBaud);
            Serial.println("[gps] no stream yet; retried pager rails with invert=1");
            return;
        }
#endif
        gpsNextProbeConfig();
    }

    if (_nmeaSeen && (now - _lastByteMs) >= GPS_STREAM_STALL_MS) {
        debugLogGps("[gps] stream stalled for %lums, restarting UART1 at baud=%lu\n",
                    (unsigned long)(now - _lastByteMs), (unsigned long)_activeBaud);
        gpsApplyPortAndBaud(_activeRx, _activeTx, _activeBaud);
        _lastByteMs = now;
    }

    // Detect first real fix: only after warmup to ignore stale hot-start data
    if (_firstFixMs == 0
        && (now - _startMs) >= GPS_WARMUP_MS
        && _gps.location.isValid()
        && _gps.location.age() < 5000) {
        _firstFixMs = now;
        debugLogGps("[gps] first fix after %lums sats=%d\n",
                    (unsigned long)(_firstFixMs - _startMs),
                    _gps.satellites.isValid() ? (int)_gps.satellites.value() : 0);
    }

    if (debugGpsEnabled() && (now - _lastDbg >= 5000)) {
        _lastDbg = now;
        uint32_t sf = _gps.sentencesWithFix();
        debugLogGps("[gps] sats=%d fix=%d q=%c sf=%lu(+%lu) age=%lums hdop=%.1f pos=%.6f,%.6f bytes=%lu\n",
                    _gps.satellites.isValid() ? (int)_gps.satellites.value() : -1,
                    (int)_gps.location.isValid(),
                    _gps.location.isValid() ? (char)_gps.location.FixQuality() : '?',
                    (unsigned long)sf,
                    (unsigned long)(sf - _prevSentences),
                    _gps.location.isValid() ? (unsigned long)_gps.location.age() : 0UL,
                    _gps.hdop.isValid() ? _gps.hdop.hdop() : 99.9,
                    _gps.location.lat(), _gps.location.lng(),
                    (unsigned long)_totalBytes);
        _prevSentences = sf;
    }
}

void gpsSetEnabled(bool en) {
    if (en && !_enabled) gpsBegin();
    else if (!en && _enabled) gpsEnd();
}

bool gpsIsEnabled() { return _enabled; }

bool gpsHasFix() {
    if (!_enabled) return false;
    // Ignore everything during the warm-up window after start/restart
    if (millis() - _startMs < GPS_WARMUP_MS) return false;
    if (!_gps.location.isValid()) return false;
    if (_gps.location.age() > 5000) return false;
    // HDOP sanity check: the L76K hot-start cache often reports high HDOP
    // (or stale HDOP) with a fake quality > 0.  Real outdoor fixes with
    // 8+ sats have HDOP < 3; indoor/marginal < 10.  Reject > 20.
    if (_gps.hdop.isValid() && _gps.hdop.hdop() > 20.0) return false;
    return true;
}

bool gpsHasNmeaStream() {
    return _enabled && _nmeaSeen;
}

int32_t gpsLatI() {
    return (int32_t)(_gps.location.lat() * 1e7);
}

int32_t gpsLonI() {
    return (int32_t)(_gps.location.lng() * 1e7);
}

int32_t gpsAltM() {
    return _gps.altitude.isValid() ? (int32_t)_gps.altitude.meters() : 0;
}

uint8_t gpsSats() {
    if (!_enabled) return 0;
    // Don't report stale cached sat count during warmup
    if (millis() - _startMs < GPS_WARMUP_MS) return 0;

    uint32_t now = millis();

    // Prefer fresh GSA-based "satellites used" when available.
    bool gngsaFresh = false;
    uint8_t gngsaUsed = gsaSatsUsed(_gngsaSats, gngsaFresh);

    bool gpgsaFresh = false;
    uint8_t gpgsaUsed = gsaSatsUsed(_gpgsaSats, gpgsaFresh);

    bool hasFresh = false;
    uint8_t sats = 0;
    if (gngsaFresh || gpgsaFresh) {
        // GN and GP can alternate; use the best fresh reading.
        hasFresh = true;
        sats = max(gngsaUsed, gpgsaUsed);
    }

    // Fallback to direct GGA satellites-used fields (GN/GP talkers).
    if (!hasFresh) {
        bool gnggaFresh = false;
        bool gpggaFresh = false;
        uint8_t gngga = parseCustomU8(_gnggaSatsUsed, gnggaFresh);
        uint8_t gpgga = parseCustomU8(_gpggaSatsUsed, gpggaFresh);
        if (gnggaFresh || gpggaFresh) {
            hasFresh = true;
            sats = max(gngga, gpgga);
        }
    }

    // Legacy TinyGPS++ satellite value (typically from GPGGA).
    if (!hasFresh && _gps.satellites.isValid() && _gps.satellites.age() < GPS_SATS_MAX_AGE_MS) {
        hasFresh = true;
        sats = (uint8_t)_gps.satellites.value();
    }

    // Some modules report satellites in view via GSV before GSA/GGA stabilize.
    if (!hasFresh) {
        bool gngsvFresh = false;
        bool gpgsvFresh = false;
        bool glgsvFresh = false;
        bool gagsvFresh = false;
        bool bdgsvFresh = false;
        bool gbgsvFresh = false;
        bool gqgsvFresh = false;
        bool qzgsvFresh = false;
        uint8_t gngsv = parseCustomU8(_gngsvSatsView, gngsvFresh);
        uint8_t gpgsv = parseCustomU8(_gpgsvSatsView, gpgsvFresh);
        uint8_t glgsv = parseCustomU8(_glgsvSatsView, glgsvFresh);
        uint8_t gagsv = parseCustomU8(_gagsvSatsView, gagsvFresh);
        uint8_t bdgsv = parseCustomU8(_bdgsvSatsView, bdgsvFresh);
        uint8_t gbgsv = parseCustomU8(_gbgsvSatsView, gbgsvFresh);
        uint8_t gqgsv = parseCustomU8(_gqgsvSatsView, gqgsvFresh);
        uint8_t qzgsv = parseCustomU8(_qzgsvSatsView, qzgsvFresh);
        if (gngsvFresh || gpgsvFresh || glgsvFresh || gagsvFresh
            || bdgsvFresh || gbgsvFresh || gqgsvFresh || qzgsvFresh) {
            hasFresh = true;
            sats = gngsv;
            sats = max(sats, gpgsv);
            sats = max(sats, glgsv);
            sats = max(sats, gagsv);
            sats = max(sats, bdgsv);
            sats = max(sats, gbgsv);
            sats = max(sats, gqgsv);
            sats = max(sats, qzgsv);
        }
    }

    if (hasFresh) {
        if (sats > 0) {
            _lastSats = sats;
            _lastSatsMs = now;
            return sats;
        }
        // A transient zero can appear between sentence updates; smooth it while fix is valid.
        if (_nmeaSeen && _lastSats > 0 && (now - _lastSatsMs) < GPS_SATS_HOLD_MS)
            return _lastSats;
        _lastSats = 0;
        _lastSatsMs = now;
        return 0;
    }

    // No fresh sat sentence right now; keep the last valid count briefly.
    if (_nmeaSeen && _lastSats > 0 && (now - _lastSatsMs) < GPS_SATS_HOLD_MS)
        return _lastSats;

    _lastSats = 0;
    _lastSatsMs = now;

    return 0;
}

uint32_t gpsFixAgeMs() {
    return _firstFixMs ? (millis() - _firstFixMs) : 0;
}

uint32_t gpsSearchTimeMs() {
    if (!_enabled) return 0;
    if (_firstFixMs) return _firstFixMs - _startMs;
    return millis() - _startMs;
}

float gpsCourse() {
    return _gps.course.isValid() ? (float)_gps.course.deg() : 0.0f;
}

float gpsSpeedKmh() {
    return _gps.speed.isValid() ? (float)_gps.speed.kmph() : 0.0f;
}

bool gpsUtcDateTime(int &year, int &month, int &day,
                    int &hour, int &minute, int &second) {
    if (!gpsHasFix()) return false;
    if (!_gps.date.isValid() || !_gps.time.isValid()) return false;
    if (_gps.date.age() > 5000 || _gps.time.age() > 5000) return false;

    year   = _gps.date.year();
    month  = _gps.date.month();
    day    = _gps.date.day();
    hour   = _gps.time.hour();
    minute = _gps.time.minute();
    second = _gps.time.second();
    return true;
}
