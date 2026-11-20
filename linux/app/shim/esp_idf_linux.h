// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The ESP-IDF calls the shared sources make, for DEVICE_CARDPUTER_ZERO.
// Mirrors IDF 4.4 (arduino-esp32 2.0.17), the version the S3 builds use, so
// the firmware's own version checks take the same branches. Everything with a
// Linux meaning is implemented in app/esp_linux.cpp and app/nvs_linux.cpp; the
// flash, OTA, sleep and GPIO-wakeup calls report "not supported".
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define ESP_IDF_VERSION_MAJOR 4
#define ESP_IDF_VERSION_MINOR 4
#define ESP_IDF_VERSION_PATCH 7
#define ESP_IDF_VERSION_VAL(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(4, 4, 7)

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NVS_BASE 0x1100
#define ESP_ERR_NVS_NOT_INITIALIZED (ESP_ERR_NVS_BASE + 0x01)
#define ESP_ERR_NVS_NOT_FOUND (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_TYPE_MISMATCH (ESP_ERR_NVS_BASE + 0x03)
#define ESP_ERR_NVS_READ_ONLY (ESP_ERR_NVS_BASE + 0x04)
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE (ESP_ERR_NVS_BASE + 0x05)
#define ESP_ERR_NVS_INVALID_NAME (ESP_ERR_NVS_BASE + 0x06)
#define ESP_ERR_NVS_INVALID_HANDLE (ESP_ERR_NVS_BASE + 0x07)
#define ESP_ERR_NVS_KEY_TOO_LONG (ESP_ERR_NVS_BASE + 0x09)
#define ESP_ERR_NVS_INVALID_LENGTH (ESP_ERR_NVS_BASE + 0x0c)
#define ESP_ERR_NVS_NO_FREE_PAGES (ESP_ERR_NVS_BASE + 0x0d)
#define ESP_ERR_NVS_NEW_VERSION_FOUND (ESP_ERR_NVS_BASE + 0x10)
#define ESP_ERROR_CHECK(x) ((void)(x))

typedef enum {
    ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC, ESP_RST_INT_WDT,
    ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO,
} esp_reset_reason_t;

typedef enum {
    ESP_MAC_WIFI_STA, ESP_MAC_WIFI_SOFTAP, ESP_MAC_BT, ESP_MAC_ETH, ESP_MAC_IEEE802154,
    ESP_MAC_BASE, ESP_MAC_EFUSE_FACTORY, ESP_MAC_EFUSE_CUSTOM,
} esp_mac_type_t;

typedef enum { CHIP_ESP32 = 1, CHIP_ESP32S2 = 2, CHIP_ESP32S3 = 9, CHIP_ESP32C3 = 5 } esp_chip_model_t;
typedef struct {
    esp_chip_model_t model;
    uint32_t features;
    uint16_t revision;
    uint8_t cores;
} esp_chip_info_t;
#define CHIP_FEATURE_EMB_FLASH (1 << 0)
#define CHIP_FEATURE_WIFI_BGN (1 << 1)
#define CHIP_FEATURE_BLE (1 << 4)
#define CHIP_FEATURE_BT (1 << 5)
#define CHIP_FEATURE_EMB_PSRAM (1 << 6)

#define MALLOC_CAP_EXEC (1 << 0)
#define MALLOC_CAP_32BIT (1 << 1)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)

typedef enum {
    ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_ALL, ESP_SLEEP_WAKEUP_EXT0, ESP_SLEEP_WAKEUP_EXT1,
    ESP_SLEEP_WAKEUP_TIMER, ESP_SLEEP_WAKEUP_TOUCHPAD, ESP_SLEEP_WAKEUP_ULP, ESP_SLEEP_WAKEUP_GPIO,
    ESP_SLEEP_WAKEUP_UART,
} esp_sleep_source_t;
typedef esp_sleep_source_t esp_sleep_wakeup_cause_t;

typedef enum { ESP_PARTITION_TYPE_APP = 0, ESP_PARTITION_TYPE_DATA = 1, ESP_PARTITION_TYPE_ANY = 0xff } esp_partition_type_t;
typedef enum {
    ESP_PARTITION_SUBTYPE_APP_FACTORY = 0, ESP_PARTITION_SUBTYPE_APP_OTA_0 = 0x10,
    ESP_PARTITION_SUBTYPE_APP_OTA_1 = 0x11, ESP_PARTITION_SUBTYPE_APP_OTA_2 = 0x12,
    ESP_PARTITION_SUBTYPE_APP_TEST = 0x20,
    ESP_PARTITION_SUBTYPE_DATA_OTA = 0x00, ESP_PARTITION_SUBTYPE_DATA_NVS = 0x02,
    ESP_PARTITION_SUBTYPE_DATA_SPIFFS = 0x82, ESP_PARTITION_SUBTYPE_DATA_FAT = 0x81,
    ESP_PARTITION_SUBTYPE_ANY = 0xff,
} esp_partition_subtype_t;
typedef struct {
    esp_partition_type_t type;
    esp_partition_subtype_t subtype;
    uint32_t address;
    uint32_t size;
    char label[17];
    bool encrypted;
} esp_partition_t;

// NVS (IDF 4.4 API shapes).
typedef uint32_t nvs_handle_t;
typedef nvs_handle_t nvs_handle;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
typedef enum {
    NVS_TYPE_U8 = 0x01, NVS_TYPE_I8 = 0x11, NVS_TYPE_U16 = 0x02, NVS_TYPE_I16 = 0x12,
    NVS_TYPE_U32 = 0x04, NVS_TYPE_I32 = 0x14, NVS_TYPE_U64 = 0x08, NVS_TYPE_I64 = 0x18,
    NVS_TYPE_STR = 0x21, NVS_TYPE_BLOB = 0x42, NVS_TYPE_ANY = 0xff,
} nvs_type_t;
#define NVS_KEY_NAME_MAX_SIZE 16
#define NVS_NS_NAME_MAX_SIZE 16
#define NVS_DEFAULT_PART_NAME "nvs"
typedef struct {
    char namespace_name[16];
    char key[16];
    nvs_type_t type;
} nvs_entry_info_t;
typedef struct {
    size_t used_entries;
    size_t free_entries;
    size_t total_entries;
    size_t namespace_count;
} nvs_stats_t;
typedef struct nvs_opaque_iterator_t *nvs_iterator_t;

#ifdef __cplusplus
extern "C" {
#endif
const char *esp_err_to_name(esp_err_t code);
esp_reset_reason_t esp_reset_reason(void);
void esp_restart(void) __attribute__((noreturn));
uint32_t esp_get_free_heap_size(void);
uint32_t esp_get_minimum_free_heap_size(void);
const char *esp_get_idf_version(void);
esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type);
esp_err_t esp_efuse_mac_get_default(uint8_t *mac);
void esp_chip_info(esp_chip_info_t *out);
uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len);
int64_t esp_timer_get_time(void);

void *heap_caps_malloc(size_t size, uint32_t caps);
void *heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void *heap_caps_realloc(void *p, size_t size, uint32_t caps);
void heap_caps_free(void *p);
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_total_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);
size_t heap_caps_get_minimum_free_size(uint32_t caps);
void heap_caps_malloc_extmem_enable(size_t limit);
bool heap_caps_check_integrity_all(bool print_errors);

esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us);
esp_err_t esp_sleep_enable_ext0_wakeup(int gpio, int level);
esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t mask, int mode);
esp_err_t esp_sleep_enable_gpio_wakeup(void);
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_source_t source);
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void);
bool esp_sleep_is_valid_wakeup_gpio(int gpio);
esp_err_t esp_light_sleep_start(void);
void esp_deep_sleep_start(void) __attribute__((noreturn));
esp_err_t esp_deep_sleep_enable_gpio_wakeup(uint64_t mask, int mode);

const esp_partition_t *esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                                const char *label);
const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_ota_get_boot_partition(void);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *start);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);

esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
esp_err_t nvs_get_stats(const char *part, nvs_stats_t *out);
nvs_iterator_t nvs_entry_find(const char *part, const char *ns, nvs_type_t type);
nvs_iterator_t nvs_entry_next(nvs_iterator_t it);
void nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t *out);
void nvs_release_iterator(nvs_iterator_t it);
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *out);
void nvs_close(nvs_handle_t h);
esp_err_t nvs_commit(nvs_handle_t h);
esp_err_t nvs_erase_all(nvs_handle_t h);
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key);
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *out);
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t v);
esp_err_t nvs_get_i8(nvs_handle_t h, const char *key, int8_t *out);
esp_err_t nvs_set_i8(nvs_handle_t h, const char *key, int8_t v);
esp_err_t nvs_get_u16(nvs_handle_t h, const char *key, uint16_t *out);
esp_err_t nvs_set_u16(nvs_handle_t h, const char *key, uint16_t v);
esp_err_t nvs_get_i16(nvs_handle_t h, const char *key, int16_t *out);
esp_err_t nvs_set_i16(nvs_handle_t h, const char *key, int16_t v);
esp_err_t nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *out);
esp_err_t nvs_set_u32(nvs_handle_t h, const char *key, uint32_t v);
esp_err_t nvs_get_i32(nvs_handle_t h, const char *key, int32_t *out);
esp_err_t nvs_set_i32(nvs_handle_t h, const char *key, int32_t v);
esp_err_t nvs_get_u64(nvs_handle_t h, const char *key, uint64_t *out);
esp_err_t nvs_set_u64(nvs_handle_t h, const char *key, uint64_t v);
esp_err_t nvs_get_i64(nvs_handle_t h, const char *key, int64_t *out);
esp_err_t nvs_set_i64(nvs_handle_t h, const char *key, int64_t v);
esp_err_t nvs_find_key(nvs_handle_t h, const char *key, nvs_type_t *out);
esp_err_t nvs_get_used_entry_count(nvs_handle_t h, size_t *used);
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *len);
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *v);
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len);
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *v, size_t len);
#ifdef __cplusplus
}
#endif
