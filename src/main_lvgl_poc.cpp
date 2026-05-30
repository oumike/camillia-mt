#if defined(UI_LVGL_POC)

#include <Arduino.h>
#include <esp_mac.h>
#include <string.h>

#include "channel_mgr.h"
#include "config.h"
#include "debug_flags.h"
#include "keyboard.h"
#include "lgfx_tdeck.h"
#include "live_util.h"
#include "mesh_proto.h"
#include "mesh_radio.h"
#include "node_db.h"
#include <lvgl.h>

static LGFX_TDeck lcd;
static TDeckKeyboard kb;

uint8_t myPubKey[32] = {0};
uint8_t myPrivKey[32] = {0};
uint8_t myDeviceRole = 0;

static constexpr uint16_t kMaxHorRes =
    (DEVICE_LCD_LANDSCAPE_W > DEVICE_LCD_PORTRAIT_W) ? DEVICE_LCD_LANDSCAPE_W : DEVICE_LCD_PORTRAIT_W;
static constexpr uint16_t kDrawBufLines = 40;
static lv_color_t s_drawBufMem[kMaxHorRes * kDrawBufLines];
static lv_disp_draw_buf_t s_drawBuf;

static lv_obj_t *s_screen = nullptr;
static lv_obj_t *s_navPanel = nullptr;
static lv_obj_t *s_chatPanel = nullptr;
static lv_obj_t *s_channelBtns[MESH_CHANNELS] = {};
static lv_obj_t *s_chatTitle = nullptr;
static lv_obj_t *s_chatBody = nullptr;

static int s_activeChannel = 0;
static bool s_keyboardI2cReady = true;
static uint32_t s_myNodeId = 0;
static bool s_radioReady = false;

static int modWrap(int value, int delta, int count) {
    int next = value + delta;
    while (next < 0) next += count;
    while (next >= count) next -= count;
    return next;
}

static void lvglFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    lcd.pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t *)&color_p->full);
    lv_disp_flush_ready(disp);
}

static void lvglTouchRead(lv_indev_drv_t *indev, lv_indev_data_t *data) {
    LV_UNUSED(indev);
    data->continue_reading = false;
#if TOUCH_POLL_ENABLED
    int32_t tx = 0;
    int32_t ty = 0;
    if (lcd.getTouch(&tx, &ty)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = tx;
        data->point.y = ty;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
#else
    data->state = LV_INDEV_STATE_RELEASED;
#endif
}

static void appendLine(char *dst, size_t dstSize, size_t &used, const char *line, bool withNl) {
    if (!dst || !line || used >= dstSize) return;
    int wrote = snprintf(dst + used, dstSize - used, "%s%s", line, withNl ? "\n" : "");
    if (wrote < 0) return;
    if ((size_t)wrote >= (dstSize - used)) {
        used = dstSize - 1;
    } else {
        used += (size_t)wrote;
    }
}

static void refreshChannelView() {
    if (!s_chatTitle || !s_chatBody) return;

    lv_label_set_text_fmt(s_chatTitle, "Channel %d", s_activeChannel);

    static char chatBuf[1200];
    size_t used = 0;
    chatBuf[0] = '\0';

    int shown = 0;
    static constexpr int kMaxVisibleRows = 14;
    for (int row = 0; row < kMaxVisibleRows; row++) {
        const DisplayLine *dl = Channels.getLine(s_activeChannel, row);
        if (!dl) break;
        appendLine(chatBuf, sizeof(chatBuf), used, dl->text, true);
        shown++;
    }
    if (shown == 0) {
        appendLine(chatBuf, sizeof(chatBuf), used, "(no traffic yet)", false);
    }

    chatBuf[sizeof(chatBuf) - 1] = '\0';
    lv_label_set_text(s_chatBody, chatBuf);

    for (int i = 0; i < MESH_CHANNELS; i++) {
        bool active = (i == s_activeChannel);
        lv_obj_set_style_border_width(s_channelBtns[i], active ? 2 : 1, 0);
        lv_obj_set_style_bg_opa(s_channelBtns[i], active ? LV_OPA_70 : LV_OPA_40, 0);
        lv_obj_set_style_bg_color(s_channelBtns[i], active ? lv_color_hex(0x1D4ED8) : lv_color_hex(0x111827), 0);
    }
}

static void setActiveChannel(int channelIdx) {
    if (channelIdx < 0 || channelIdx >= MESH_CHANNELS) return;
    s_activeChannel = channelIdx;
    Channels.setActive(channelIdx);
    refreshChannelView();
}

static void onChannelClick(lv_event_t *e) {
    int channel = (int)(intptr_t)lv_event_get_user_data(e);
    setActiveChannel(channel);
}

static void buildUi() {
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x05070D), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    const int horRes = lv_disp_get_hor_res(NULL);
    const int verRes = lv_disp_get_ver_res(NULL);
    const int margin = 6;
    const int navW = 70;

    s_navPanel = lv_obj_create(s_screen);
    lv_obj_set_size(s_navPanel, navW, verRes - margin * 2);
    lv_obj_align(s_navPanel, LV_ALIGN_LEFT_MID, margin, 0);
    lv_obj_set_style_radius(s_navPanel, 10, 0);
    lv_obj_set_style_bg_color(s_navPanel, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_border_width(s_navPanel, 1, 0);
    lv_obj_set_style_border_color(s_navPanel, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_pad_left(s_navPanel, 3, 0);
    lv_obj_set_style_pad_right(s_navPanel, 3, 0);
    lv_obj_set_style_pad_top(s_navPanel, 3, 0);
    lv_obj_set_style_pad_bottom(s_navPanel, 3, 0);
    lv_obj_set_style_pad_row(s_navPanel, 3, 0);
    lv_obj_set_flex_flow(s_navPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_navPanel, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < MESH_CHANNELS; i++) {
        lv_obj_t *btn = lv_btn_create(s_navPanel);
        s_channelBtns[i] = btn;
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, 24);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, onChannelClick, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, onChannelClick, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text_fmt(lbl, "CH %d", i);
        lv_obj_center(lbl);
    }

    s_chatPanel = lv_obj_create(s_screen);
    lv_obj_set_size(s_chatPanel, horRes - navW - margin * 3, verRes - margin * 2);
    lv_obj_align(s_chatPanel, LV_ALIGN_RIGHT_MID, -margin, 0);
    lv_obj_set_style_radius(s_chatPanel, 10, 0);
    lv_obj_set_style_bg_color(s_chatPanel, lv_color_hex(0x0B1220), 0);
    lv_obj_set_style_border_width(s_chatPanel, 1, 0);
    lv_obj_set_style_border_color(s_chatPanel, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_pad_all(s_chatPanel, 6, 0);
    lv_obj_clear_flag(s_chatPanel, LV_OBJ_FLAG_SCROLLABLE);

    s_chatTitle = lv_label_create(s_chatPanel);
    lv_obj_set_style_text_color(s_chatTitle, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_opa(s_chatTitle, LV_OPA_90, 0);
    lv_obj_align(s_chatTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    s_chatBody = lv_label_create(s_chatPanel);
    lv_obj_set_width(s_chatBody, lv_pct(100));
    lv_label_set_long_mode(s_chatBody, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_chatBody, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_text_opa(s_chatBody, LV_OPA_90, 0);
    lv_obj_align(s_chatBody, LV_ALIGN_TOP_LEFT, 0, 22);

    lv_scr_load(s_screen);
}

static void appendRxText(int chanIdx, uint32_t fromNode, const char *text, uint32_t packetId) {
    char timePrefix[12];
    char sender[16];
    char prefix[36];

    liveBuildPrefix(timePrefix, sizeof(timePrefix));
    liveNodeLabel(fromNode, sender, sizeof(sender), false);
    snprintf(prefix, sizeof(prefix), "%s[%s] ", timePrefix, sender);

    Channels.addMessage(chanIdx, prefix, text, TFT_WHITE, packetId, false);
}

static bool processMeshPacket(const MeshPacket &pkt) {
    Nodes.updateFromPacket(pkt);
    if (!pkt.decrypted) return false;

    switch (pkt.portnum) {
        case TEXT_MESSAGE_APP: {
            int chanIdx = (pkt.chanIdx >= 0 && pkt.chanIdx < MESH_CHANNELS) ? pkt.chanIdx : 0;

            char textBuf[MESH_TEXT_MAX_LEN + 1];
            size_t copy = pkt.payloadLen;
            if (copy > MESH_TEXT_MAX_LEN) copy = MESH_TEXT_MAX_LEN;
            memcpy(textBuf, pkt.payload, copy);
            textBuf[copy] = '\0';
            for (size_t i = 0; i < copy; i++) {
                if (textBuf[i] == '\r' || textBuf[i] == '\n') textBuf[i] = ' ';
            }

            if (textBuf[0]) {
                appendRxText(chanIdx, pkt.hdr.from, textBuf, pkt.hdr.id);
                return chanIdx == s_activeChannel;
            }
            return false;
        }

        case NODEINFO_APP: {
            UserInfo u = {};
            if (decodeUser(pkt.payload, pkt.payloadLen, u)) {
                Nodes.updateUser(pkt.hdr.from, u);
            }
            return false;
        }

        case POSITION_APP: {
            PositionInfo p = {};
            if (decodePosition(pkt.payload, pkt.payloadLen, p)) {
                Nodes.updatePosition(pkt.hdr.from, p);
            }
            return false;
        }

        case TELEMETRY_APP: {
            TelemetryInfo t = {};
            if (decodeTelemetry(pkt.payload, pkt.payloadLen, t)) {
                Nodes.updateTelemetry(pkt.hdr.from, t);
            }
            return false;
        }

        default:
            return false;
    }
}

static bool pollMeshRx() {
    bool changed = false;
    MeshPacket pkt;
    while (Radio.pollRx(pkt)) {
        changed = processMeshPacket(pkt) || changed;
    }

    if (Channels.expireAcks()) {
        changed = true;
    }

    return changed;
}

static void handleHardwareInput() {
    char k = kb.readTrackball();
    if (k == KEY_NONE) {
#if defined(DEVICE_TDECK)
        if (s_keyboardI2cReady) k = kb.readKey();
#else
        k = kb.readKey();
#endif
    }
    if (k == KEY_NONE) return;

    switch (k) {
        case KEY_SCROLL_UP:
        case KEY_PREV_CHAN:
            setActiveChannel(modWrap(s_activeChannel, -1, MESH_CHANNELS));
            return;
        case KEY_SCROLL_DN:
        case KEY_NEXT_CHAN:
            setActiveChannel(modWrap(s_activeChannel, 1, MESH_CHANNELS));
            return;
        default:
            break;
    }

    if (k >= '1' && k <= '8') {
        setActiveChannel((int)(k - '1'));
    }
}

void setup() {
    Serial.begin(115200);
    delay(120);

    // Match baseline firmware board-power bring-up so keyboard/touch I2C devices are powered.
#if (BOARD_POWERON >= 0)
    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
#endif
#if (BOARD_VEXT_ENABLE >= 0)
    pinMode(BOARD_VEXT_ENABLE, OUTPUT);
    digitalWrite(BOARD_VEXT_ENABLE, BOARD_VEXT_ON_LEVEL);
    delay(20);
#endif

#if defined(DEVICE_CARDPUTER_LORA_HAT)
    kb.begin();
#endif

    lcd.init();
    lcd.setRotation(TFT_ROTATION_DEFAULT);
    lcd.setBrightness(TFT_BRIGHTNESS_DEFAULT);
    lcd.fillScreen(TFT_BLACK);

#if !defined(DEVICE_CARDPUTER_LORA_HAT)
    kb.begin();
#endif

#if defined(DEVICE_TDECK)
    Wire.beginTransmission((uint8_t)KB_ADDR);
    s_keyboardI2cReady = (Wire.endTransmission() == 0);
    Serial.printf("[lvgl-poc] keyboard i2c probe: %s\\n", s_keyboardI2cReady ? "ok" : "missing");
#endif

    lv_init();
    lv_disp_draw_buf_init(&s_drawBuf, s_drawBufMem, nullptr, kMaxHorRes * kDrawBufLines);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = lcd.width();
    dispDrv.ver_res = lcd.height();
    dispDrv.flush_cb = lvglFlush;
    dispDrv.draw_buf = &s_drawBuf;
    lv_disp_drv_register(&dispDrv);

    static lv_indev_drv_t touchDrv;
    lv_indev_drv_init(&touchDrv);
    touchDrv.type = LV_INDEV_TYPE_POINTER;
    touchDrv.read_cb = lvglTouchRead;
    lv_indev_drv_register(&touchDrv);

    buildUi();

    debugSetFlags(false, false, false);
    Nodes.init();
    Channels.init();

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    s_myNodeId = ((uint32_t)mac[2] << 24)
               | ((uint32_t)mac[3] << 16)
               | ((uint32_t)mac[4] << 8)
               | ((uint32_t)mac[5]);

    s_radioReady = Radio.init();
    if (!s_radioReady) {
        Channels.addMessage(0, "", "[radio] init failed", TFT_RED);
    } else {
        Channels.addMessage(0, "", "[radio] listening for mesh traffic", TFT_DARKGREY);
    }

    setActiveChannel(0);
    Serial.printf("[lvgl-poc] started (%dx%d)\\n", lcd.width(), lcd.height());
}

void loop() {
    handleHardwareInput();

    if (s_radioReady && pollMeshRx()) {
        refreshChannelView();
    }

    lv_timer_handler();
    delay(5);
}

#endif  // UI_LVGL_POC
