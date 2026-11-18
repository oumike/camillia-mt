// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Where the app keeps its state: $CAMILLIA_HOME, else ~/.config/camillia-app.
//   nvs/<namespace>   Preferences / NVS (app/nvs_linux.cpp)
//   fs/               the files an SD card holds on other boards (app/fs_linux.cpp)
#include <string>

const std::string &czeroDataDir();
// mkdir -p; true if the directory exists afterwards.
bool czeroMkdirs(const std::string &path);
