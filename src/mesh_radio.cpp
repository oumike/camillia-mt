#include "mesh_radio.h"
#include <SPI.h>
#include <Wire.h>

#if defined(DEVICE_CARDPUTER_LORA_HAT)
#  include <M5Unified.h>
#  include <utility/PI4IOE5V6408_Class.hpp>
#endif

#if defined(DEVICE_TLORA_PAGER_TFT)
#  include "hal/xl9555.h"
namespace {

// Arm all T-LoRa Pager peripheral rails via the XL9555 GPIO expander so the
// SX1262 (and other peripherals) see a clean power-on sequence.
// invertDirSense=true is tried on a retry if the first attempt fails,
// as some board revisions have opposite direction-register polarity.
static bool pagerPrimeLoRaRail(bool invertDirSense) {
    Wire.begin(KB_SDA, KB_SCL);
    int expAddr = xl9555FindAddr();
    if (expAddr < 0) {
        Serial.println("[radio] pager expander not found (0x20-0x27)");
        return false;
    }

    uint8_t out0 = 0xFF, out1 = 0xFF, cfg0 = 0xFF, cfg1 = 0xFF;
    (void)xl9555ReadAll((uint8_t)expAddr, out0, out1, cfg0, cfg1);

    if (invertDirSense) {
        // Inverted polarity: configure all rails as inputs, which on these
        // units has the effect of enabling the peripheral power switches.
        //
        // Unlike the standard branch below, DRV_EN and NFC_EN are still enabled
        // here. Disabling them would mean driving the pin as an output at a
        // level this path has never established, and this branch only runs on
        // revisions we have no way to test against — a wrong guess costs a
        // board that will not bring its LoRa rail up at all. The saving is not
        // worth that risk on the fallback path.
        xl9555SetInput(XL9555_PIN_DRV_EN,    cfg0, cfg1);
        xl9555SetInput(XL9555_PIN_AMP_EN,    cfg0, cfg1);
        xl9555SetInput(XL9555_PIN_KB_RST,    cfg0, cfg1);
        xl9555SetInput(XL9555_PIN_LORA_EN,   cfg0, cfg1);
        xl9555SetInput(XL9555_PIN_GPS_EN,    cfg0, cfg1);
        xl9555SetInput(XL9555_PIN_NFC_EN,    cfg0, cfg1);
        xl9555SetInput(XL9555_PIN_GPS_RST,   cfg0, cfg1);
        xl9555SetInput(XL9555_PIN_KB_EN,     cfg0, cfg1);
        xl9555SetInput(XL9555_PIN_GPIO_EN,   cfg0, cfg1);
        xl9555SetInput(XL9555_PIN_SD_EN,     cfg0, cfg1);
        xl9555SetInput(XL9555_PIN_SD_PULLEN, cfg0, cfg1);
    } else {
        // Standard polarity: drive the rails we actually use high, SD pull-down
        // as input.
        //
        // DRV_EN (haptic driver) and NFC_EN are deliberately left OFF: there is
        // no haptic or NFC code anywhere in this firmware, so powering them was
        // energising silicon we never address, for the entire uptime. Same
        // reasoning as AMP_EN below, which has always been handled this way.
        // If haptics or NFC are added later, power them at point of use rather
        // than restoring a blanket enable here.
        xl9555SetOutput(XL9555_PIN_DRV_EN,  false, out0, out1, cfg0, cfg1);
        // Speaker amp stays OFF at boot; the audio layer powers it only while a
        // sound plays, so an idle amp can't emit random clicks (e.g. from LoRa
        // TX current transients coupling into the always-on amp).
        xl9555SetOutput(XL9555_PIN_AMP_EN,  false, out0, out1, cfg0, cfg1);
        xl9555SetOutput(XL9555_PIN_KB_RST,  true, out0, out1, cfg0, cfg1);
        xl9555SetOutput(XL9555_PIN_LORA_EN, true, out0, out1, cfg0, cfg1);
        xl9555SetOutput(XL9555_PIN_GPS_EN,  true, out0, out1, cfg0, cfg1);
        xl9555SetOutput(XL9555_PIN_NFC_EN,  false, out0, out1, cfg0, cfg1);
        xl9555SetOutput(XL9555_PIN_GPS_RST, true, out0, out1, cfg0, cfg1);
        xl9555SetOutput(XL9555_PIN_KB_EN,   true, out0, out1, cfg0, cfg1);
        xl9555SetOutput(XL9555_PIN_GPIO_EN, true, out0, out1, cfg0, cfg1);
        xl9555SetOutput(XL9555_PIN_SD_EN,   true, out0, out1, cfg0, cfg1);
        xl9555SetInput(XL9555_PIN_SD_PULLEN, cfg0, cfg1);
    }

    bool ok = xl9555WriteAll((uint8_t)expAddr, out0, out1, cfg0, cfg1);
    if (!ok) {
        Serial.printf("[radio] pager expander write failed addr=0x%02X invert=%d\n",
                      expAddr, invertDirSense ? 1 : 0);
        return false;
    }

    Serial.printf("[radio] pager expander armed addr=0x%02X invert=%d\n",
                  expAddr, invertDirSense ? 1 : 0);
    delay(12);  // Allow rails to stabilize before SX1262 probe
    return true;
}
} // namespace
#endif

volatile bool MeshRadio::_rxFlag = false;
MeshRadio Radio;
static constexpr bool kVerboseRadioIo = false;
static uint32_t sLastRxDoneMs = 0;

// ── Airtime accounting (rolling 1-hour window, one bucket per minute) ─────────
// Feeds DeviceMetrics channel_utilization / air_util_tx telemetry. busyMs counts
// both our transmissions and packets we heard; txMs counts only our TX.
namespace {
struct AirBucket { uint32_t minute; uint32_t txMs; uint32_t busyMs; };
constexpr int kAirBuckets = 60;
AirBucket sAir[kAirBuckets] = {};

void airNote(uint32_t toaMs, bool isTx) {
    if (toaMs == 0) return;
    uint32_t m = millis() / 60000UL;
    AirBucket &b = sAir[m % kAirBuckets];
    if (b.minute != m) { b.minute = m; b.txMs = 0; b.busyMs = 0; }
    b.busyMs += toaMs;
    if (isTx) b.txMs += toaMs;
}

float airPct(bool txOnly) {
    uint32_t m = millis() / 60000UL;
    uint64_t sum = 0;
    for (int i = 0; i < kAirBuckets; i++) {
        // Only count buckets that fall inside the last hour.
        if ((uint32_t)(m - sAir[i].minute) < (uint32_t)kAirBuckets)
            sum += txOnly ? sAir[i].txMs : sAir[i].busyMs;
    }
    float pct = (float)((double)sum * 100.0 / 3600000.0);  // window = 1 hour in ms
    return (pct > 100.0f) ? 100.0f : pct;
}
} // namespace

void IRAM_ATTR MeshRadio::_onDio1() { _rxFlag = true; }

// Arms receive. On LR11x0 the chip's "largest payload the receiver will accept"
// has to be put back to the maximum first — RadioLib leaves it set to the length
// of whatever this node last transmitted, which makes the radio deaf to anything
// longer than its own last packet. See MESH_LR11XX_RESTORE_RX_MAX_PAYLOAD in
// mesh_radio.h for the full story, and issue #43 for what it looked like from
// the outside.
int MeshRadio::_armRx() {
#if MESH_RADIO_IS_LR11XX
    // Standby first: SetPacketParams is a configuration command, and the chip is
    // still sitting in continuous RX at most of the call sites below (a received
    // packet does not leave RX). Re-entering RX is what startReceive() does next
    // anyway, so this costs a mode transition and nothing else.
    _radio.standby();
    const int16_t state = _radio.restoreRxMaxPayload();
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[radio] restoreRxMaxPayload failed: %d\n", (int)state);
    }
#endif
    return _radio.startReceive();
}

// ── RX health counters ───────────────────────────────────────────────────────
// "The M9 misses messages" is three different bugs depending on where the packet
// is lost, and until now the firmware threw away the evidence that tells them
// apart: pollRx() swallowed a bad length and a CRC failure into the same silent
// `return false`. These count what the radio actually reported, so a session can
// answer whether packets are arriving damaged (RF: switch, gain, clock), not
// arriving at all (RF: sensitivity, sync), or arriving fine and being lost above
// the radio (crypto, dedup, ignore list). See issue #43.
namespace {
struct RxHealth {
    uint32_t irqs;        // DIO1 assertions serviced
    uint32_t good;        // packets handed up
    uint32_t crcErr;      // arrived, failed CRC — heard but damaged
    uint32_t badLen;      // IRQ with a length no packet can have
    uint32_t readErr;     // the buffer read itself failed
    uint32_t wrapped;     // LR11x0 only: packets whose tail wrapped the RX ring
};
RxHealth sRx = {};
} // namespace

void MeshRadio::logRxHealth(const char *why) {
    Serial.printf("[radio] rx health (%s): irq=%lu ok=%lu crcErr=%lu badLen=%lu readErr=%lu"
#if MESH_RADIO_IS_LR11XX
                  " wrapped=%lu"
#endif
                  "\n",
                  why ? why : "-",
                  (unsigned long)sRx.irqs, (unsigned long)sRx.good,
                  (unsigned long)sRx.crcErr, (unsigned long)sRx.badLen,
                  (unsigned long)sRx.readErr
#if MESH_RADIO_IS_LR11XX
                  , (unsigned long)sRx.wrapped
#endif
                  );
}

#if MESH_RADIO_IS_LR11XX
// The chip's own account of itself: firmware revision (preproduction units run
// pre-0x0308, which is what tools/patch_radiolib_lr11x0.py exists for) and the
// error register, which is the one place a failed calibration is recorded. A
// bad image or PLL calibration costs real sensitivity and is otherwise
// invisible — the radio comes up, answers SPI, and quietly hears less.
void MeshRadio::logLr11xxHealth(const char *why) {
    LR11x0VersionInfo_t info = {};
    if (_radio.getVersionInfo(&info) != RADIOLIB_ERR_NONE) {
        Serial.printf("[radio] LR11x0 (%s): getVersionInfo failed - chip not answering\n",
                      why ? why : "-");
        return;
    }
    uint16_t errors = 0;
    (void)_radio.getErrors(&errors);
    Serial.printf("[radio] LR11x0 (%s): hw=0x%02X device=0x%02X fw=%u.%u errors=0x%04X%s\n",
                  why ? why : "-", info.hardware, info.device,
                  (unsigned)info.fwMajor, (unsigned)info.fwMinor, errors,
                  errors ? "  <-- see decode below" : "");
    if (!errors) return;

    // Decoded rather than left as a hex word: every one of these bits means a
    // different thing is wrong, and only some of them cost sensitivity.
    if (errors & RADIOLIB_LR11X0_ERROR_STAT_LF_RC_CALIB_ERR)  Serial.println("[radio]   LF_RC_CALIB_ERR");
    if (errors & RADIOLIB_LR11X0_ERROR_STAT_HF_RC_CALIB_ERR)  Serial.println("[radio]   HF_RC_CALIB_ERR");
    if (errors & RADIOLIB_LR11X0_ERROR_STAT_ADC_CALIB_ERR)    Serial.println("[radio]   ADC_CALIB_ERR");
    if (errors & RADIOLIB_LR11X0_ERROR_STAT_PLL_CALIB_ERR)    Serial.println("[radio]   PLL_CALIB_ERR");
    if (errors & RADIOLIB_LR11X0_ERROR_STAT_IMG_CALIB_ERR)    Serial.println("[radio]   IMG_CALIB_ERR (costs RX sensitivity)");
    if (errors & RADIOLIB_LR11X0_ERROR_STAT_HF_XOSC_START_ERR) Serial.println("[radio]   HF_XOSC_START_ERR (clock/TCXO config)");
    if (errors & RADIOLIB_LR11X0_ERROR_STAT_LF_XOSC_START_ERR) Serial.println("[radio]   LF_XOSC_START_ERR");
    if (errors & RADIOLIB_LR11X0_ERROR_STAT_PLL_LOCK_ERR)     Serial.println("[radio]   PLL_LOCK_ERR (frequency not reached)");
}
#endif

// Boosted-gain RX is NOT an SX126x-only feature, however the guard here used to
// read: the LR11x0 has its own SetRxBoosted command (0x0227) and RadioLib
// exposes it under the same setRxBoostedGainMode() name. Wrapping this in
// !MESH_RADIO_IS_LR11XX left the M9 with a config toggle that moved a field and
// nothing else — the radio kept the chip default while "[radio] ready ...
// rxBoost=1" printed — which is the last thing you want on the one board in the
// fleet that is short on sensitivity. See issue #43.
void MeshRadio::setRxBoostedGain(bool enabled) {
    _rxBoostedGain = enabled;
    if (_ready) {
        _radio.setRxBoostedGainMode(enabled);
        _armRx();   // re-arm so the new gain setting takes effect
    }
}

bool MeshRadio::init(uint8_t txPower, bool rxBoostedGain) {
    _rxBoostedGain = rxBoostedGain;
#if defined(DEVICE_TLORA_PAGER_TFT)
    (void)pagerPrimeLoRaRail(false);
#endif

#if defined(LORA_POWER_ENABLE_PIN) && (LORA_POWER_ENABLE_PIN >= 0)
    pinMode(LORA_POWER_ENABLE_PIN, OUTPUT);
    digitalWrite(LORA_POWER_ENABLE_PIN, LORA_POWER_ENABLE_LEVEL);
    delay(10);
#endif

    SPI.begin(LORA_SPI_SCK, LORA_SPI_MISO, LORA_SPI_MOSI);

    Serial.printf("[radio] pins: sck=%d miso=%d mosi=%d cs=%d dio1=%d rst=%d busy=%d\n",
                  LORA_SPI_SCK, LORA_SPI_MISO, LORA_SPI_MOSI,
                  LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

#if defined(DEVICE_CARDPUTER_LORA_HAT)
    m5::PI4IOE5V6408_Class ioexp(0x43, 400000, &m5::In_I2C);
    if (ioexp.begin()) {
        ioexp.setDirection(0, true);
        ioexp.setHighImpedance(0, false);
        ioexp.digitalWrite(0, true);
        Serial.println("[radio] enabled Cardputer Cap LoRa-1262 IO expander");
    }
#endif

#if (LORA_FEM_POWER_PIN >= 0)
    pinMode(LORA_FEM_POWER_PIN, OUTPUT);
    digitalWrite(LORA_FEM_POWER_PIN, HIGH);
#endif
#if (LORA_FEM_ENABLE_PIN >= 0)
    pinMode(LORA_FEM_ENABLE_PIN, OUTPUT);
    digitalWrite(LORA_FEM_ENABLE_PIN, HIGH);
#endif
#if (LORA_FEM_TX_MODE_PIN >= 0)
    pinMode(LORA_FEM_TX_MODE_PIN, OUTPUT);
    digitalWrite(LORA_FEM_TX_MODE_PIN, HIGH);
#endif

    _radio.reset();
    int state = _radio.begin(MESH_FREQ, MESH_BW, MESH_SF, MESH_CR,
                             MESH_SYNC, txPower, MESH_PREAMBLE,
                             MESH_TCXO_V);
#if defined(DEVICE_TLORA_PAGER_TFT)
    if (state == RADIOLIB_ERR_CHIP_NOT_FOUND) {
        Serial.println("[radio] retrying init with alternate pager expander polarity");
        if (pagerPrimeLoRaRail(true)) {
            state = _radio.begin(MESH_FREQ, MESH_BW, MESH_SF, MESH_CR,
                                 MESH_SYNC, txPower, MESH_PREAMBLE,
                                 MESH_TCXO_V);
        }
    }
#endif
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[radio] init failed: %d\n", state);
        if (state == RADIOLIB_ERR_CHIP_NOT_FOUND) {
            Serial.println("[radio] hint: LoRa chip not detected. Check build target, pin mapping, and power gating.");
#if defined(DEVICE_TLORA_PAGER_TFT)
            Serial.println("[radio] target=tlora-pager-tft (expected pins: CS36 DIO1=14 RST47 BUSY48)");
            Serial.printf("[radio] target=tlora-pager-tft (radio=%s)\n", PAGER_LORA_USE_LR1121 ? "LR1121" : "SX1262");
            Serial.println("[radio] target=tlora-pager-tft (verify XL9555 LoRa rail: EXPANDS_LORA_EN)");
#elif defined(DEVICE_TDECK)
            Serial.println("[radio] target=tdeck (expected pins: CS9 DIO1=45 RST17 BUSY13)");
#elif defined(DEVICE_TDECK_PRO)
            Serial.println("[radio] target=tdeck-pro (expected pins: CS3 DIO1=5 RST4 BUSY6 EN46)");
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
            Serial.println("[radio] target=cardputer-cap (board IO expander must be enabled before SX1262 init)");
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
            Serial.println("[radio] target=heltec-v4 (verify board power and radio wiring)");
#elif defined(DEVICE_WIO_TRACKER_L2)
            Serial.printf("[radio] target=wio-tracker-l2 (expected pins: CS%d DIO1=%d RST%d BUSY%d)\n",
                          LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
#endif
        }
        return false;
    }

#if !MESH_RADIO_IS_LR11XX
    _radio.setDio2AsRfSwitch(true);
#endif

#if MESH_RADIO_IS_LR11XX && defined(MESH_LR11XX_RFSWITCH_DIO56)
    // The LR11x0 equivalent of the line above, and it has to come after begin():
    // setRfSwitchTable() does not stage anything, it sends SetDioAsRfSwitch
    // straight to the chip. Skipping it leaves the antenna switch wherever it
    // powered up, which costs receive sensitivity rather than announcing itself.
    // See the truth table (and the warning attached to it) in the board header.
    {
        static const uint32_t kRfSwitchPins[Module::RFSWITCH_MAX_PINS] = {
            RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6,
            RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC
        };
        static const Module::RfSwitchMode_t kRfSwitchTable[] = {
            { LR11x0::MODE_STBY,  { LOW,  LOW  } },
            { LR11x0::MODE_RX,    { HIGH, LOW  } },
            { LR11x0::MODE_TX,    { HIGH, HIGH } },
            { LR11x0::MODE_TX_HP, { LOW,  HIGH } },
            { LR11x0::MODE_TX_HF, { LOW,  LOW  } },
            { LR11x0::MODE_GNSS,  { LOW,  LOW  } },
            { LR11x0::MODE_WIFI,  { LOW,  LOW  } },
            END_OF_MODE_TABLE,
        };
        _radio.setRfSwitchTable(kRfSwitchPins, kRfSwitchTable);
        Serial.println("[radio] LR11x0 RF switch: DIO5/DIO6 configured");
    }
#endif
    // Explicitly apply the PA; the begin() parameter alone may not stick. This
    // used to be a hardcoded 22, which silently overrode the configured power
    // whenever setup()'s "did anything differ from the compile-time default?"
    // check decided not to call reconfigure().
    _radio.setOutputPower(txPower);
#if !MESH_RADIO_IS_LR11XX
    _radio.setCurrentLimit(140.0);   // SX1262 HP PA max; default OCP may be too low
#endif
    // Both families have this; only the SX126x branch above is chip-specific.
    const int boostState = _radio.setRxBoostedGainMode(_rxBoostedGain);
#if MESH_RADIO_IS_LR11XX
    _radio.setIrqAction(_onDio1);
#else
    _radio.setDio1Action(_onDio1);
#endif
    _armRx();

    _ready = true;
    // rxBoost prints what the chip accepted, not what was asked for. The old
    // line printed the request, which is how a setting that was never being
    // applied on this board sat unnoticed (issue #43).
    Serial.printf("[radio] ready  %.3f MHz  SF%d  BW%.0f  CR4/%d  %ddBm  rxBoost=%s\n",
                  MESH_FREQ, MESH_SF, MESH_BW, MESH_CR, (int)txPower,
                  (boostState != RADIOLIB_ERR_NONE) ? "FAILED"
                                                    : (_rxBoostedGain ? "1" : "0"));
    if (boostState != RADIOLIB_ERR_NONE) {
        Serial.printf("[radio] setRxBoostedGainMode failed: %d\n", boostState);
    }
#if MESH_RADIO_IS_LR11XX
    logLr11xxHealth("init");
#endif
    return true;
}

bool MeshRadio::reconfigure(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t power) {
    if (!_ready) return false;
    bool ok = true;
    int state = _radio.standby();
    if (state != RADIOLIB_ERR_NONE) ok = false;
    state = _radio.setFrequency(freq);
    if (state != RADIOLIB_ERR_NONE) ok = false;
    state = _radio.setSpreadingFactor(sf);
    if (state != RADIOLIB_ERR_NONE) ok = false;
    state = _radio.setBandwidth(bw);
    if (state != RADIOLIB_ERR_NONE) ok = false;
    state = _radio.setCodingRate(cr);
    if (state != RADIOLIB_ERR_NONE) ok = false;
    state = _radio.setOutputPower(power);
    if (state != RADIOLIB_ERR_NONE) ok = false;
    state = _armRx();
    if (state != RADIOLIB_ERR_NONE) ok = false;

    if (!ok) {
        Serial.printf("[radio] reconfigure failed  %.3f MHz  SF%d  BW%.0f  CR4/%d  %ddBm\n",
                      freq, sf, bw, cr, power);
        return false;
    }

    Serial.printf("[radio] reconfigured  %.3f MHz  SF%d  BW%.0f  CR4/%d  %ddBm\n",
                  freq, sf, bw, cr, power);
    return true;
}

#if MESH_RADIO_IS_LR11XX
// Reads one packet out of the LR11x0's RX buffer, wrapping correctly.
//
// THE BUG THIS EXISTS FOR. The LR11x0 hands out its 256-byte RX buffer as a
// ring: GetRxBufferStatus returns both the length and a *start offset*, and that
// offset advances a few bytes on every packet received (RadioLib's own comment
// in readData(): "the LR11x0 seems to move the buffer base by 4 bytes on every
// packet"). When a packet starts late enough that offset + length runs past 256,
// the chip wraps the tail around to the start of the buffer — but RadioLib reads
// it as one linear run of `length` bytes from `offset`, so everything past the
// end comes back as whatever is there instead of the packet's tail.
//
// The symptom is the one reported in issue #43, including the parts that made no
// sense for an RF fault:
//   • short packets survive, long ones do not — a packet fits iff
//     length <= 256 - offset, so the cutoff is a *length* threshold;
//   • the threshold drifts, because the offset creeps up ~4 bytes per packet and
//     wraps at 256. Right after a reboot the offset is 0 and everything works;
//   • it is not correlated with distance or SNR, and a node being heard fine one
//     minute loses its longer messages the next;
//   • the packet is not "weak" — it is received intact and then read wrong, so
//     nothing in the radio's own error reporting flags it.
//
// Reading in two parts across the wrap is what the ring layout has always
// required. When offset + length <= 256 (the only case that worked before) this
// is byte-for-byte the old behavior.
int MeshRadio::_readPacketLr11xx(uint8_t *buf, size_t bufCap, size_t &outLen) {
    outLen = 0;

    uint8_t offset = 0;
    const size_t len = _radio.getPacketLength(true, &offset);
    // One error for both ends of the range — the caller only needs "not a usable
    // packet", and outLen staying 0 is what tells it this was the length check
    // rather than the read below.
    if (len < sizeof(MeshHdr) || len > bufCap) return RADIOLIB_ERR_PACKET_TOO_LONG;

    // Same CRC verdict RadioLib's readData() reaches, taken before the read
    // because the read clears the flags. A header error only counts when no
    // valid header was seen, otherwise it is a leftover from the packet before.
    const uint32_t irq = _radio.getIrqFlags();
    const bool crcBad = (irq & RADIOLIB_LR11X0_IRQ_CRC_ERR)
                     || ((irq & RADIOLIB_LR11X0_IRQ_HEADER_ERR)
                         && !(irq & RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID));

    constexpr size_t kRxBufBytes = 256;   // the chip's RX buffer, and the ring modulus
    const size_t firstRun = (offset + len > kRxBufBytes) ? (kRxBufBytes - offset) : len;

    int state = _radio.readBuffer8(buf, firstRun, offset);
    if (state == RADIOLIB_ERR_NONE && firstRun < len) {
        // The tail wrapped to the base of the buffer.
        state = _radio.readBuffer8(buf + firstRun, len - firstRun, 0);
        // The first one always announces itself, verbose or not: this is the
        // packet that a build without this fix would have read as garbage and
        // dropped without a word, so seeing the line at all is the confirmation
        // that the ring wrap was what ate the long messages.
        if (sRx.wrapped == 0 || kVerboseRadioIo) {
            Serial.printf("[radio] LR11x0 RX wrap: offset=%u len=%u split=%u+%u"
                          " (before this fix, this packet was lost)\n",
                          (unsigned)offset, (unsigned)len,
                          (unsigned)firstRun, (unsigned)(len - firstRun));
            logRxHealth("first wrap");
        }
        sRx.wrapped++;
    }

    // Housekeeping readData() would otherwise have done for us.
    _radio.clearRxBuffer();
    _radio.clearIrqFlags(RADIOLIB_LR11X0_IRQ_ALL);

    if (state != RADIOLIB_ERR_NONE) return state;
    outLen = len;
    return crcBad ? RADIOLIB_ERR_CRC_MISMATCH : RADIOLIB_ERR_NONE;
}
#endif

bool MeshRadio::pollRx(MeshPacket &pkt) {
#if defined(LORA_TEST_MQTT_ONLY) && LORA_TEST_MQTT_ONLY
    (void)pkt;
    return false;   // TEMP: LoRa RX disabled for MQTT-only testing
#endif
    if (!_rxFlag) return false;
    _rxFlag = false;
    sRx.irqs++;

    uint8_t buf[256];
#if MESH_RADIO_IS_LR11XX
    size_t len = 0;
    const int readState = _readPacketLr11xx(buf, sizeof(buf), len);
    if (readState != RADIOLIB_ERR_NONE) {
        if (readState == RADIOLIB_ERR_CRC_MISMATCH) sRx.crcErr++;
        else if (len == 0)                          sRx.badLen++;
        else                                        sRx.readErr++;
        // standby() before re-arming, not startReceive() alone: on this family a
        // corrupted packet can leave the receiver shifted, and every packet after
        // it arrives damaged until the radio has been through standby. Ported
        // from MeshCore's LR1110 driver, which hit the same thing.
        _radio.standby();
        _armRx();
        return false;
    }
#else
    size_t len = _radio.getPacketLength();
    if (len < sizeof(MeshHdr) || len > 256) {
        sRx.badLen++;
        _armRx();
        return false;
    }

    if (_radio.readData(buf, len) != RADIOLIB_ERR_NONE) {
        sRx.readErr++;
        _armRx();
        return false;
    }
#endif
    sRx.good++;

    if (kVerboseRadioIo) {
        // Dump raw header bytes for wire-format verification
        Serial.printf("[radio] RX hdr: ");
        for (size_t i = 0; i < 16 && i < len; i++) Serial.printf("%02x ", buf[i]);
        Serial.println();
    }

    airNote((uint32_t)(_radio.getTimeOnAir(len) / 1000UL), false);  // heard packet → channel busy

    pkt.rssi  = _radio.getRSSI();
    pkt.snr   = _radio.getSNR();
    pkt.rxMs  = millis();
    memcpy(&pkt.hdr, buf, sizeof(MeshHdr));
    pkt.portnum = 0;
    pkt.requestId = 0;
    pkt.dataDest = 0;
    pkt.dataSource = 0;
    pkt.hasDataDest = false;
    pkt.hasDataSource = false;
    pkt.wantResponse = false;
    pkt.payloadLen = 0;
    pkt.decrypted = false;
    pkt.chanIdx = -1;
    pkt.rawLen = 0;

    // Decrypt and decode
    size_t payloadLen = len - sizeof(MeshHdr);
    const uint8_t *cipher = buf + sizeof(MeshHdr);

    if (payloadLen > 0) {
        // Preserve raw cipher for deferred PKI decrypt in handleRx
        pkt.rawLen = (payloadLen <= sizeof(pkt.rawCipher)) ? payloadLen : 0;
        if (pkt.rawLen) memcpy(pkt.rawCipher, cipher, payloadLen);

        // Channel hash 0 is reserved for PKI packets; let handleRx perform PKI
        // decrypt to avoid channel-key false positives on random ciphertext.
        if (pkt.hdr.channel != 0) {
            uint8_t plain[256];
            pkt.chanIdx = decryptPacket(pkt.hdr, cipher, plain, payloadLen);
            pkt.decrypted = (pkt.chanIdx >= 0);

            if (pkt.decrypted) {
                const uint8_t *payPtr; size_t payLen;
                decodeData(plain, payloadLen, pkt.portnum, payPtr, payLen,
                           pkt.requestId, pkt.wantResponse,
                           &pkt.dataDest, &pkt.hasDataDest,
                           &pkt.dataSource, &pkt.hasDataSource);
                if (payPtr && payLen <= sizeof(pkt.payload)) {
                    memcpy(pkt.payload, payPtr, payLen);
                    pkt.payloadLen = payLen;
                } else if (payPtr) {
                    // Leaving payloadLen at 0 makes an over-long payload vanish
                    // with nothing to show for it — the packet still decrypted,
                    // so it isn't logged as encrypted either. Say so instead.
                    Serial.printf("[radio] payload too long: port=%lu len=%u cap=%u (dropped)\n",
                                  (unsigned long)pkt.portnum,
                                  (unsigned)payLen,
                                  (unsigned)sizeof(pkt.payload));
                }
            }
        }
    }

    sLastRxDoneMs = millis();
    _armRx();
    return true;
}

bool MeshRadio::transmit(const uint8_t *buf, size_t len) {
#if defined(LORA_TEST_MQTT_ONLY) && LORA_TEST_MQTT_ONLY
    // TEMP: LoRa TX disabled for MQTT-only testing. Report success so senders
    // still run their MQTT uplink and local-display paths.
    (void)buf; (void)len;
    return true;
#endif
    if (kVerboseRadioIo) {
        // Dump header bytes for wire-format verification
        Serial.printf("[radio] TX hdr: ");
        for (size_t i = 0; i < 16 && i < len; i++) Serial.printf("%02x ", buf[i]);
        Serial.println();
    }

    // If RX IRQ is still pending (common during burst traffic), drain and re-arm
    // before attempting TX so the radio is not left in a stale RX_DONE state.
    if (_rxFlag) {
        size_t pendingLen = _radio.getPacketLength();
        if (pendingLen >= sizeof(MeshHdr) && pendingLen <= 256) {
            uint8_t dump[256];
            (void)_radio.readData(dump, pendingLen);
        }
        _rxFlag = false;
        _armRx();
        delay(2);
    }

    // Small settle window after RX improves turn-around reliability for quick replies.
    uint32_t nowMs = millis();
    uint32_t sinceRxMs = nowMs - sLastRxDoneMs;
    if (sinceRxMs < 90) {
        delay(90 - sinceRxMs);
    }

    static constexpr uint8_t kTxAttempts =
#if defined(DEVICE_TLORA_PAGER_TFT)
        6;
#else
        4;
#endif
    int state = RADIOLIB_ERR_UNKNOWN;
    for (uint8_t attempt = 0; attempt < kTxAttempts; attempt++) {
        _rxFlag = false;    // clear any stale DIO1 flag before TX
        (void)_radio.standby();
        state = _radio.transmit(const_cast<uint8_t*>(buf), len);
        if (state == RADIOLIB_ERR_NONE) break;

        if (attempt + 1 < kTxAttempts) {
            Serial.printf("[radio] TX retry %u state=%d\n", (unsigned)(attempt + 1), state);
            _armRx();
            uint16_t backoffMs =
#if defined(DEVICE_TLORA_PAGER_TFT)
                (uint16_t)(24 + (attempt * 28));
#else
                (uint16_t)(16 + (attempt * 20));
#endif
            delay(backoffMs);
        }
    }

    if (kVerboseRadioIo || state != RADIOLIB_ERR_NONE) {
        Serial.printf("[radio] TX state=%d (%s)\n", state,
                      state == RADIOLIB_ERR_NONE ? "OK" : "FAIL");
    }
    if (state == RADIOLIB_ERR_NONE) {
        airNote((uint32_t)(_radio.getTimeOnAir(len) / 1000UL), true);
    }
    _rxFlag = false;    // discard TX_DONE ISR trigger
    _armRx();
    return state == RADIOLIB_ERR_NONE;
}

float MeshRadio::channelUtilPercent() { return airPct(false); }
float MeshRadio::airUtilTxPercent()   { return airPct(true); }

void MeshRadio::setRxPaused(bool paused) {
    if (!_ready) return;
    _rxFlag = false;
    if (paused) {
        (void)_radio.standby();
    } else {
        (void)_armRx();
    }
}
