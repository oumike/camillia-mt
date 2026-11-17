// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// CardputerZero cap bring-up: 5 V to the cap, and the PI4IOE5V6408 P0 that
// gates the cap's RF switch. Both must be done before the SX1262 is touched.
bool linuxCapPowerOn();
bool linuxCapArmRfSwitch();
