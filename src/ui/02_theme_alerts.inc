// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: colours, alert sounds and melodies, UI theme presets.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

static constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    return (uint16_t)(((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3));
}

static uint16_t blend565(uint16_t c1, uint16_t c2, uint8_t t);

static constexpr UiThemePresetLite kUiThemePresets[] = {
    {UI_THEME_CAMELLIA, UI_MODE_DARK,  0x0843, 0x1065, 0x18A7, 0xDA8E, "Camillia Dark"},
    {UI_THEME_CAMELLIA, UI_MODE_LIGHT,
        rgb565(0xff, 0xf7, 0xfa), rgb565(0xff, 0xfd, 0xfe), rgb565(0xf8, 0xee, 0xf3), rgb565(0xb0, 0x2f, 0x62),
        "Camillia Light"},
    {UI_THEME_EVERGREEN, UI_MODE_DARK,  0x00A8, 0x11AA, 0x1A2C, 0x55B0, "Evergreen Dark"},
    {UI_THEME_EVERGREEN, UI_MODE_LIGHT, 0xE73C, 0xF7DE, 0xE71B, 0x2D2A, "Evergreen Light"},
    {UI_THEME_EARTHEN, UI_MODE_DARK,  0x1082, 0x2104, 0x2945, 0xD38B, "Earthy Dark"},
    {UI_THEME_EARTHEN, UI_MODE_LIGHT, 0xF7DE, 0xFFDF, 0xF75C, 0xB40B, "Earthy Light"},
    {UI_THEME_SOLARIZED, UI_MODE_DARK,
        rgb565(0x00, 0x2b, 0x36), rgb565(0x07, 0x36, 0x42), rgb565(0x0c, 0x3c, 0x47), rgb565(0x2a, 0xa1, 0x98),
        "Solarized Dark"},
    {UI_THEME_SOLARIZED, UI_MODE_LIGHT,
        rgb565(0xee, 0xe8, 0xd5), rgb565(0xfd, 0xf6, 0xe3), rgb565(0xee, 0xe8, 0xd5), rgb565(0x2a, 0xa1, 0x98),
        "Solarized Light"},
    {UI_THEME_CRIMSON, UI_MODE_DARK,
        rgb565(0x06, 0x0f, 0x24), rgb565(0x12, 0x24, 0x4c), rgb565(0x1b, 0x33, 0x63), rgb565(0xff, 0x4a, 0x58),
        "Crimson Blue Dark"},
    {UI_THEME_CRIMSON, UI_MODE_LIGHT,
        rgb565(0xf3, 0xf7, 0xff), rgb565(0xf8, 0xfb, 0xff), rgb565(0xe6, 0xef, 0xff), rgb565(0xc6, 0x28, 0x39),
        "Crimson Blue Light"},
    {UI_THEME_SCARLET_POP, UI_MODE_DARK,
        rgb565(0x15, 0x00, 0x09), rgb565(0x76, 0x00, 0x31), rgb565(0x8b, 0x00, 0x38), rgb565(0xd5, 0x1c, 0x39),
        "Scarlet Pop Dark"},
    {UI_THEME_SCARLET_POP, UI_MODE_LIGHT,
        rgb565(0xff, 0xf2, 0xf4), rgb565(0xff, 0xf8, 0xf9), rgb565(0xff, 0xea, 0xed), rgb565(0xd5, 0x1c, 0x39),
        "Scarlet Pop Light"},
    {UI_THEME_INK_WASH, UI_MODE_DARK,
        rgb565(0x11, 0x13, 0x18), rgb565(0x1C, 0x21, 0x28), rgb565(0x25, 0x2B, 0x34), rgb565(0xD8, 0xDD, 0xE4),
        "Ink Wash Dark"},
    {UI_THEME_INK_WASH, UI_MODE_LIGHT,
        rgb565(0xF3, 0xF5, 0xF7), rgb565(0xFF, 0xFF, 0xFF), rgb565(0xE8, 0xEB, 0xEF), rgb565(0x2E, 0x34, 0x40),
        "Ink Wash Light"},
    {UI_THEME_LAVENDAR_FIELDS, UI_MODE_DARK,
        rgb565(0x1A, 0x12, 0x30), rgb565(0x25, 0x1A, 0x45), rgb565(0x2F, 0x22, 0x58), rgb565(0xB7, 0x9B, 0xFF),
        "Lavendar Fields Dark"},
    {UI_THEME_LAVENDAR_FIELDS, UI_MODE_LIGHT,
        rgb565(0xF5, 0xEF, 0xFB), rgb565(0xFF, 0xF9, 0xFF), rgb565(0xED, 0xE1, 0xF7), rgb565(0x7B, 0x5B, 0xA7),
        "Lavendar Fields Light"},
    {UI_THEME_WILD_FLOWERS, UI_MODE_DARK,
        rgb565(0x1A, 0x24, 0x30), rgb565(0x25, 0x35, 0x47), rgb565(0x2D, 0x45, 0x5B), rgb565(0xC7, 0x8F, 0xCF),
        "Wild Flowers Dark"},
    {UI_THEME_WILD_FLOWERS, UI_MODE_LIGHT,
        rgb565(0xF6, 0xFA, 0xF4), rgb565(0xFF, 0xFF, 0xFF), rgb565(0xE5, 0xF0, 0xE2), rgb565(0x8A, 0x5F, 0xAF),
        "Wild Flowers Light"},
    {UI_THEME_QUIET_LUXURY, UI_MODE_DARK,
        rgb565(0x2A, 0x1F, 0x17), rgb565(0x34, 0x27, 0x1E), rgb565(0x40, 0x31, 0x26), rgb565(0xD9, 0xC7, 0xA3),
        "Quiet Luxury Dark"},
    {UI_THEME_QUIET_LUXURY, UI_MODE_LIGHT,
        rgb565(0xFA, 0xF4, 0xEA), rgb565(0xFF, 0xFD, 0xF8), rgb565(0xF1, 0xE7, 0xD5), rgb565(0xA8, 0x84, 0x4F),
        "Quiet Luxury Light"},
    {UI_THEME_MORNING_DEW, UI_MODE_DARK,
        rgb565(0x12, 0x28, 0x2A), rgb565(0x1A, 0x36, 0x38), rgb565(0x23, 0x43, 0x45), rgb565(0x9C, 0xD8, 0xC8),
        "Morning Dew Dark"},
    {UI_THEME_MORNING_DEW, UI_MODE_LIGHT,
        rgb565(0xEE, 0xF9, 0xF6), rgb565(0xFF, 0xFF, 0xFF), rgb565(0xDD, 0xF1, 0xEC), rgb565(0x4E, 0x9C, 0x8A),
        "Morning Dew Light"},
    {UI_THEME_WINTER_CHILL, UI_MODE_DARK,
        rgb565(0x15, 0x1F, 0x2B), rgb565(0x1C, 0x2A, 0x3A), rgb565(0x24, 0x36, 0x49), rgb565(0x8F, 0xB3, 0xD9),
        "Winter Chill Dark"},
    {UI_THEME_WINTER_CHILL, UI_MODE_LIGHT,
        rgb565(0xF1, 0xF7, 0xFC), rgb565(0xFF, 0xFF, 0xFF), rgb565(0xDF, 0xEB, 0xF6), rgb565(0x5C, 0x86, 0xB2),
        "Winter Chill Light"},
    {UI_THEME_CAMELLIA_BLACK, UI_MODE_DARK,
        rgb565(0x00, 0x00, 0x00), rgb565(0x00, 0x00, 0x00), rgb565(0x0A, 0x0A, 0x0A), rgb565(0xFF, 0xFF, 0xFF),
        "Camillia Black"},
};

#if !HAS_UI_THEMES
static constexpr UiThemePresetLite kPaperTheme = {
    UI_THEME_CAMELLIA, UI_MODE_LIGHT,
    0xFFFF, 0xFFFF, 0xFFFF, 0x0000,
    "Camillia Paper",
};
#endif

static constexpr int kUiThemePresetCount =
    (int)(sizeof(kUiThemePresets) / sizeof(kUiThemePresets[0]));

// 16 basic colors offered when the user picks the color their own messages
// render in. Stored as an index (RhinoConfig::userMsgColor); 0xFF means "use
// the adaptive theme default" (classic yellow / light-mode amber).
struct UserMsgColorOption {
    uint16_t color;      // rgb565
    const char *name;
};

static constexpr UserMsgColorOption kUserMsgColors[] = {
    {rgb565(0xFF, 0xFF, 0xFF), TR_NOOP("White")},
    {rgb565(0xC0, 0xC0, 0xC0), TR_NOOP("Silver")},
    {rgb565(0xFF, 0x3B, 0x30), TR_NOOP("Red")},
    {rgb565(0xFF, 0x95, 0x00), TR_NOOP("Orange")},
    {rgb565(0xFF, 0xE0, 0x00), TR_NOOP("Yellow")},
    {rgb565(0x7C, 0xFC, 0x00), TR_NOOP("Lime")},
    {rgb565(0x34, 0xC7, 0x59), TR_NOOP("Green")},
    {rgb565(0x30, 0xD0, 0xC0), TR_NOOP("Teal")},
    {rgb565(0x00, 0xE5, 0xFF), TR_NOOP("Cyan")},
    {rgb565(0x5A, 0xC8, 0xFA), TR_NOOP("Sky")},
    {rgb565(0x3B, 0x82, 0xF6), TR_NOOP("Blue")},
    {rgb565(0x5E, 0x5C, 0xE6), TR_NOOP("Indigo")},
    {rgb565(0xAF, 0x52, 0xDE), TR_NOOP("Purple")},
    {rgb565(0xFF, 0x2D, 0x95), TR_NOOP("Magenta")},
    {rgb565(0xFF, 0x6F, 0xB5), TR_NOOP("Pink")},
    {rgb565(0xB5, 0x65, 0x1D), TR_NOOP("Brown")},
};

static constexpr int kUserMsgColorCount =
    (int)(sizeof(kUserMsgColors) / sizeof(kUserMsgColors[0]));

struct UiPalette {
    uint16_t bgMain;
    uint16_t statusTop;
    uint16_t statusBg;
    uint16_t panelBg;
    uint16_t panelAlt;
    uint16_t panelStrong;
    uint16_t tabActive;
    uint16_t tabUnread;
    uint16_t tabIdle;
    uint16_t divider;
    uint16_t dividerHi;
    uint16_t inputBg;
    uint16_t inputTop;
    uint16_t accent;
    uint16_t cursor;
    uint16_t textMain;
    uint16_t textDim;
    uint16_t textOnAccent;
    uint16_t statusText;
    uint16_t selectBg;
    uint16_t selectAccent;
    uint16_t nodeHot;
    uint16_t nodeWarm;
    uint16_t dmMuted;
    uint16_t battGood;
    uint16_t battWarn;
    uint16_t battBad;
    uint16_t splashTop;
    uint16_t splashBottom;
    uint16_t splashCardBg;
    uint16_t splashCardEdge;
    uint16_t splashCardEdgeHi;
    uint16_t splashTitle;
    uint16_t splashSub;
    uint16_t splashDim;
};

static UiPalette s_ui = {};
static uint8_t s_appliedUiTheme = 0xFF;
static uint8_t s_appliedUiMode = 0xFF;
// What the message views were last rendered at. The theme pair above is set by
// applyUiThemePalette(); this one has no equivalent hook, so it is updated
// wherever the font size is applied.
static uint8_t s_appliedFontSize = 0xFF;
#if HAS_NAV_BAR_TOGGLE
// What the screens on display were actually built with. The web form writes
// straight into s_cfg before the save callback runs, so s_cfg is no use for
// telling "changed" from "unchanged" there — this is, the same way
// s_appliedFontSize is for the font.
static bool s_appliedNavBar = false;
#endif
#if HAS_CHANNEL_PANEL_LOCK
// The channel panel lock the current screen was built with (onWebCfgSaved()).
static bool s_appliedChannelPanelLocked = true;
// Keyboard focus on the docked column: no touch on this board, so Left (or C)
// steps into the list, Up/Down move the cursor (s_cardputerDropdownSelection),
// Enter switches, Right/Esc hand the keys back to the chat.
static bool s_channelColumnFocus = false;
#endif
#if HAS_NAV_ALIGN_SETTING
// The nav alignment the current screen was built with, so a web config save
// can tell whether it moved (onWebCfgSaved()).
static NavAlign s_appliedNavAlign = NAV_ALIGN_CENTER;

// The word the Config row and the picker show for an alignment.
static const char *navAlignName(NavAlign a) {
    return a == NAV_ALIGN_LEFT ? TR("Left") : a == NAV_ALIGN_RIGHT ? TR("Right") : TR("Center");
}
#endif

static const char *msgAlertSoundName(uint8_t mode) {
    switch (mode) {
        case MSG_ALERT_SOUND_CHIRPY: return TR("Chirpy");
        case MSG_ALERT_SOUND_BASS:   return TR("Bass");
        case MSG_ALERT_SOUND_OFF:    return TR("Off");
        case MSG_ALERT_SOUND_DEFAULT:
        default:                     return TR("Default");
    }
}

#if HAS_HAPTIC
static const char *hapticStyleName(uint8_t style) {
    switch (style) {
        case 0:  return TR("Off");
        case 2:  return TR("Gentle");
        case 3:  return TR("Long");
        case 4:  return TR("Heartbeat");
        case 1:
        default: return TR("Pulse");
    }
}
#endif

static const char *notifyLedColorName(uint8_t color) {
    switch (cfgCoerceNotifyLedColor((int)color)) {
        case NOTIFY_LED_COLOR_RED:     return TR("Red");
        case NOTIFY_LED_COLOR_GREEN:   return TR("Green");
        case NOTIFY_LED_COLOR_BLUE:    return TR("Blue");
        case NOTIFY_LED_COLOR_YELLOW:  return TR("Yellow");
        case NOTIFY_LED_COLOR_CYAN:    return TR("Cyan");
        case NOTIFY_LED_COLOR_MAGENTA: return TR("Magenta");
        case NOTIFY_LED_COLOR_WHITE:   return TR("White");
        case NOTIFY_LED_COLOR_OFF:     return TR("Off");
        default:                       return TR("Blue");
    }
}

#if HAS_NOTIFY_LED
static uint16_t notifyLedColorPreview565(uint8_t color) {
    switch (cfgCoerceNotifyLedColor((int)color)) {
        case NOTIFY_LED_COLOR_RED:     return TFT_RED;
        case NOTIFY_LED_COLOR_GREEN:   return TFT_GREEN;
        case NOTIFY_LED_COLOR_BLUE:    return TFT_BLUE;
        case NOTIFY_LED_COLOR_YELLOW:  return TFT_YELLOW;
        case NOTIFY_LED_COLOR_CYAN:    return TFT_CYAN;
        case NOTIFY_LED_COLOR_MAGENTA: return TFT_MAGENTA;
        case NOTIFY_LED_COLOR_WHITE:   return TFT_WHITE;
        case NOTIFY_LED_COLOR_OFF:     return TFT_BLACK;
        default:                       return TFT_BLUE;
    }
}
#endif

#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_WIO_TRACKER_L2) \
    || defined(DEVICE_TDISPLAY_P4) || defined(DEVICE_TANMATSU)
namespace {
static bool sPagerAudioInitTried = false;
static bool sPagerAudioReady = false;
static constexpr i2s_port_t kPagerI2SPort = I2S_NUM_0;
static constexpr uint8_t kPagerAudioVolActive = 50;
static constexpr uint8_t kPagerAudioVolIdle = 10;
static constexpr float kPagerToneAmplitude = 7800.0f;
static uint8_t sPagerAudioVolume = 0xFF;
#if !defined(DEVICE_TANMATSU)
static audio_driver::DriverPins sPagerAudioPins;
static audio_driver::AudioBoard sPagerAudioBoard(audio_driver::AudioDriverES8311,
                                                 sPagerAudioPins);
#endif

// The codec's I2C bus. On the T-Display P4 that is controller 1 re-pinned to
// the codec's 20/21, which it shares with the keyboard accessory -- so every
// codec access points it back here first (tdisplayP4I2c1()). Elsewhere the
// codec has the main bus to itself.
#if defined(DEVICE_TANMATSU)
// The Tanmatsu's ES8156 is driven directly (tanmatsu_coproc.cpp), not through
// AudioBoard, which has no driver for it. Its digital volume stays fixed and the
// amplifier, switched per sound, is what keeps idle silent; loudness is scaled
// in the samples as on every board.
static inline void pagerAudioSetMute(bool) {}
static inline void pagerAudioApplyVolume(uint8_t volume) { sPagerAudioVolume = volume; }
#else
static inline TwoWire &pagerAudioCodecBus() {
#if defined(DEVICE_TDISPLAY_P4)
    return tdisplayP4I2c1(TDISPLAY_P4_I2C1_AUDIO);
#else
    return Wire;
#endif
}

static inline void pagerAudioSetMute(bool mute) {
    (void)pagerAudioCodecBus();
    sPagerAudioBoard.setMute(mute);
}

static inline void pagerAudioApplyVolume(uint8_t volume) {
    if (sPagerAudioVolume == volume) return;
    (void)pagerAudioCodecBus();
    sPagerAudioBoard.setVolume(volume);
    sPagerAudioVolume = volume;
}
#endif

// Gate the speaker power amp. It stays off while idle so power-supply
// transients (for example LoRa TX current spikes) cannot become random clicks.
#if defined(DEVICE_TLORA_PAGER_TFT)
static int sPagerExpAddr = -2;  // -2 = not yet probed
#endif
static void pagerAudioSetAmp(bool on) {
#if defined(DEVICE_WIO_TRACKER_L2)
    if (!wioTrackerL2IoSetAudioPaPower(on)) {
        Serial.printf("[audio] Wio Tracker L2 PA %s failed\n", on ? "on" : "off");
    }
#elif defined(DEVICE_TDISPLAY_P4)
    // XL9535 bit 6 (TDISPLAY_P4_EXP_AUDIO_POWER), LilyGO's amplifier enable.
    if (!tdisplayP4IoSetAudioPower(on)) {
        Serial.printf("[audio] T-Display P4 amp %s failed\n", on ? "on" : "off");
    }
#elif defined(DEVICE_TANMATSU)
    // Coprocessor OUTPUT bit 0.
    if (!tanmatsuCoprocSetAmplifier(on)) {
        Serial.printf("[audio] Tanmatsu amp %s failed\n", on ? "on" : "off");
    }
#else
    if (sPagerExpAddr == -2) sPagerExpAddr = xl9555FindAddr();
    if (sPagerExpAddr < 0) return;
    uint8_t out0, out1, cfg0, cfg1;
    if (!xl9555ReadAll((uint8_t)sPagerExpAddr, out0, out1, cfg0, cfg1)) return;
    xl9555SetOutput(XL9555_PIN_AMP_EN, on, out0, out1, cfg0, cfg1);
    (void)xl9555WriteAll((uint8_t)sPagerExpAddr, out0, out1, cfg0, cfg1);
#endif
}

static bool pagerAudioSelectCommFormat(i2s_config_t &cfg) {
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 4)
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
#else
    // Legacy drivers expect I2S Philips mode as I2S | I2S_MSB.
    cfg.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
#endif
    return true;
}

static bool pagerAudioInitI2S() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = 44100;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    if (!pagerAudioSelectCommFormat(cfg)) return false;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 6;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;
    cfg.fixed_mclk = 0;

    esp_err_t err = i2s_driver_install(kPagerI2SPort, &cfg, 0, nullptr);
    if (err == ESP_ERR_INVALID_STATE) {
        i2s_driver_uninstall(kPagerI2SPort);
        err = i2s_driver_install(kPagerI2SPort, &cfg, 0, nullptr);
    }
    if (err != ESP_OK) {
        Serial.printf("[audio] i2s install failed err=%d\n", (int)err);
        return false;
    }

    i2s_pin_config_t pinCfg = {};
    pinCfg.bck_io_num = AUDIO_DAC_I2S_BCK;
    pinCfg.ws_io_num = AUDIO_DAC_I2S_WS;
    pinCfg.data_out_num = AUDIO_DAC_I2S_DOUT;
    pinCfg.data_in_num = I2S_PIN_NO_CHANGE;
#if defined(ESP_IDF_VERSION) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0))
    pinCfg.mck_io_num = AUDIO_DAC_I2S_MCLK;
#endif

    err = i2s_set_pin(kPagerI2SPort, &pinCfg);
    if (err != ESP_OK) {
        Serial.printf("[audio] i2s set pin failed err=%d\n", (int)err);
        i2s_driver_uninstall(kPagerI2SPort);
        return false;
    }

    err = i2s_set_clk(kPagerI2SPort, 44100, I2S_BITS_PER_SAMPLE_16BIT,
                      I2S_CHANNEL_STEREO);
    if (err != ESP_OK) {
        Serial.printf("[audio] i2s set clk failed err=%d\n", (int)err);
        i2s_driver_uninstall(kPagerI2SPort);
        return false;
    }

    i2s_zero_dma_buffer(kPagerI2SPort);
    return true;
}

static bool pagerAudioEnsureReady() {
    if (sPagerAudioReady) return true;
    if (!sPagerAudioInitTried) {
        sPagerAudioInitTried = true;
    } else {
        // Splash playback can run before full UI init; keep retrying.
        Serial.println("[audio] codec init retry");
    }

#if defined(DEVICE_TANMATSU)
    if (!tanmatsuCodecBegin()) {
        Serial.println("[audio] codec init failed");
        return false;
    }
    sPagerAudioVolume = 0xFF;
    pagerAudioSetAmp(false);  // amp powered only during playback (see pagerAudioSetAmp)
#else
    sPagerAudioPins.addI2C(audio_driver::PinFunction::CODEC, pagerAudioCodecBus());
    sPagerAudioPins.addI2S(audio_driver::PinFunction::CODEC,
                           AUDIO_DAC_I2S_MCLK,
                           AUDIO_DAC_I2S_BCK,
                           AUDIO_DAC_I2S_WS,
                           AUDIO_DAC_I2S_DOUT,
                           AUDIO_DAC_I2S_DIN);

    (void)sPagerAudioBoard.driver().setI2CAddress(AUDIO_CODEC_ADDR);

    audio_driver::CodecConfig cfg;
    cfg.input_device = audio_driver::ADC_INPUT_NONE;
    cfg.output_device = audio_driver::DAC_OUTPUT_ALL;
    cfg.i2s.bits = audio_driver::BIT_LENGTH_16BITS;
    cfg.i2s.rate = audio_driver::RATE_44K;
    cfg.i2s.channels = audio_driver::CHANNELS2;
    cfg.i2s.fmt = audio_driver::I2S_NORMAL;
    cfg.i2s.mode = audio_driver::MODE_SLAVE;

    if (!sPagerAudioBoard.begin(cfg)) {
        Serial.println("[audio] codec init failed");
        sPagerAudioReady = false;
        return false;
    }

    sPagerAudioVolume = 0xFF;
    pagerAudioApplyVolume(kPagerAudioVolIdle);
    // Stay muted while idle: an unmuted DAC sitting between sounds emits random
    // DC-offset clicks/snaps. We unmute only while a tone is actually playing.
    sPagerAudioBoard.setMute(true);
    pagerAudioSetAmp(false);  // amp powered only during playback (see pagerAudioSetAmp)
#endif

    if (!pagerAudioInitI2S()) {
        sPagerAudioReady = false;
        return false;
    }

    sPagerAudioReady = true;
    Serial.println("[audio] codec/i2s ready");
    return true;
}

static inline void pagerAudioStartPlayback() {
    // Power the speaker amp and let the class-D output stage settle before audio.
    pagerAudioSetAmp(true);
    delay(AUDIO_AMP_SETTLE_MS);
    // Raise the gain while still muted so the analog volume step is inaudible.
    pagerAudioApplyVolume(kPagerAudioVolActive);
    i2s_zero_dma_buffer(kPagerI2SPort);
    // Prime codec/I2S with a short silent pre-roll so the first note isn't clipped.
    int16_t preRoll[256] = {0};
    size_t preRollWritten = 0;
    (void)i2s_write(kPagerI2SPort, preRoll, sizeof(preRoll),
                    &preRollWritten, 10 / portTICK_PERIOD_MS);
    // Unmute only once a clean stream of silence is already flowing → click-free.
    pagerAudioSetMute(false);
}

static void pagerAudioWriteSilence(uint16_t durationMs);

// Longer than the I2S DMA queue: 6 buffers x 256 frames at 44.1 kHz = 34.8 ms.
static constexpr uint16_t kPagerI2SDrainMs = 40;

static inline void pagerAudioStopPlayback() {
    // Drain before muting. i2s_write() returns once a block is in the DMA queue,
    // not once it has played, so up to ~35 ms of the last note is still queued
    // here. The old 512-frame (11.6 ms) tail was shorter than that queue, and the
    // mute and i2s_zero_dma_buffer() below cut the end of the note off.
    pagerAudioWriteSilence(kPagerI2SDrainMs);
    // Mute while the output is silent, then drop the gain (now inaudible) and
    // leave the DAC muted for idle so it can't emit stray clicks between sounds.
    pagerAudioSetMute(true);
    i2s_zero_dma_buffer(kPagerI2SPort);
    pagerAudioApplyVolume(kPagerAudioVolIdle);
    pagerAudioSetAmp(false);  // power down the amp for idle → no idle snaps
}

static void pagerAudioWriteSilence(uint16_t durationMs) {
    if (!pagerAudioEnsureReady() || durationMs == 0) return;

    static constexpr uint32_t kSampleRate = 44100;
    static constexpr int kChunkFrames = 120;
    int16_t zeroPcm[kChunkFrames * 2] = {0};

    uint32_t framesRemaining = ((uint32_t)durationMs * kSampleRate) / 1000U;
    while (framesRemaining > 0) {
        int framesNow = (framesRemaining > (uint32_t)kChunkFrames)
                        ? kChunkFrames
                        : (int)framesRemaining;
        size_t written = 0;
        esp_err_t err = i2s_write(kPagerI2SPort, zeroPcm,
                                  (size_t)(framesNow * 2 * (int)sizeof(int16_t)),
                                  &written, portMAX_DELAY);
        if (err != ESP_OK) {
            Serial.printf("[audio] i2s silence write failed err=%d\n", (int)err);
            break;
        }
        framesRemaining -= (uint32_t)framesNow;
    }
}

static void pagerAudioPlayTone(uint16_t freqHz, uint16_t durationMs) {
    if (!pagerAudioEnsureReady()) return;

    static constexpr uint32_t kSampleRate = 44100;
    static constexpr int kChunkFrames = 120;
    int16_t pcm[kChunkFrames * 2];

    uint32_t framesRemaining = ((uint32_t)durationMs * kSampleRate) / 1000U;
    if (framesRemaining == 0) return;
    const uint32_t totalFrames = framesRemaining;
    uint32_t frameIndex = 0;
    uint32_t attackFrames = kSampleRate / 500;
    uint32_t releaseFrames = kSampleRate / 400;
    if (attackFrames < 8) attackFrames = 8;
    if (releaseFrames < 8) releaseFrames = 8;
    uint32_t maxRamp = totalFrames / 5;
    if (maxRamp < 8) maxRamp = 8;
    if (attackFrames > maxRamp) attackFrames = maxRamp;
    if (releaseFrames > maxRamp) releaseFrames = maxRamp;

    // Hoisted: the volume cannot change mid-tone, and inside the loop this was
    // a config read, a clamp and a float divide on every one of 44,100 samples
    // a second.
    const float gain = kPagerToneAmplitude * notifyVolumeScale();
    const float phaseStep = 2.0f * (float)M_PI * (float)freqHz / (float)kSampleRate;
    float phase = 0.0f;

    while (framesRemaining > 0) {
        int framesNow = (framesRemaining > (uint32_t)kChunkFrames)
                        ? kChunkFrames
                        : (int)framesRemaining;
        for (int i = 0; i < framesNow; i++) {
            float s = sinf(phase);
            phase += phaseStep;
            if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;

            float env = 1.0f;
            if (frameIndex < attackFrames) {
                env = (float)frameIndex / (float)attackFrames;
            }
            uint32_t framesToEnd = totalFrames - frameIndex;
            if (framesToEnd < releaseFrames) {
                float tail = (float)framesToEnd / (float)releaseFrames;
                if (tail < env) env = tail;
            }

            int16_t v = (int16_t)(s * gain * env);
            pcm[(i * 2)] = v;
            pcm[(i * 2) + 1] = v;
            frameIndex++;
        }

        size_t written = 0;
        esp_err_t err = i2s_write(kPagerI2SPort, pcm,
                                  (size_t)(framesNow * 2 * (int)sizeof(int16_t)),
                                  &written, portMAX_DELAY);
        if (err != ESP_OK) {
            Serial.printf("[audio] i2s write failed err=%d\n", (int)err);
            break;
        }
        framesRemaining -= (uint32_t)framesNow;
    }
}

} // namespace
#endif

#if defined(DEVICE_TDECK)
namespace {
static bool sTdeckAudioInitTried = false;
static bool sTdeckAudioReady = false;
static constexpr i2s_port_t kTdeckI2SPort = I2S_NUM_0;

static bool tdeckAudioSelectCommFormat(i2s_config_t &cfg) {
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 4)
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
#else
    cfg.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
#endif
    return true;
}

static bool tdeckAudioInitI2S() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = 44100;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    if (!tdeckAudioSelectCommFormat(cfg)) return false;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 6;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;
    cfg.fixed_mclk = 0;

    esp_err_t err = i2s_driver_install(kTdeckI2SPort, &cfg, 0, nullptr);
    if (err == ESP_ERR_INVALID_STATE) {
        i2s_driver_uninstall(kTdeckI2SPort);
        err = i2s_driver_install(kTdeckI2SPort, &cfg, 0, nullptr);
    }
    if (err != ESP_OK) {
        Serial.printf("[audio] tdeck i2s install failed err=%d\n", (int)err);
        return false;
    }

    i2s_pin_config_t pinCfg = {};
    pinCfg.bck_io_num = TDECK_DAC_I2S_BCK;
    pinCfg.ws_io_num = TDECK_DAC_I2S_WS;
    pinCfg.data_out_num = TDECK_DAC_I2S_DOUT;
    pinCfg.data_in_num = I2S_PIN_NO_CHANGE;
#if defined(ESP_IDF_VERSION) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0))
    pinCfg.mck_io_num = I2S_PIN_NO_CHANGE;
#endif

    err = i2s_set_pin(kTdeckI2SPort, &pinCfg);
    if (err != ESP_OK) {
        Serial.printf("[audio] tdeck i2s set pin failed err=%d\n", (int)err);
        i2s_driver_uninstall(kTdeckI2SPort);
        return false;
    }

    err = i2s_set_clk(kTdeckI2SPort, 44100, I2S_BITS_PER_SAMPLE_16BIT,
                      I2S_CHANNEL_STEREO);
    if (err != ESP_OK) {
        Serial.printf("[audio] tdeck i2s set clk failed err=%d\n", (int)err);
        i2s_driver_uninstall(kTdeckI2SPort);
        return false;
    }

    i2s_zero_dma_buffer(kTdeckI2SPort);
    return true;
}

static bool tdeckAudioEnsureReady() {
    if (sTdeckAudioInitTried) return sTdeckAudioReady;
    sTdeckAudioInitTried = true;
    sTdeckAudioReady = tdeckAudioInitI2S();
    if (sTdeckAudioReady) {
        Serial.println("[audio] tdeck i2s speaker ready");
    }
    return sTdeckAudioReady;
}

static inline void tdeckAudioStartPlayback() {
    i2s_zero_dma_buffer(kTdeckI2SPort);
}

// Real silence frames, so a rest between notes is heard. A delay() here would
// not be: ~35 ms of the previous note is still queued in DMA when
// tdeckAudioPlayTone() returns, so the delay elapses while it plays and the
// notes run together.
static void tdeckAudioWriteSilence(uint16_t durationMs) {
    if (!tdeckAudioEnsureReady() || durationMs == 0) return;

    static constexpr uint32_t kSampleRate = 44100;
    static constexpr int kChunkFrames = 120;
    int16_t zeroPcm[kChunkFrames * 2] = {0};

    uint32_t framesRemaining = ((uint32_t)durationMs * kSampleRate) / 1000U;
    while (framesRemaining > 0) {
        int framesNow = (framesRemaining > (uint32_t)kChunkFrames)
                        ? kChunkFrames
                        : (int)framesRemaining;
        size_t written = 0;
        esp_err_t err = i2s_write(kTdeckI2SPort, zeroPcm,
                                  (size_t)(framesNow * 2 * (int)sizeof(int16_t)),
                                  &written, portMAX_DELAY);
        if (err != ESP_OK) {
            Serial.printf("[audio] tdeck i2s silence write failed err=%d\n", (int)err);
            break;
        }
        framesRemaining -= (uint32_t)framesNow;
    }
}

// Longer than the I2S DMA queue: 6 buffers x 256 frames at 44.1 kHz = 34.8 ms.
static constexpr uint16_t kTdeckI2SDrainMs = 40;

static inline void tdeckAudioStopPlayback() {
    // Drain before zeroing: the old 64-frame tail left up to ~35 ms of the last
    // note in the DMA queue, and i2s_zero_dma_buffer() erased it unplayed.
    tdeckAudioWriteSilence(kTdeckI2SDrainMs);
    i2s_zero_dma_buffer(kTdeckI2SPort);
}

static void tdeckAudioPlayTone(uint16_t freqHz, uint16_t durationMs) {
    if (!tdeckAudioEnsureReady()) return;

    static constexpr uint32_t kSampleRate = 44100;
    static constexpr int kChunkFrames = 120;
    int16_t pcm[kChunkFrames * 2];

    uint32_t framesRemaining = ((uint32_t)durationMs * kSampleRate) / 1000U;
    if (framesRemaining == 0) return;
    const uint32_t totalFrames = framesRemaining;
    uint32_t frameIndex = 0;
    uint32_t rampFrames = kSampleRate / 400;
    if (rampFrames < 12) rampFrames = 12;

    // Hoisted: the volume cannot change mid-tone, and inside the loop this was
    // a config read, a clamp and a float divide on every one of 44,100 samples
    // a second.
    const float gain = 2800.0f * notifyVolumeScale();
    const float phaseStep = 2.0f * (float)M_PI * (float)freqHz / (float)kSampleRate;
    float phase = 0.0f;

    while (framesRemaining > 0) {
        int framesNow = (framesRemaining > (uint32_t)kChunkFrames)
                        ? kChunkFrames
                        : (int)framesRemaining;
        for (int i = 0; i < framesNow; i++) {
            float s = sinf(phase);
            phase += phaseStep;
            if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;

            float env = 1.0f;
            if (frameIndex < rampFrames) {
                env = (float)frameIndex / (float)rampFrames;
            }
            uint32_t framesToEnd = totalFrames - frameIndex;
            if (framesToEnd < rampFrames) {
                float tail = (float)framesToEnd / (float)rampFrames;
                if (tail < env) env = tail;
            }

            int16_t v = (int16_t)(s * gain * env);
            pcm[(i * 2)] = v;
            pcm[(i * 2) + 1] = v;
            frameIndex++;
        }

        size_t written = 0;
        esp_err_t err = i2s_write(kTdeckI2SPort, pcm,
                                  (size_t)(framesNow * 2 * (int)sizeof(int16_t)),
                                  &written, portMAX_DELAY);
        if (err != ESP_OK) {
            Serial.printf("[audio] tdeck i2s write failed err=%d\n", (int)err);
            break;
        }
        framesRemaining -= (uint32_t)framesNow;
    }
}

} // namespace
#endif

#if defined(DEVICE_CARDPUTER_LORA_HAT)
namespace {
// Re-applied on every play rather than once: the volume setting can change
// between notifications, and M5's speaker keeps whatever it was last given.
static inline void cardputerAudioEnsureReady() {
    // Clamped: the scale reaches 2.0x at 100%, and this driver takes a uint8_t.
    {
        const float scaled = 180.0f * notifyVolumeScale();
        cardputerSpeakerSetVolume((uint8_t)(scaled > 255.0f ? 255.0f : scaled));
    }
}
} // namespace
#endif

// ── Notification melodies ────────────────────────────────────────────────────
// Each alert style is one list of notes, played by whichever audio path the
// board has. A rest is a note of frequency 0, so gaps are part of the melody
// and every path plays them the same way: real silence frames on I2S, a
// zero-duty note on the buzzer, a wait on the Cardputer speaker.
struct AlertNote {
    uint16_t hz;      // 0 = rest
    uint16_t durMs;
};

struct AlertMelody {
    const AlertNote *notes;
    size_t count;
};

template <size_t N>
static constexpr AlertMelody alertMelody(const AlertNote (&notes)[N]) {
    return AlertMelody{notes, N};
}

#if (BOARD_BUZZER >= 0)
namespace {
// ── Passive-buzzer alert patterns (Heltec, M9) ───────────────────────────────
// Every alert style used to leave this board as one 1760 Hz beep: Default,
// Chirpy and Bass were the same sound, which is the one thing the setting
// exists to choose between.
//
// These are not copies of the speaker patterns above, for two reasons.
//
//  - A passive piezo has a single resonant peak, somewhere around 2-4 kHz, and
//    falls away steeply below it. The Cardputer's Bass pattern bottoms out at
//    440 Hz, which this part reproduces at a fraction of the volume — so "Bass"
//    here is the lowest register it still projects and earns its name by being
//    slower and longer, not by being inaudibly low.
//  - tone() does not block the caller. It posts to the Arduino tone task, which
//    holds the pin for each note's duration before taking the next one off the
//    queue, so a whole pattern is enqueued in microseconds and plays out behind
//    the main loop. An alert here never delays packet handling, which the
//    blocking speaker paths above do for the length of their pattern.
//
// A rest is a note of frequency 0: ledcWriteTone() takes the duty to zero for
// the duration, so gaps queue exactly like notes and keep their place in the
// sequence. Without them the notes run together and the shape is lost.
static void buzzerPlayMelody(const AlertMelody &m) {
    for (size_t i = 0; i < m.count; i++) {
        tone(BOARD_BUZZER, m.notes[i].hz, m.notes[i].durMs);
    }
}

// Two-tone descending chime — the "you have mail" shape, and the one that
// carries best across a room.
static const AlertNote kBuzzerDefault[] = {
    {2637, 70}, {0, 30}, {1976, 110},
};

// Three rising chirps at the top of the part's range: short, bright, and the
// most obviously different from Default when heard back to back in the picker.
static const AlertNote kBuzzerChirpy[] = {
    {2637, 28}, {0, 22}, {3136, 28}, {0, 22}, {3951, 46},
};

// Low and unhurried. Two notes rather than three, each roughly twice the length
// of the Chirpy ones, so it reads as a different sound and not just a quieter
// one — which is all a true low note would have been on this hardware.
static const AlertNote kBuzzerBass[] = {
    {1109, 90}, {0, 60}, {880, 150},
};
} // namespace
#endif

#if HAS_AUDIO_ALERTS && (BOARD_BUZZER < 0)
// ── Speaker melodies (Pager, Wio Tracker L2, T-Display P4, Tanmatsu, T-Deck,
// Cardputer, CardputerZero) ──────────────────────────────────────────────────
// One set for every board with a real speaker, so a style sounds the same on
// all of them. These used to be three sets: the T-Deck's an octave above the
// Pager's at half the length, the Cardputer's somewhere between. These are the
// Pager's, which is what most builds already played. The passive buzzer keeps
// its own (above): its piezo cannot reproduce this register.
static const AlertNote kSpeakerDefault[] = {
    {880, 68}, {0, 8}, {740, 68}, {0, 8}, {660, 112},
};
static const AlertNote kSpeakerChirpy[] = {
    {1047, 36}, {0, 6}, {1319, 36}, {0, 6}, {1568, 40}, {0, 6}, {1319, 70},
};
static const AlertNote kSpeakerBass[] = {
    {392, 92}, {0, 10}, {330, 86}, {0, 10}, {262, 130},
};

// Blocks until the melody has played; the buzzer path is the only one that
// queues notes and returns.
static void speakerPlayMelody(const AlertMelody &m) {
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_WIO_TRACKER_L2) \
    || defined(DEVICE_TDISPLAY_P4) || defined(DEVICE_TANMATSU)
    if (!pagerAudioEnsureReady()) return;
    pagerAudioStartPlayback();
    for (size_t i = 0; i < m.count; i++) {
        if (m.notes[i].hz) pagerAudioPlayTone(m.notes[i].hz, m.notes[i].durMs);
        else               pagerAudioWriteSilence(m.notes[i].durMs);
    }
    pagerAudioStopPlayback();
#elif defined(DEVICE_TDECK)
    if (!tdeckAudioEnsureReady()) return;
    tdeckAudioStartPlayback();
    for (size_t i = 0; i < m.count; i++) {
        if (m.notes[i].hz) tdeckAudioPlayTone(m.notes[i].hz, m.notes[i].durMs);
        else               tdeckAudioWriteSilence(m.notes[i].durMs);
    }
    tdeckAudioStopPlayback();
#elif defined(DEVICE_CARDPUTER_ZERO)
    // Synthesized whole and handed to the system's sound server, which plays it
    // at the system volume. Unlike the paths above this returns at once.
    constexpr unsigned kRate = 22050;
    constexpr float kAmplitude = 16000.0f;
    constexpr unsigned kRampSamples = kRate / 500;   // 2 ms in and out: no clicks
    std::vector<int16_t> pcm;
    for (size_t i = 0; i < m.count; i++) {
        const size_t n = (size_t)m.notes[i].durMs * kRate / 1000;
        const float step = 2.0f * (float)M_PI * (float)m.notes[i].hz / (float)kRate;
        for (size_t k = 0; k < n; k++) {
            float env = 1.0f;
            if (k < kRampSamples) env = (float)k / kRampSamples;
            else if (n - k < kRampSamples) env = (float)(n - k) / kRampSamples;
            pcm.push_back(m.notes[i].hz
                              ? (int16_t)(kAmplitude * env * sinf(step * (float)k))
                              : (int16_t)0);
        }
    }
    czeroSystemPlayPcm(pcm.data(), pcm.size(), kRate);
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
    cardputerAudioEnsureReady();
    for (size_t i = 0; i < m.count; i++) {
        // M5's tone() returns at once, so the wait is what spaces the notes.
        if (m.notes[i].hz) (void)cardputerSpeakerTone((float)m.notes[i].hz, m.notes[i].durMs, 0, true);
        delay((uint32_t)m.notes[i].durMs);
    }
#endif
}
#endif

// The sound for one alert style, and nothing else: no LED, keyboard blink or
// vibration, which belong to an arriving message. The style picker and the
// volume slider preview through this, so trying a sound no longer flashes the
// notification lights or buzzes the motor.
static void playAlertSound(uint8_t mode) {
#if (BOARD_BUZZER >= 0)
    switch (mode) {
        case MSG_ALERT_SOUND_OFF:    return;
        case MSG_ALERT_SOUND_CHIRPY: buzzerPlayMelody(alertMelody(kBuzzerChirpy)); break;
        case MSG_ALERT_SOUND_BASS:   buzzerPlayMelody(alertMelody(kBuzzerBass)); break;
        case MSG_ALERT_SOUND_DEFAULT:
        default:                     buzzerPlayMelody(alertMelody(kBuzzerDefault)); break;
    }
#elif HAS_AUDIO_ALERTS
    switch (mode) {
        case MSG_ALERT_SOUND_OFF:    return;
        case MSG_ALERT_SOUND_CHIRPY: speakerPlayMelody(alertMelody(kSpeakerChirpy)); break;
        case MSG_ALERT_SOUND_BASS:   speakerPlayMelody(alertMelody(kSpeakerBass)); break;
        case MSG_ALERT_SOUND_DEFAULT:
        default:                     speakerPlayMelody(alertMelody(kSpeakerDefault)); break;
    }
#else
    (void)mode;
#endif
}

enum : uint8_t {
    MSG_ALERT_VISUAL_CHANNEL = 0,
    MSG_ALERT_VISUAL_DM = 1,
};

#if HAS_LIGHT_NOTIFY
// ── Light-notification window ────────────────────────────────────────────────
// The LED and the keyboard blink both repeat once a second for as long as
// something is unread, which means overnight they repeat all night. This is the
// clock that lets them stop: armed on arrival, checked only where a pattern is
// about to be re-armed, so a blink already in flight always finishes dark.
//
// Timed from the most recent arrival rather than from when the unread state
// began — a busy channel keeps the light alive, silence lets it lapse, and a
// new message starts the window over.
static uint32_t s_lightNotifyArmedMs = 0;

// Armed on arrival rather than inside the blink itself, because on HAS_KB_BLINK
// boards the blink declines to run while the screen is awake. Arming there
// would leave a message that landed with the screen on unarmed, and the light
// would then start up whenever the screen happened to sleep — hours later, past
// any window the user set.
static void lightNotifyArm() {
    s_lightNotifyArmedMs = millis();
    if (s_lightNotifyArmedMs == 0) s_lightNotifyArmedMs = 1;   // 0 is the boot value
}

static bool lightNotifyWindowOpen() {
    if (s_cfg.notifyLightTimeoutS == 0) return true;   // never stop: the default
    if (s_lightNotifyArmedMs == 0) return false;       // nothing has arrived yet
    // Unsigned subtraction, so the ~49-day millis() wrap takes care of itself.
    return (uint32_t)(millis() - s_lightNotifyArmedMs)
           < (uint32_t)s_cfg.notifyLightTimeoutS * 1000UL;
}
#endif

static void triggerMessageAlert(uint8_t visualSource) {
    static uint32_t lastAlertMs = 0;
    uint32_t nowMs = millis();
    if (nowMs - lastAlertMs < 120) {
        debugLogMessages("[alert] suppressed (rate limit, %lums since last)\n",
                         (unsigned long)(nowMs - lastAlertMs));
        return;
    }
    lastAlertMs = nowMs;
    // Distinguishes "never asked to play" from "played but inaudible" when a
    // notification goes missing — the two have completely different causes.
    debugLogMessages("[alert] play mode=%u vol=%u screenAsleep=%d cpu=%luMHz\n",
                     (unsigned)s_cfg.msgAlertSound, (unsigned)s_cfg.volumePct,
                     (int)s_screenAsleep, (unsigned long)getCpuFrequencyMhz());

#if HAS_LIGHT_NOTIFY
    // Restart the reminder window. Below the rate limit above on purpose: a
    // burst inside 120 ms is one arrival as far as the window is concerned.
    lightNotifyArm();
#endif

#if HAS_NOTIFY_LED
    // Blink the front LED for any new message, channel or DM. Deliberately
    // ahead of the sound below, which is skipped when the alert tone is
    // off — a silent device is exactly when a visual cue matters,
    // and it is the only notification available while the screen is asleep.
    meshDeckLedNotify(visualSource == MSG_ALERT_VISUAL_DM);
#endif

#if HAS_HAPTIC
    // Ahead of the sound, which is skipped when the tone is off: a
    // silent device is exactly when a buzz matters. Started before the tone,
    // whose blocking I2S writes would otherwise delay it -- the motor runs on
    // the chip while they play. Once for a channel message, twice for a DM.
    tdisplayP4HapticPlay(cfgHapticStyle(s_cfg),
                         visualSource == MSG_ALERT_VISUAL_DM ? 2 : 1);
#endif

#if HAS_KB_BLINK
    // Ahead of the sound, for the same reason as the Mesh Deck LED: a device
    // with the alert tone off is exactly when a visual cue matters. Called for every message; it decides
    // for itself whether the current board permits a pulse while awake.
    kbBlinkNotify(visualSource == MSG_ALERT_VISUAL_DM);
#endif

    playAlertSound(s_cfg.msgAlertSound);
}

#if HAS_VOLUME_CONTROL
// Sample tone for the volume slider. Plays even when notification sound is OFF,
// which would otherwise leave the volume control silent for exactly the people
// most likely to be turning it back up. Plays the default pattern whatever the alert style is set to — the
// slider is about level, not about which sound.
static void playVolumePreviewTone() {
    playAlertSound(MSG_ALERT_SOUND_DEFAULT);
}
#endif

static void playSplashStartupRiff() {
    if (!s_cfg.splashMelodyEnabled) return;

    // Middle-octave riff: E, F, F#, F, E, D#, E
#if HAS_AUDIO_ALERTS && (BOARD_BUZZER < 0)
    // Quarter notes with a sixteenth rest between.
    static const AlertNote kRiff[] = {
        {330, 220}, {0, 55}, {349, 220}, {0, 55}, {370, 220}, {0, 55}, {349, 220}, {0, 55},
        {330, 220}, {0, 55}, {311, 220}, {0, 55}, {330, 220},
    };
    speakerPlayMelody(alertMelody(kRiff));
#elif (BOARD_BUZZER >= 0)
    static const uint16_t kNotesHz[] = {330, 349, 370, 349, 330, 311, 330};
    static const uint16_t kQuarterMs = 220;
    static const uint16_t kPause16thMs = 55;
    static const size_t kCount = sizeof(kNotesHz) / sizeof(kNotesHz[0]);
    for (size_t i = 0; i < kCount; i++) {
        tone(BOARD_BUZZER, kNotesHz[i], kQuarterMs);
        delay((uint32_t)kQuarterMs + (i + 1 < kCount ? kPause16thMs : 0));
    }
#endif
}

// ── Selectable themes ────────────────────────────────────────────────────────
// The built-in presets followed by whichever custom slots currently hold a
// theme, compacted so an empty slot leaves no gap in the picker. Indices are
// only stable while nothing is added or removed, which is all any caller needs:
// the on-device picker is rebuilt every time it opens, and the web page carries
// its own numbering.
struct UiThemeChoice {
    uint8_t     theme;
    uint8_t     mode;
    const char *name;
    uint16_t    bgMain;
    uint16_t    panelAlt;
    uint16_t    accent;
};

static int uiThemeChoiceCount() {
    return kUiThemePresetCount + uiCustomThemeCount();
}

static bool uiThemeChoiceAt(int idx, UiThemeChoice &out) {
    if (idx < 0) return false;
    if (idx < kUiThemePresetCount) {
        const UiThemePresetLite &p = kUiThemePresets[idx];
        out = {p.theme, p.mode, p.name, p.bgMain, p.panelAlt, p.accent};
        return true;
    }
    int want = idx - kUiThemePresetCount;
    for (int slot = 0; slot < UI_CUSTOM_THEME_SLOTS; slot++) {
        const UiCustomTheme *ct = uiCustomThemeGet(slot);
        if (!ct) continue;
        if (want-- == 0) {
            out = {uiThemeFromCustomSlot(slot), ct->mode, ct->name,
                   ct->bgMain, ct->panelAlt, ct->accent};
            return true;
        }
    }
    return false;
}

static int uiThemePresetIndexFromCfg() {
    if (uiThemeIsCustom(s_cfg.uiTheme)) {
        const int target = uiThemeCustomSlot(s_cfg.uiTheme);
        int idx = kUiThemePresetCount;
        for (int slot = 0; slot < UI_CUSTOM_THEME_SLOTS; slot++) {
            if (!uiCustomThemeGet(slot)) continue;
            if (slot == target) return idx;
            idx++;
        }
        return 0;   // selected slot has since been emptied
    }
    uint8_t mode = s_cfg.uiMode;
    if (uiThemeForcesDark(s_cfg.uiTheme)) mode = UI_MODE_DARK;
    for (int i = 0; i < kUiThemePresetCount; i++) {
        if (kUiThemePresets[i].theme == s_cfg.uiTheme
            && kUiThemePresets[i].mode == mode) {
            return i;
        }
    }
    return 0;
}

static const char *uiThemePresetNameFromCfg() {
    UiThemeChoice choice;
    if (!uiThemeChoiceAt(uiThemePresetIndexFromCfg(), choice)) return "Camillia Dark";
    return choice.name;
}

// These used to write one key each. Settings now live in a single blob, so a
// one-field save means rewriting the whole thing — hence the debounce in
// markConfigDirty() rather than a write per keypress.
static void persistUiTheme()             { markConfigDirty(); }
static void persistMessageAlertSetting() { markConfigDirty(); }
static void persistSplashMelodySetting() { markConfigDirty(); }

static void applyUiThemePalette() {
#if !HAS_UI_THEMES
    // Preserve the config fields for blob compatibility, but this build has one
    // semantic palette: the inverse of Camillia Black for reflective paper.
    s_cfg.uiTheme = MY_UI_THEME;
    s_cfg.uiMode = UI_MODE_LIGHT;
    s_cfg.chatStyle = cfgCoerceChatStyle(s_cfg.chatStyle);
    s_cfg.chatColorsEnabled = false;
    s_cfg.userMsgColor = 0xFF;
    constexpr uint16_t white = 0xFFFF;
    constexpr uint16_t black = 0x0000;
    s_ui = {
        white, white, white, white, white, white,
        black, black, white, black, black, white, black,
        black, black, black, black, white, black,
        black, black, black, black, black,
        black, black, black,
        white, white, white, black, black, black, black, black
    };
    s_appliedUiTheme = s_cfg.uiTheme;
    s_appliedUiMode = s_cfg.uiMode;
    return;
#endif

    // A custom theme selected from a slot that has since been emptied — deleted
    // in web config while it was live, or a config restored without it — has no
    // colors to read. Fall back before anything dereferences it.
    if (uiThemeIsCustom(s_cfg.uiTheme) && !uiCustomThemeGet(uiThemeCustomSlot(s_cfg.uiTheme))) {
        Serial.printf("[theme] custom slot %d is empty, falling back to Camellia\n",
                      uiThemeCustomSlot(s_cfg.uiTheme));
        s_cfg.uiTheme = UI_THEME_CAMELLIA;
    }

    const UiCustomTheme *custom = uiThemeIsCustom(s_cfg.uiTheme)
                                      ? uiCustomThemeGet(uiThemeCustomSlot(s_cfg.uiTheme))
                                      : nullptr;
    if (custom) {
        // The mode is part of the theme the user built, not a separate axis
        // they can flip underneath it: it picks the text and on-accent colors
        // the four authored ones were chosen against.
        s_cfg.uiMode = custom->mode;
    } else {
        s_cfg.uiTheme = (uint8_t)constrain((int)s_cfg.uiTheme, 0, UI_THEME_COUNT - 1);
        if (uiThemeForcesDark(s_cfg.uiTheme)) {
            s_cfg.uiMode = UI_MODE_DARK;
        } else {
            s_cfg.uiMode = (uint8_t)(s_cfg.uiMode == UI_MODE_LIGHT ? UI_MODE_LIGHT : UI_MODE_DARK);
        }
    }

    const UiThemePresetLite *preset = &kUiThemePresets[0];
    if (!custom) {
        for (int i = 0; i < kUiThemePresetCount; i++) {
            if (kUiThemePresets[i].theme == s_cfg.uiTheme && kUiThemePresets[i].mode == s_cfg.uiMode) {
                preset = &kUiThemePresets[i];
                break;
            }
        }
    }

    // The only difference a custom theme makes: where the four authored colors
    // come from. Every derivation below — the blends, the mode constants, the
    // aliases — is the same code the presets go through, which is what keeps a
    // user-built theme looking like a real one rather than four flat colors.
    const bool isLight = (s_cfg.uiMode == UI_MODE_LIGHT);
    const uint16_t bgMain = custom ? custom->bgMain : preset->bgMain;
    const uint16_t panelBg = custom ? custom->panelBg : preset->panelBg;
    const uint16_t panelAlt = custom ? custom->panelAlt : preset->panelAlt;
    const uint16_t accent = custom ? custom->accent : preset->accent;

    const uint16_t statusTop = blend565(bgMain, panelBg, isLight ? 128 : 96);
    const uint16_t statusBg = isLight ? panelAlt : panelBg;
    const uint16_t panelStrong = blend565(panelAlt, accent, isLight ? 36 : 48);
    const uint16_t tabActive = accent;
    const uint16_t tabUnread = blend565(accent, rgb565(0xFF, 0xB3, 0x00), 92);
    const uint16_t tabIdle = blend565(panelAlt, bgMain, isLight ? 90 : 120);
    const uint16_t divider = blend565(panelAlt, bgMain, isLight ? 135 : 150);
    const uint16_t dividerHi = blend565(panelAlt, accent, isLight ? 74 : 92);
    const uint16_t inputBg = panelAlt;
    const uint16_t inputTop = blend565(panelBg, panelAlt, 120);
    const uint16_t cursor = accent;

    const uint16_t textMain = isLight ? rgb565(0x1E, 0x24, 0x2C) : rgb565(0xF3, 0xF6, 0xFA);
    const uint16_t textDim = isLight ? rgb565(0x5E, 0x68, 0x76) : rgb565(0xB7, 0xC0, 0xCC);
    const uint16_t textOnAccent = isLight ? rgb565(0xFF, 0xFF, 0xFF) : rgb565(0x08, 0x0D, 0x14);
    const uint16_t statusText = textMain;

    const uint16_t selectBg = blend565(panelAlt, accent, isLight ? 36 : 44);
    const uint16_t selectAccent = accent;
    const uint16_t nodeHot = blend565(accent, rgb565(0xFF, 0x58, 0x58), 140);
    const uint16_t nodeWarm = blend565(accent, rgb565(0xFF, 0xB7, 0x2C), 120);
    const uint16_t dmMuted = isLight ? rgb565(0x8E, 0x95, 0xA2) : rgb565(0x8C, 0x97, 0xA8);

    const uint16_t battGood = rgb565(0x3A, 0xC7, 0x62);
    const uint16_t battWarn = rgb565(0xF2, 0xB5, 0x2E);
    const uint16_t battBad = rgb565(0xE0, 0x4F, 0x4F);

    const uint16_t splashTop = statusTop;
    const uint16_t splashBottom = bgMain;
    const uint16_t splashCardBg = panelBg;
    const uint16_t splashCardEdge = blend565(panelBg, accent, isLight ? 52 : 66);
    const uint16_t splashCardEdgeHi = blend565(panelAlt, accent, isLight ? 74 : 92);
    const uint16_t splashTitle = textMain;
    const uint16_t splashSub = textDim;
    const uint16_t splashDim = blend565(textDim, panelBg, isLight ? 84 : 72);

    s_ui = {
        bgMain, statusTop, statusBg, panelBg, panelAlt, panelStrong,
        tabActive, tabUnread, tabIdle, divider, dividerHi, inputBg, inputTop,
        accent, cursor, textMain, textDim, textOnAccent, statusText,
        selectBg, selectAccent, nodeHot, nodeWarm, dmMuted,
        battGood, battWarn, battBad,
        splashTop, splashBottom, splashCardBg, splashCardEdge, splashCardEdgeHi,
        splashTitle, splashSub, splashDim
    };

    s_appliedUiTheme = s_cfg.uiTheme;
    s_appliedUiMode = s_cfg.uiMode;
}

// web_config.h: the web page wears the palette derived above, so presets,
// custom themes and the e-ink palette all come out as they look on screen.
bool webCfgThemeColors(WebThemeColors *out) {
    if (!out || s_appliedUiTheme == 0xFF) return false;   // not applied yet
    out->bg          = s_ui.bgMain;
    out->panel       = s_ui.panelBg;
    out->panelAlt    = s_ui.panelAlt;
    out->panelStrong = s_ui.panelStrong;
    out->divider     = s_ui.divider;
    out->dividerHi   = s_ui.dividerHi;
    out->inputBg     = s_ui.inputBg;
    out->accent      = s_ui.accent;
    out->text        = s_ui.textMain;
    out->textDim     = s_ui.textDim;
    out->onAccent    = s_ui.textOnAccent;
    out->good        = s_ui.battGood;
    out->warn        = s_ui.battWarn;
    out->bad         = s_ui.battBad;
    out->isLight     = (s_cfg.uiMode == UI_MODE_LIGHT);
    return true;
}

static inline lv_color_t lvColorFrom565(uint16_t c) {
    uint8_t r = (uint8_t)((((c >> 11) & 0x1F) * 255) / 31);
    uint8_t g = (uint8_t)((((c >> 5) & 0x3F) * 255) / 63);
    uint8_t b = (uint8_t)(((c & 0x1F) * 255) / 31);
    return lv_color_make(r, g, b);
}

static constexpr uint16_t hex24To565(uint32_t rgb) {
    return rgb565((uint8_t)((rgb >> 16) & 0xFF),
                  (uint8_t)((rgb >> 8) & 0xFF),
                  (uint8_t)(rgb & 0xFF));
}

static uint16_t blend565(uint16_t c1, uint16_t c2, uint8_t t) {
    int r1 = (c1 >> 11) & 0x1F;
    int g1 = (c1 >> 5) & 0x3F;
    int b1 = c1 & 0x1F;
    int r2 = (c2 >> 11) & 0x1F;
    int g2 = (c2 >> 5) & 0x3F;
    int b2 = c2 & 0x1F;
    int r = r1 + ((r2 - r1) * t) / 255;
    int g = g1 + ((g2 - g1) * t) / 255;
    int b = b1 + ((b2 - b1) * t) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static bool isTrafficBgColor24(uint32_t rgb) {
    switch (rgb) {
        case 0x4A1D1D:
        case 0x1E3E27:
        case 0x1B3E34:
        case 0x4A4318:
        case 0x4A2D1F:
        case 0x4A3418:
        case 0x1A3B40:
        case 0x33224A:
        case 0x12345D:
        case 0x1D2E58:
        case 0x1A3754:
        case 0x1A3A3E:
        case 0x4A3618:
        case 0x102D52:
        case 0x3E3619:
        case 0x10254A:
            return true;
        default:
            return false;
    }
}

static lv_color_t themedColorHex(uint32_t rgb) {
    uint16_t mapped = 0;
    bool hasMapped = true;

    switch (rgb) {
        case 0x0B1E44: mapped = s_ui.bgMain; break;
        case 0x0E285B: mapped = s_ui.panelBg; break;
        case 0x0F2A5C: mapped = s_ui.panelAlt; break;
        case 0x102750: mapped = s_ui.tabIdle; break;
        case 0x102B61:
        case 0x1E355F: mapped = s_ui.inputBg; break;
        // Both are the same thing: a surface raised off the panel. 0x123266 is
        // the idle row, 0x16386F the button face -- every action button in the
        // UI is drawn in it (Discovery's Sweep/Preset/Clear/Save row, chat's
        // New Message and Actions, the Nodes filter, the DM composer).
        //
        // 0x16386F was not in this table at all, which is the bug. Its labels
        // are 0xE8F1FF/0xD9E8FF, which map to s_ui.textMain and so flip to near
        // black on a light theme -- while the button face, unmapped, stayed the
        // dark navy authored for the dark themes. Black on navy: the buttons
        // were there and could be pressed, and nothing on them could be read.
        //
        // Visible on the touch builds first because those are the boards whose
        // buttons are the only way in; the same faces are drawn on every board.
        case 0x123266:
        case 0x16386F: mapped = s_ui.panelStrong; break;
        case 0x2A4E8F: mapped = s_ui.selectBg; break;
        case 0x2A4FB4: mapped = s_ui.tabActive; break;
        case 0x2B4D8C:
        case 0x335D9D: mapped = s_ui.divider; break;
        case 0x3F669F:
        case 0x8FB5E6:
        case 0x5C86C6: mapped = s_ui.dividerHi; break;
        case 0x4C76BA:
        case 0x5B86C7: mapped = s_ui.inputTop; break;
        case 0x79DDB8:
        case 0x84E07A:
        case 0x2C7A3B: mapped = s_ui.battGood; break;
        case 0x8EEBFF: mapped = s_ui.accent; break;
        case 0x4EC9FF:
        case 0x90B4FF: mapped = s_ui.selectAccent; break;
        case 0xA7C7FF: mapped = s_ui.textDim; break;
        case 0xBFD6FF: mapped = s_ui.statusText; break;
        case 0xD9E8FF:
        case 0xE8F1FF: mapped = s_ui.textMain; break;
        case 0xEAF3FF: mapped = s_ui.textOnAccent; break;
        case 0xFFF0B8: mapped = s_ui.tabUnread; break;
        case 0xF4D35E: mapped = s_ui.battWarn; break;
        case 0xFF6B6B: mapped = s_ui.battBad; break;
        default: hasMapped = false; break;
    }

    if (hasMapped) {
        return lvColorFrom565(mapped);
    }

    if (isTrafficBgColor24(rgb)) {
        uint16_t tone = hex24To565(rgb);
        if (s_cfg.uiMode == UI_MODE_LIGHT) {
            // Lift live-traffic backgrounds in light mode so they stay readable.
            tone = blend565(tone, 0xFFFF, 170);
        }
        return lvColorFrom565(tone);
    }

    return lv_color_make((uint8_t)((rgb >> 16) & 0xFF),
                         (uint8_t)((rgb >> 8) & 0xFF),
                         (uint8_t)(rgb & 0xFF));
}

#define lv_color_hex(rgb) themedColorHex((uint32_t)(rgb))

