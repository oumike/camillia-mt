// SPDX-License-Identifier: GPL-3.0-or-later
// NVS on the CardputerZero: one file per namespace under <data dir>/nvs/.
// It keeps ESP-IDF's semantics where the firmware can see them -- typed
// entries, type-mismatched reads fail, 15-character key and namespace names,
// iteration over every namespace -- so the firmware's settings code runs
// unchanged. Every write rewrites its namespace file atomically (temp file +
// rename), as a flash commit would.
//
// File format: "CNVS1\n", then records of
//   u8 type, u8 keyLen, key bytes, u32 valueLen (LE), value bytes.
#include <esp_idf_linux.h>

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "czero_paths.h"

namespace {

struct Entry {
    nvs_type_t type;
    std::vector<uint8_t> data;
};
typedef std::map<std::string, Entry> Namespace;

std::recursive_mutex gMu;
std::map<std::string, Namespace> gCache;   // loaded namespaces
std::map<nvs_handle_t, std::pair<std::string, bool>> gHandles;  // ns, readOnly
nvs_handle_t gNextHandle = 1;

const char kMagic[] = "CNVS1\n";

std::string nvsDir() {
    static std::string d = czeroDataDir() + "/nvs";
    return d;
}

bool validName(const char *s) { return s && *s && strlen(s) < NVS_KEY_NAME_MAX_SIZE; }

Namespace &load(const std::string &ns) {
    auto it = gCache.find(ns);
    if (it != gCache.end()) return it->second;
    Namespace &out = gCache[ns];
    FILE *f = fopen((nvsDir() + "/" + ns).c_str(), "rb");
    if (!f) return out;
    char magic[sizeof kMagic - 1];
    if (fread(magic, 1, sizeof magic, f) != sizeof magic || memcmp(magic, kMagic, sizeof magic) != 0) {
        fprintf(stderr, "[nvs] %s: not a namespace file, ignoring\n", ns.c_str());
        fclose(f);
        return out;
    }
    for (;;) {
        uint8_t type, klen;
        if (fread(&type, 1, 1, f) != 1 || fread(&klen, 1, 1, f) != 1) break;
        std::string key(klen, '\0');
        uint8_t lenb[4];
        if (fread(&key[0], 1, klen, f) != klen || fread(lenb, 1, 4, f) != 4) break;
        uint32_t vlen = lenb[0] | (lenb[1] << 8) | (lenb[2] << 16) | ((uint32_t)lenb[3] << 24);
        if (vlen > 16u * 1024 * 1024) break;
        Entry e{(nvs_type_t)type, std::vector<uint8_t>(vlen)};
        if (vlen && fread(e.data.data(), 1, vlen, f) != vlen) break;
        out[key] = std::move(e);
    }
    fclose(f);
    return out;
}

bool save(const std::string &ns) {
    const Namespace &n = load(ns);
    czeroMkdirs(nvsDir());
    const std::string path = nvsDir() + "/" + ns, tmp = path + ".tmp";
    if (n.empty()) {
        unlink(path.c_str());
        return true;
    }
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(kMagic, 1, sizeof kMagic - 1, f) == sizeof kMagic - 1;
    for (const auto &kv : n) {
        const uint8_t type = (uint8_t)kv.second.type, klen = (uint8_t)kv.first.size();
        const uint32_t vlen = (uint32_t)kv.second.data.size();
        const uint8_t lenb[4] = {(uint8_t)vlen, (uint8_t)(vlen >> 8), (uint8_t)(vlen >> 16), (uint8_t)(vlen >> 24)};
        ok = ok && fwrite(&type, 1, 1, f) == 1 && fwrite(&klen, 1, 1, f) == 1 &&
             fwrite(kv.first.data(), 1, klen, f) == klen && fwrite(lenb, 1, 4, f) == 4 &&
             (vlen == 0 || fwrite(kv.second.data.data(), 1, vlen, f) == vlen);
    }
    ok = (fflush(f) == 0) && ok;
    if (ok) fsync(fileno(f));
    ok = (fclose(f) == 0) && ok;
    if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());
        fprintf(stderr, "[nvs] %s: write failed\n", ns.c_str());
        return false;
    }
    return true;
}

std::vector<std::string> allNamespaces() {
    std::vector<std::string> out;
    DIR *d = opendir(nvsDir().c_str());
    if (!d) return out;
    while (struct dirent *e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == ".." || n.size() >= NVS_NS_NAME_MAX_SIZE || n.find('.') != std::string::npos) continue;
        out.push_back(n);
    }
    closedir(d);
    return out;
}

esp_err_t handleNs(nvs_handle_t h, bool forWrite, std::string &ns) {
    auto it = gHandles.find(h);
    if (it == gHandles.end()) return ESP_ERR_NVS_INVALID_HANDLE;
    if (forWrite && it->second.second) return ESP_ERR_NVS_READ_ONLY;
    ns = it->second.first;
    return ESP_OK;
}

esp_err_t setRaw(nvs_handle_t h, const char *key, nvs_type_t type, const void *v, size_t len) {
    std::lock_guard<std::recursive_mutex> lk(gMu);
    std::string ns;
    if (esp_err_t e = handleNs(h, true, ns)) return e;
    if (!key || !*key) return ESP_ERR_NVS_INVALID_NAME;
    if (strlen(key) >= NVS_KEY_NAME_MAX_SIZE) return ESP_ERR_NVS_KEY_TOO_LONG;
    Namespace &n = load(ns);
    Entry e{type, std::vector<uint8_t>((const uint8_t *)v, (const uint8_t *)v + len)};
    auto it = n.find(key);
    if (it != n.end() && it->second.type == e.type && it->second.data == e.data) return ESP_OK;
    n[key] = std::move(e);
    return save(ns) ? ESP_OK : ESP_FAIL;
}

esp_err_t getRaw(nvs_handle_t h, const char *key, nvs_type_t type, const Entry **out) {
    std::lock_guard<std::recursive_mutex> lk(gMu);
    std::string ns;
    if (esp_err_t e = handleNs(h, false, ns)) return e;
    if (!key || !*key) return ESP_ERR_NVS_INVALID_NAME;
    if (strlen(key) >= NVS_KEY_NAME_MAX_SIZE) return ESP_ERR_NVS_KEY_TOO_LONG;
    const Namespace &n = load(ns);
    auto it = n.find(key);
    if (it == n.end() || it->second.type != type) return ESP_ERR_NVS_NOT_FOUND;
    *out = &it->second;
    return ESP_OK;
}

template <typename T>
esp_err_t getInt(nvs_handle_t h, const char *key, nvs_type_t type, T *out) {
    std::lock_guard<std::recursive_mutex> lk(gMu);
    const Entry *e;
    if (esp_err_t r = getRaw(h, key, type, &e)) return r;
    if (e->data.size() != sizeof(T)) return ESP_ERR_NVS_NOT_FOUND;
    if (out) memcpy(out, e->data.data(), sizeof(T));
    return ESP_OK;
}

}  // namespace

struct nvs_opaque_iterator_t {
    std::vector<nvs_entry_info_t> items;
    size_t pos = 0;
};

extern "C" {

esp_err_t nvs_flash_init(void) {
    return czeroMkdirs(nvsDir()) ? ESP_OK : ESP_FAIL;
}

esp_err_t nvs_flash_erase(void) {
    std::lock_guard<std::recursive_mutex> lk(gMu);
    for (const auto &ns : allNamespaces()) unlink((nvsDir() + "/" + ns).c_str());
    gCache.clear();
    return ESP_OK;
}

esp_err_t nvs_get_stats(const char *, nvs_stats_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::recursive_mutex> lk(gMu);
    size_t used = 0, nss = 0;
    for (const auto &ns : allNamespaces()) {
        used += load(ns).size();
        nss++;
    }
    // Not flash: report a large, mostly-free store so nothing prunes for space.
    out->total_entries = 65536;
    out->used_entries = used;
    out->free_entries = out->total_entries - used;
    out->namespace_count = nss;
    return ESP_OK;
}

nvs_iterator_t nvs_entry_find(const char *, const char *ns, nvs_type_t type) {
    std::lock_guard<std::recursive_mutex> lk(gMu);
    auto *it = new nvs_opaque_iterator_t;
    std::vector<std::string> names = ns ? std::vector<std::string>{ns} : allNamespaces();
    for (const auto &n : names) {
        for (const auto &kv : load(n)) {
            if (type != NVS_TYPE_ANY && kv.second.type != type) continue;
            nvs_entry_info_t info{};
            snprintf(info.namespace_name, sizeof info.namespace_name, "%s", n.c_str());
            snprintf(info.key, sizeof info.key, "%s", kv.first.c_str());
            info.type = kv.second.type;
            it->items.push_back(info);
        }
    }
    if (it->items.empty()) {
        delete it;
        return nullptr;
    }
    return it;
}

nvs_iterator_t nvs_entry_next(nvs_iterator_t it) {
    if (!it) return nullptr;
    if (++it->pos >= it->items.size()) {
        delete it;   // IDF 4.4: next() releases the iterator when it runs out
        return nullptr;
    }
    return it;
}

void nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t *out) {
    if (it && out && it->pos < it->items.size()) *out = it->items[it->pos];
}

void nvs_release_iterator(nvs_iterator_t it) { delete it; }

esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!validName(ns) || strchr(ns, '/') || strchr(ns, '.')) return ESP_ERR_NVS_INVALID_NAME;
    std::lock_guard<std::recursive_mutex> lk(gMu);
    // IDF: opening a namespace that doesn't exist read-only fails.
    if (mode == NVS_READONLY && load(ns).empty()) {
        struct stat st;
        if (stat((nvsDir() + "/" + ns).c_str(), &st) != 0) return ESP_ERR_NVS_NOT_FOUND;
    }
    *out = gNextHandle++;
    gHandles[*out] = {ns, mode == NVS_READONLY};
    return ESP_OK;
}

void nvs_close(nvs_handle_t h) {
    std::lock_guard<std::recursive_mutex> lk(gMu);
    gHandles.erase(h);
}

esp_err_t nvs_commit(nvs_handle_t h) {
    std::lock_guard<std::recursive_mutex> lk(gMu);
    std::string ns;
    return handleNs(h, false, ns);
}

esp_err_t nvs_erase_all(nvs_handle_t h) {
    std::lock_guard<std::recursive_mutex> lk(gMu);
    std::string ns;
    if (esp_err_t e = handleNs(h, true, ns)) return e;
    load(ns).clear();
    return save(ns) ? ESP_OK : ESP_FAIL;
}

esp_err_t nvs_erase_key(nvs_handle_t h, const char *key) {
    std::lock_guard<std::recursive_mutex> lk(gMu);
    std::string ns;
    if (esp_err_t e = handleNs(h, true, ns)) return e;
    if (!key) return ESP_ERR_NVS_INVALID_NAME;
    if (load(ns).erase(key) == 0) return ESP_ERR_NVS_NOT_FOUND;
    return save(ns) ? ESP_OK : ESP_FAIL;
}

esp_err_t nvs_find_key(nvs_handle_t h, const char *key, nvs_type_t *out) {
    std::lock_guard<std::recursive_mutex> lk(gMu);
    std::string ns;
    if (esp_err_t e = handleNs(h, false, ns)) return e;
    if (!key) return ESP_ERR_NVS_INVALID_NAME;
    const Namespace &n = load(ns);
    auto it = n.find(key);
    if (it == n.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (out) *out = it->second.type;
    return ESP_OK;
}

esp_err_t nvs_get_used_entry_count(nvs_handle_t h, size_t *used) {
    std::lock_guard<std::recursive_mutex> lk(gMu);
    std::string ns;
    if (esp_err_t e = handleNs(h, false, ns)) return e;
    if (used) *used = load(ns).size();
    return ESP_OK;
}

#define NVS_INT(T, C, TYPE)                                                              \
    esp_err_t nvs_get_##T(nvs_handle_t h, const char *key, C *out) {                     \
        return getInt(h, key, TYPE, out);                                                \
    }                                                                                    \
    esp_err_t nvs_set_##T(nvs_handle_t h, const char *key, C v) {                        \
        return setRaw(h, key, TYPE, &v, sizeof v);                                       \
    }
NVS_INT(u8, uint8_t, NVS_TYPE_U8)
NVS_INT(i8, int8_t, NVS_TYPE_I8)
NVS_INT(u16, uint16_t, NVS_TYPE_U16)
NVS_INT(i16, int16_t, NVS_TYPE_I16)
NVS_INT(u32, uint32_t, NVS_TYPE_U32)
NVS_INT(i32, int32_t, NVS_TYPE_I32)
NVS_INT(u64, uint64_t, NVS_TYPE_U64)
NVS_INT(i64, int64_t, NVS_TYPE_I64)

esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *len) {
    std::lock_guard<std::recursive_mutex> lk(gMu);
    const Entry *e;
    if (esp_err_t r = getRaw(h, key, NVS_TYPE_STR, &e)) return r;
    if (!len) return ESP_ERR_INVALID_ARG;
    const size_t need = e->data.size();   // stored with its NUL
    if (!out) {
        *len = need;
        return ESP_OK;
    }
    if (*len < need) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out, e->data.data(), need);
    *len = need;
    return ESP_OK;
}

esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *v) {
    if (!v) return ESP_ERR_INVALID_ARG;
    return setRaw(h, key, NVS_TYPE_STR, v, strlen(v) + 1);
}

esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len) {
    std::lock_guard<std::recursive_mutex> lk(gMu);
    const Entry *e;
    if (esp_err_t r = getRaw(h, key, NVS_TYPE_BLOB, &e)) return r;
    if (!len) return ESP_ERR_INVALID_ARG;
    const size_t need = e->data.size();
    if (!out) {
        *len = need;
        return ESP_OK;
    }
    if (*len < need) return ESP_ERR_NVS_INVALID_LENGTH;
    if (need) memcpy(out, e->data.data(), need);
    *len = need;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *v, size_t len) {
    if (!v && len) return ESP_ERR_INVALID_ARG;
    return setRaw(h, key, NVS_TYPE_BLOB, v, len);
}

}  // extern "C"
