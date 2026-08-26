#pragma once

bool squareIoBegin();
bool squareIoReady();
bool squareIoReadWakeButton(bool &pressed);

bool squareIoSetLcdPower(bool enabled);
bool squareIoSetLcdResetReleased(bool released);
bool squareIoSetTouchResetReleased(bool released);
bool squareIoSetGrovePower(bool enabled);
bool squareIoSetGnssPower(bool enabled);
bool squareIoSetGnssResetReleased(bool released);
bool squareIoSetUserLed(bool enabled);
bool squareIoSetUsbOtg(bool enabled);
bool squareIoSetAudioPaPower(bool enabled);
bool squareIoSetSdPower(bool enabled);
bool squareIoSetBatterySense(bool enabled);