// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// arduino-esp32 Preferences over the file-backed NVS (app/nvs_linux.cpp).
#include "Arduino.h"
#include "esp_idf_linux.h"

typedef enum {
    PT_I8, PT_U8, PT_I16, PT_U16, PT_I32, PT_U32, PT_I64, PT_U64, PT_STR, PT_BLOB, PT_INVALID
} PreferenceType;

class Preferences {
public:
    Preferences() {}
    ~Preferences() { end(); }
    bool begin(const char *name, bool readOnly = false, const char *partition_label = nullptr);
    void end();
    bool clear();
    bool remove(const char *key);

    size_t putChar(const char *key, int8_t v);
    size_t putUChar(const char *key, uint8_t v);
    size_t putShort(const char *key, int16_t v);
    size_t putUShort(const char *key, uint16_t v);
    size_t putInt(const char *key, int32_t v);
    size_t putUInt(const char *key, uint32_t v);
    size_t putLong(const char *key, int32_t v) { return putInt(key, v); }
    size_t putULong(const char *key, uint32_t v) { return putUInt(key, v); }
    size_t putLong64(const char *key, int64_t v);
    size_t putULong64(const char *key, uint64_t v);
    size_t putFloat(const char *key, float v) { return putBytes(key, &v, sizeof v); }
    size_t putDouble(const char *key, double v) { return putBytes(key, &v, sizeof v); }
    size_t putBool(const char *key, bool v) { return putUChar(key, (uint8_t)(v ? 1 : 0)); }
    size_t putString(const char *key, const char *v);
    size_t putString(const char *key, String v) { return putString(key, v.c_str()); }
    size_t putBytes(const char *key, const void *v, size_t len);

    bool isKey(const char *key);
    PreferenceType getType(const char *key);
    int8_t getChar(const char *key, int8_t d = 0);
    uint8_t getUChar(const char *key, uint8_t d = 0);
    int16_t getShort(const char *key, int16_t d = 0);
    uint16_t getUShort(const char *key, uint16_t d = 0);
    int32_t getInt(const char *key, int32_t d = 0);
    uint32_t getUInt(const char *key, uint32_t d = 0);
    int32_t getLong(const char *key, int32_t d = 0) { return getInt(key, d); }
    uint32_t getULong(const char *key, uint32_t d = 0) { return getUInt(key, d); }
    int64_t getLong64(const char *key, int64_t d = 0);
    uint64_t getULong64(const char *key, uint64_t d = 0);
    float getFloat(const char *key, float d = NAN);
    double getDouble(const char *key, double d = NAN);
    bool getBool(const char *key, bool d = false) { return getUChar(key, d ? 1 : 0) == 1; }
    size_t getString(const char *key, char *v, size_t maxLen);
    String getString(const char *key, String d = String());
    size_t getBytesLength(const char *key);
    size_t getBytes(const char *key, void *buf, size_t maxLen);
    size_t freeEntries();

private:
    nvs_handle_t _h = 0;
    bool _started = false;
    bool _readOnly = false;
};
