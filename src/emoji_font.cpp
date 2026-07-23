#include <Arduino.h>
#include "emoji_font.h"
#include "config.h"
#include "fonts/emoji_font.h"   // kEmojiFontData / kEmojiFontDataLen (flash)
// lvgl.h pulls in extra/libs/lv_libs.h, which declares lv_tiny_ttf_* when
// LV_USE_TINY_TTF is set (see lv_conf.h). emoji_font.h already includes it.

// Text sizes that carry user content (messages, names, previews) and therefore
// need an emoji fallback. Titles/hints/splash faces are omitted deliberately —
// they don't render user text, and each extra instance costs LVGL-pool cache.
namespace {
struct EmojiSlot {
    const lv_font_t *base;     // built-in Montserrat face
    lv_coord_t px;             // tiny_ttf render size to match it
    lv_font_t merged;          // mutable copy of base with fallback attached
    lv_font_t *emoji;          // tiny_ttf instance (owns glyph cache)
    bool ready;
};

// The Montserrat sizes chat/DM/node text is actually drawn at (see main_lvgl
// scaledChatFont / kMainScreenFont / kChannelChatFont). montserrat_16 is here
// too because reply previews and some node rows use it.
EmojiSlot s_slots[] = {
    { &lv_font_montserrat_10, 12, {}, nullptr, false },
    { &lv_font_montserrat_12, 14, {}, nullptr, false },
    { &lv_font_montserrat_14, 16, {}, nullptr, false },
    { &lv_font_montserrat_16, 18, {}, nullptr, false },
    { &lv_font_montserrat_18, 20, {}, nullptr, false },
};
constexpr int kSlotCount = (int)(sizeof(s_slots) / sizeof(s_slots[0]));

// Per-instance glyph cache budget, in bytes. Every tiny_ttf allocation (cache
// included) comes from the LVGL pool, so the Cardputer's 96 KB pool sets the
// ceiling. Emoji are re-rasterized on a cache miss — slower on scroll, never a
// failure — so a small cache is a fine trade there.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
constexpr size_t kEmojiCacheBytes = 2048;
#else
constexpr size_t kEmojiCacheBytes = 6144;
#endif

bool s_inited = false;
}

void emojiFontInit() {
#if LV_USE_TINY_TTF
    if (s_inited) return;
    s_inited = true;

    int readyCount = 0;
    for (int i = 0; i < kSlotCount; i++) {
        EmojiSlot &s = s_slots[i];
        s.emoji = lv_tiny_ttf_create_data_ex((const void *)kEmojiFontData,
                                             (size_t)kEmojiFontDataLen,
                                             s.px, kEmojiCacheBytes);
        if (!s.emoji) {
            // Out of pool at boot: leave this size without emoji rather than
            // half-initialize. emojiFont() will return the plain base for it.
            s.ready = false;
            Serial.printf("[emoji] slot %d (px=%d) tiny_ttf create FAILED "
                          "(LVGL pool exhausted?)\n", i, (int)s.px);
            continue;
        }
        // Copy the const base into a writable face and chain the fallback. The
        // copy shares the base's glyph data (pointers), so only the struct is
        // duplicated; the fallback is what LVGL walks for missing glyphs.
        s.merged = *s.base;
        s.merged.fallback = s.emoji;
        s.ready = true;
        readyCount++;
    }
    Serial.printf("[emoji] init: %d/%d fallback faces ready (font %u bytes)\n",
                  readyCount, kSlotCount, (unsigned)kEmojiFontDataLen);
#else
    Serial.println("[emoji] LV_USE_TINY_TTF is 0 — emoji fallback disabled");
#endif
}

const lv_font_t *emojiFont(const lv_font_t *base) {
#if LV_USE_TINY_TTF
    if (base) {
        for (int i = 0; i < kSlotCount; i++) {
            if (s_slots[i].base == base) {
                return s_slots[i].ready ? &s_slots[i].merged : base;
            }
        }
    }
#endif
    return base;
}
