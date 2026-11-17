// SPDX-License-Identifier: GPL-3.0-or-later
#include <string>
#include <Preferences.h>

#include <string.h>

bool Preferences::begin(const char *name, bool readOnly, const char *) {
    if (_started) return false;
    _readOnly = readOnly;
    if (nvs_open(name, readOnly ? NVS_READONLY : NVS_READWRITE, &_h) != ESP_OK) return false;
    _started = true;
    return true;
}

void Preferences::end() {
    if (!_started) return;
    nvs_close(_h);
    _started = false;
}

bool Preferences::clear() { return _started && !_readOnly && nvs_erase_all(_h) == ESP_OK; }
bool Preferences::remove(const char *key) { return _started && !_readOnly && nvs_erase_key(_h, key) == ESP_OK; }

#define PUT(NAME, C, FN)                                                         \
    size_t Preferences::NAME(const char *key, C v) {                             \
        if (!_started || !key || _readOnly) return 0;                            \
        return FN(_h, key, v) == ESP_OK ? sizeof(C) : 0;                         \
    }
PUT(putChar, int8_t, nvs_set_i8)
PUT(putUChar, uint8_t, nvs_set_u8)
PUT(putShort, int16_t, nvs_set_i16)
PUT(putUShort, uint16_t, nvs_set_u16)
PUT(putInt, int32_t, nvs_set_i32)
PUT(putUInt, uint32_t, nvs_set_u32)
PUT(putLong64, int64_t, nvs_set_i64)
PUT(putULong64, uint64_t, nvs_set_u64)

size_t Preferences::putString(const char *key, const char *v) {
    if (!_started || !key || !v || _readOnly) return 0;
    return nvs_set_str(_h, key, v) == ESP_OK ? strlen(v) : 0;
}

size_t Preferences::putBytes(const char *key, const void *v, size_t len) {
    if (!_started || !key || !v || !len || _readOnly) return 0;
    return nvs_set_blob(_h, key, v, len) == ESP_OK ? len : 0;
}

bool Preferences::isKey(const char *key) { return getType(key) != PT_INVALID; }

PreferenceType Preferences::getType(const char *key) {
    nvs_type_t t;
    if (!_started || !key || nvs_find_key(_h, key, &t) != ESP_OK) return PT_INVALID;
    switch (t) {
        case NVS_TYPE_I8: return PT_I8;
        case NVS_TYPE_U8: return PT_U8;
        case NVS_TYPE_I16: return PT_I16;
        case NVS_TYPE_U16: return PT_U16;
        case NVS_TYPE_I32: return PT_I32;
        case NVS_TYPE_U32: return PT_U32;
        case NVS_TYPE_I64: return PT_I64;
        case NVS_TYPE_U64: return PT_U64;
        case NVS_TYPE_STR: return PT_STR;
        case NVS_TYPE_BLOB: return PT_BLOB;
        default: return PT_INVALID;
    }
}

#define GET(NAME, C, FN)                                                         \
    C Preferences::NAME(const char *key, C d) {                                  \
        C v = d;                                                                 \
        if (_started && key) FN(_h, key, &v);                                    \
        return v;                                                                \
    }
GET(getChar, int8_t, nvs_get_i8)
GET(getUChar, uint8_t, nvs_get_u8)
GET(getShort, int16_t, nvs_get_i16)
GET(getUShort, uint16_t, nvs_get_u16)
GET(getInt, int32_t, nvs_get_i32)
GET(getUInt, uint32_t, nvs_get_u32)
GET(getLong64, int64_t, nvs_get_i64)
GET(getULong64, uint64_t, nvs_get_u64)

float Preferences::getFloat(const char *key, float d) {
    float v = d;
    return getBytes(key, &v, sizeof v) == sizeof v ? v : d;
}

double Preferences::getDouble(const char *key, double d) {
    double v = d;
    return getBytes(key, &v, sizeof v) == sizeof v ? v : d;
}

size_t Preferences::getString(const char *key, char *v, size_t maxLen) {
    size_t len = 0;
    if (!_started || !key || !v || !maxLen) return 0;
    if (nvs_get_str(_h, key, nullptr, &len) != ESP_OK || len > maxLen) return 0;
    return nvs_get_str(_h, key, v, &len) == ESP_OK ? len : 0;
}

String Preferences::getString(const char *key, String d) {
    size_t len = 0;
    if (!_started || !key || nvs_get_str(_h, key, nullptr, &len) != ESP_OK || len == 0) return d;
    std::string buf(len, '\0');
    if (nvs_get_str(_h, key, &buf[0], &len) != ESP_OK) return d;
    return String(buf.c_str());
}

size_t Preferences::getBytesLength(const char *key) {
    size_t len = 0;
    if (!_started || !key || nvs_get_blob(_h, key, nullptr, &len) != ESP_OK) return 0;
    return len;
}

size_t Preferences::getBytes(const char *key, void *buf, size_t maxLen) {
    size_t len = getBytesLength(key);
    if (!len || !buf || len > maxLen) return 0;
    return nvs_get_blob(_h, key, buf, &len) == ESP_OK ? len : 0;
}

size_t Preferences::freeEntries() {
    nvs_stats_t s;
    return nvs_get_stats(nullptr, &s) == ESP_OK ? s.free_entries : 0;
}
