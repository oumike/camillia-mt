#pragma once
#include <stddef.h>
#include <stdint.h>

// Declarations only: pure map helpers compile, accidental filesystem use fails
// to link rather than pretending a storage operation succeeded.
#define FILE_READ "r"
enum SeekMode { SeekSet };
namespace fs {
class File {
public:
    explicit operator bool() const;
    size_t size() const;
    size_t read(uint8_t *out, size_t len);
    bool seek(uint32_t position, SeekMode mode);
    void close();
};
class FS {
public:
    bool exists(const char *path);
    bool mkdir(const char *path);
    File open(const char *path, const char *mode);
};
}
using fs::File;
