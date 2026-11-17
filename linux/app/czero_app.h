// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Process-level hooks for the CardputerZero app (app/main_linux.cpp).

// Kept so esp_restart() can exec the same binary with the same arguments.
void czeroSetArgv(int argc, char **argv);
// Leave the app cleanly (persist, release the radio, exit). Never returns.
[[noreturn]] void czeroExit(int code);
// Re-exec this binary: ESP.restart() on a device with no reset line.
[[noreturn]] void czeroRestart();
