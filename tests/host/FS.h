#pragma once
#include <stdint.h>
#include <stdlib.h>

// Compile persistence paths, but fail if a send-only test touches a filesystem.
#define FILE_WRITE "w"
class File {
public:
    explicit operator bool() const { abort(); }
    bool isDirectory() const { abort(); }
    File openNextFile() { abort(); }
    const char *name() const { abort(); }
    size_t size() const { abort(); }
    size_t read(uint8_t *, size_t) { abort(); }
    size_t write(const uint8_t *, size_t) { abort(); }
    void flush() { abort(); }
    void close() { abort(); }
};

namespace fs {
class FS {
public:
    File open(const char *, const char * = "r") { abort(); }
    bool exists(const char *) { abort(); }
    bool mkdir(const char *) { abort(); }
    bool rmdir(const char *) { abort(); }
    bool remove(const char *) { abort(); }
    bool rename(const char *, const char *) { abort(); }
};
}
