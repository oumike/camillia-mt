// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// "Internal flash" on the CardputerZero is a directory: <data dir>/fs.
#include "FS.h"

namespace fs {
class LittleFSFS : public FS {
public:
    LittleFSFS() : FS("fs") {}
    bool begin(bool formatOnFail = false, const char *basePath = "/littlefs", uint8_t maxOpenFiles = 10,
               const char *partitionLabel = "spiffs");
    void end() {}
    bool format();
    size_t totalBytes();
    size_t usedBytes();
};
}  // namespace fs

extern fs::LittleFSFS LittleFS;
