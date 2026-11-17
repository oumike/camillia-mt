// SPDX-License-Identifier: GPL-3.0-or-later
// ESP-IDF system calls, given their nearest Linux meaning.
#include <esp_idf_linux.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "czero_app.h"

namespace {

uint64_t memInfoKb(const char *field) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char line[128];
    uint64_t v = 0;
    const size_t n = strlen(field);
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, field, n) == 0 && line[n] == ':') {
            v = strtoull(line + n + 1, nullptr, 10);
            break;
        }
    }
    fclose(f);
    return v;
}

uint32_t clamp32(uint64_t v) { return v > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)v; }

uint32_t crc32(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

const esp_partition_t kAppPartition = {ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, 0, 0,
                                       "factory", false};

}  // namespace

extern "C" {

const char *esp_err_to_name(esp_err_t code) {
    switch (code) {
        case ESP_OK: return "ESP_OK";
        case ESP_FAIL: return "ESP_FAIL";
        case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_INVALID_SIZE: return "ESP_ERR_INVALID_SIZE";
        case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
        case ESP_ERR_NOT_SUPPORTED: return "ESP_ERR_NOT_SUPPORTED";
        case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
        case ESP_ERR_NVS_NOT_FOUND: return "ESP_ERR_NVS_NOT_FOUND";
        case ESP_ERR_NVS_TYPE_MISMATCH: return "ESP_ERR_NVS_TYPE_MISMATCH";
        case ESP_ERR_NVS_READ_ONLY: return "ESP_ERR_NVS_READ_ONLY";
        case ESP_ERR_NVS_NOT_ENOUGH_SPACE: return "ESP_ERR_NVS_NOT_ENOUGH_SPACE";
        case ESP_ERR_NVS_INVALID_NAME: return "ESP_ERR_NVS_INVALID_NAME";
        case ESP_ERR_NVS_INVALID_HANDLE: return "ESP_ERR_NVS_INVALID_HANDLE";
        case ESP_ERR_NVS_KEY_TOO_LONG: return "ESP_ERR_NVS_KEY_TOO_LONG";
        case ESP_ERR_NVS_INVALID_LENGTH: return "ESP_ERR_NVS_INVALID_LENGTH";
        default: return "ESP_ERR_UNKNOWN";
    }
}

esp_reset_reason_t esp_reset_reason(void) { return ESP_RST_POWERON; }
void esp_restart(void) { czeroRestart(); }
uint32_t esp_get_free_heap_size(void) { return clamp32(memInfoKb("MemAvailable") * 1024); }
uint32_t esp_get_minimum_free_heap_size(void) { return esp_get_free_heap_size(); }
const char *esp_get_idf_version(void) { return "linux"; }

// A stable, locally administered MAC from the machine id. Only labels and
// legacy fallbacks use it: the node number comes from the public key.
esp_err_t esp_efuse_mac_get_default(uint8_t *mac) {
    if (!mac) return ESP_ERR_INVALID_ARG;
    char id[64] = {0};
    if (FILE *f = fopen("/etc/machine-id", "r")) {
        if (!fgets(id, sizeof id, f)) id[0] = 0;
        fclose(f);
    }
    const uint32_t a = crc32(0, (const uint8_t *)id, strlen(id));
    const uint32_t b = crc32(a, (const uint8_t *)"camillia", 8);
    mac[0] = 0x02;
    mac[1] = (uint8_t)(b >> 8);
    mac[2] = (uint8_t)(a >> 24);
    mac[3] = (uint8_t)(a >> 16);
    mac[4] = (uint8_t)(a >> 8);
    mac[5] = (uint8_t)a;
    return ESP_OK;
}
esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t) { return esp_efuse_mac_get_default(mac); }

void esp_chip_info(esp_chip_info_t *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->cores = (uint8_t)sysconf(_SC_NPROCESSORS_ONLN);
}

uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len) {
    // ROM semantics: the caller's crc is not pre-inverted.
    return crc32(crc, buf, len);
}

void *heap_caps_malloc(size_t size, uint32_t) { return malloc(size); }
void *heap_caps_calloc(size_t n, size_t size, uint32_t) { return calloc(n, size); }
void *heap_caps_realloc(void *p, size_t size, uint32_t) { return realloc(p, size); }
void heap_caps_free(void *p) { free(p); }
size_t heap_caps_get_free_size(uint32_t) { return (size_t)memInfoKb("MemAvailable") * 1024; }
size_t heap_caps_get_total_size(uint32_t) { return (size_t)memInfoKb("MemTotal") * 1024; }
size_t heap_caps_get_largest_free_block(uint32_t) { return heap_caps_get_free_size(0); }
size_t heap_caps_get_minimum_free_size(uint32_t) { return heap_caps_get_free_size(0); }
void heap_caps_malloc_extmem_enable(size_t) {}
bool heap_caps_check_integrity_all(bool) { return true; }

// Sleep: the kernel and the launcher own power. Light sleep is declined so
// callers keep running; deep sleep ("power off") leaves the app.
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t) { return ESP_OK; }
esp_err_t esp_sleep_enable_ext0_wakeup(int, int) { return ESP_OK; }
esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t, int) { return ESP_OK; }
esp_err_t esp_sleep_enable_gpio_wakeup(void) { return ESP_OK; }
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_source_t) { return ESP_OK; }
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void) { return ESP_SLEEP_WAKEUP_UNDEFINED; }
bool esp_sleep_is_valid_wakeup_gpio(int) { return false; }
esp_err_t esp_light_sleep_start(void) { return ESP_ERR_NOT_SUPPORTED; }
void esp_deep_sleep_start(void) { czeroExit(0); }
esp_err_t esp_deep_sleep_enable_gpio_wakeup(uint64_t, int) { return ESP_OK; }

// No partitions or OTA slots: the app is updated by its installer.
const esp_partition_t *esp_partition_find_first(esp_partition_type_t, esp_partition_subtype_t, const char *) {
    return nullptr;
}
const esp_partition_t *esp_ota_get_running_partition(void) { return &kAppPartition; }
const esp_partition_t *esp_ota_get_boot_partition(void) { return &kAppPartition; }
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *) { return nullptr; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) { return ESP_OK; }

}  // extern "C"
