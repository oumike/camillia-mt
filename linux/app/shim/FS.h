// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The arduino-esp32 fs::FS / fs::File API over a directory on the Linux
// filesystem (app/fs_linux.cpp). Paths are the firmware's ("/camillia/...")
// and resolve under the FS instance's root.
#include <memory>
#include <string>
#include <time.h>

#include "Arduino.h"

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

namespace fs {

enum SeekMode { SeekSet = 0, SeekCur = 1, SeekEnd = 2 };

struct FileImpl;
typedef std::shared_ptr<FileImpl> FileImplPtr;

class File : public Stream {
public:
    File(FileImplPtr p = FileImplPtr()) : _p(p) {}
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buf, size_t size) override;
    using Print::write;
    int available() override;
    int read() override;
    int peek() override;
    void flush() override;
    size_t read(uint8_t *buf, size_t size);
    size_t readBytes(char *buffer, size_t length) { return read((uint8_t *)buffer, length); }
    bool seek(uint32_t pos, SeekMode mode);
    bool seek(uint32_t pos) { return seek(pos, SeekSet); }
    size_t position() const;
    size_t size() const;
    bool setBufferSize(size_t) { return true; }
    void close();
    operator bool() const;
    time_t getLastWrite();
    const char *path() const;
    const char *name() const;
    boolean isDirectory(void);
    File openNextFile(const char *mode = FILE_READ);
    String getNextFileName(void);
    String getNextFileName(bool *isDir);
    void rewindDirectory(void);

private:
    FileImplPtr _p;
};

class FS {
public:
    explicit FS(const char *rootSuffix) : _suffix(rootSuffix) {}
    File open(const char *path, const char *mode = FILE_READ, const bool create = false);
    File open(const String &path, const char *mode = FILE_READ, const bool create = false) {
        return open(path.c_str(), mode, create);
    }
    bool exists(const char *path);
    bool exists(const String &path) { return exists(path.c_str()); }
    bool remove(const char *path);
    bool remove(const String &path) { return remove(path.c_str()); }
    bool rename(const char *from, const char *to);
    bool rename(const String &from, const String &to) { return rename(from.c_str(), to.c_str()); }
    bool mkdir(const char *path);
    bool mkdir(const String &path) { return mkdir(path.c_str()); }
    bool rmdir(const char *path);
    bool rmdir(const String &path) { return rmdir(path.c_str()); }
    // Host path for a firmware path; the root is created on first use.
    std::string hostPath(const char *path);
    const std::string &root();

protected:
    const char *_suffix;
    std::string _root;
};

}  // namespace fs

using fs::File;
using fs::FS;
using fs::SeekCur;
using fs::SeekEnd;
using fs::SeekMode;
using fs::SeekSet;
