// SPDX-License-Identifier: GPL-3.0-or-later
// fs::FS over a host directory. See shim/FS.h.
#include <FS.h>
#include <LittleFS.h>

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <string>

#include "czero_paths.h"

namespace fs {

struct FileImpl {
    std::string hostPath;
    std::string fwPath;   // as the firmware named it, for path()/name()
    std::string mode;
    FILE *fp = nullptr;
    DIR *dir = nullptr;
    ~FileImpl() {
        if (fp) fclose(fp);
        if (dir) closedir(dir);
    }
};

}  // namespace fs

const std::string &czeroDataDir() {
    static std::string dir = [] {
        if (const char *e = getenv("CAMILLIA_HOME"); e && *e) return std::string(e);
        const char *home = getenv("HOME");
        return std::string(home && *home ? home : "/tmp") + "/.config/camillia-app";
    }();
    return dir;
}

bool czeroMkdirs(const std::string &path) {
    std::string cur;
    size_t i = 0;
    while (i <= path.size()) {
        size_t j = path.find('/', i);
        if (j == std::string::npos) j = path.size();
        cur = path.substr(0, j);
        if (!cur.empty()) ::mkdir(cur.c_str(), 0700);
        i = j + 1;
    }
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

namespace fs {

static std::string normalise(const char *path) {
    std::string p = path ? path : "/";
    if (p.empty() || p[0] != '/') p = "/" + p;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

const std::string &FS::root() {
    if (_root.empty()) {
        _root = czeroDataDir() + "/" + _suffix;
        czeroMkdirs(_root);
    }
    return _root;
}

std::string FS::hostPath(const char *path) {
    std::string p = normalise(path);
    // Paths come from the firmware itself, but keep them inside the root.
    if (p.find("/../") != std::string::npos || (p.size() >= 3 && p.compare(p.size() - 3, 3, "/..") == 0))
        return root() + "/.invalid";
    return p == "/" ? root() : root() + p;
}

File FS::open(const char *path, const char *mode, const bool create) {
    std::string host = hostPath(path);
    std::string m = mode ? mode : "r";
    struct stat st;
    const bool exists = ::stat(host.c_str(), &st) == 0;
    auto impl = std::make_shared<FileImpl>();
    impl->hostPath = host;
    impl->fwPath = normalise(path);
    impl->mode = m;
    if (exists && S_ISDIR(st.st_mode)) {
        impl->dir = opendir(host.c_str());
        return impl->dir ? File(impl) : File();
    }
    if (m[0] == 'r' && !exists) return File();
    if (m[0] != 'r') {
        size_t slash = host.rfind('/');
        if (create && slash != std::string::npos) czeroMkdirs(host.substr(0, slash));
    }
    // fopen modes; "b" is meaningless on Linux but harmless.
    impl->fp = fopen(host.c_str(), m.c_str());
    return impl->fp ? File(impl) : File();
}

bool FS::exists(const char *path) {
    struct stat st;
    return ::stat(hostPath(path).c_str(), &st) == 0;
}

bool FS::remove(const char *path) { return ::unlink(hostPath(path).c_str()) == 0; }
bool FS::rename(const char *from, const char *to) {
    return ::rename(hostPath(from).c_str(), hostPath(to).c_str()) == 0;
}
bool FS::mkdir(const char *path) {
    std::string h = hostPath(path);
    return ::mkdir(h.c_str(), 0700) == 0 || errno == EEXIST;
}
bool FS::rmdir(const char *path) { return ::rmdir(hostPath(path).c_str()) == 0; }

size_t File::write(uint8_t c) { return write(&c, 1); }
size_t File::write(const uint8_t *buf, size_t size) {
    if (!_p || !_p->fp) return 0;
    return fwrite(buf, 1, size, _p->fp);
}
int File::available() {
    if (!_p || !_p->fp) return 0;
    long pos = ftell(_p->fp);
    struct stat st;
    if (pos < 0 || fstat(fileno(_p->fp), &st) != 0) return 0;
    fflush(_p->fp);
    long left = (long)st.st_size - pos;
    return left > 0 ? (int)std::min<long>(left, 0x7fffffff) : 0;
}
int File::read() {
    if (!_p || !_p->fp) return -1;
    int c = fgetc(_p->fp);
    return c == EOF ? -1 : c;
}
int File::peek() {
    if (!_p || !_p->fp) return -1;
    int c = fgetc(_p->fp);
    if (c == EOF) return -1;
    ungetc(c, _p->fp);
    return c;
}
void File::flush() {
    if (_p && _p->fp) fflush(_p->fp);
}
size_t File::read(uint8_t *buf, size_t size) {
    if (!_p || !_p->fp) return 0;
    return fread(buf, 1, size, _p->fp);
}
bool File::seek(uint32_t pos, SeekMode mode) {
    if (!_p || !_p->fp) return false;
    int whence = mode == SeekCur ? SEEK_CUR : (mode == SeekEnd ? SEEK_END : SEEK_SET);
    return fseek(_p->fp, (long)pos, whence) == 0;
}
size_t File::position() const {
    if (!_p || !_p->fp) return 0;
    long p = ftell(_p->fp);
    return p < 0 ? 0 : (size_t)p;
}
size_t File::size() const {
    if (!_p) return 0;
    if (_p->fp) fflush(_p->fp);
    struct stat st;
    return ::stat(_p->hostPath.c_str(), &st) == 0 ? (size_t)st.st_size : 0;
}
void File::close() { _p.reset(); }
File::operator bool() const { return _p && (_p->fp || _p->dir); }
time_t File::getLastWrite() {
    struct stat st;
    return _p && ::stat(_p->hostPath.c_str(), &st) == 0 ? st.st_mtime : 0;
}
const char *File::path() const { return _p ? _p->fwPath.c_str() : nullptr; }
const char *File::name() const {
    if (!_p) return nullptr;
    size_t slash = _p->fwPath.rfind('/');
    return _p->fwPath.c_str() + (slash == std::string::npos ? 0 : slash + 1);
}
boolean File::isDirectory(void) { return _p && _p->dir; }

static struct dirent *nextEntry(DIR *d) {
    while (struct dirent *e = readdir(d)) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) return e;
    }
    return nullptr;
}

File File::openNextFile(const char *mode) {
    if (!_p || !_p->dir) return File();
    struct dirent *e = nextEntry(_p->dir);
    if (!e) return File();
    std::string child = (_p->fwPath == "/" ? "" : _p->fwPath) + "/" + e->d_name;
    std::string host = _p->hostPath + "/" + e->d_name;
    auto impl = std::make_shared<FileImpl>();
    impl->hostPath = host;
    impl->fwPath = child;
    impl->mode = mode ? mode : "r";
    struct stat st;
    if (::stat(host.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) impl->dir = opendir(host.c_str());
    else impl->fp = fopen(host.c_str(), impl->mode.c_str());
    return (impl->dir || impl->fp) ? File(impl) : File();
}
String File::getNextFileName(bool *isDir) {
    if (!_p || !_p->dir) return String();
    struct dirent *e = nextEntry(_p->dir);
    if (!e) return String();
    std::string child = (_p->fwPath == "/" ? "" : _p->fwPath) + "/" + e->d_name;
    if (isDir) {
        struct stat st;
        *isDir = ::stat((_p->hostPath + "/" + e->d_name).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }
    return String(child.c_str());
}
String File::getNextFileName(void) { return getNextFileName(nullptr); }
void File::rewindDirectory(void) {
    if (_p && _p->dir) rewinddir(_p->dir);
}

bool LittleFSFS::begin(bool, const char *, uint8_t, const char *) { return czeroMkdirs(root()); }
bool LittleFSFS::format() {
    // Not a partition here: "format" would be rm -rf of the user's files,
    // which nothing on this board needs.
    return false;
}
size_t LittleFSFS::totalBytes() {
    struct statvfs s;
    return statvfs(root().c_str(), &s) == 0 ? (size_t)s.f_blocks * s.f_frsize : 0;
}
size_t LittleFSFS::usedBytes() {
    struct statvfs s;
    return statvfs(root().c_str(), &s) == 0 ? (size_t)(s.f_blocks - s.f_bfree) * s.f_frsize : 0;
}

}  // namespace fs

fs::LittleFSFS LittleFS;
