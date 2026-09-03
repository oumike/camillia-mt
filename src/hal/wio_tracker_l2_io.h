#pragma once

bool wioTrackerL2IoBegin();
bool wioTrackerL2IoReady();
bool wioTrackerL2IoReadWakeButton(bool &pressed);

bool wioTrackerL2IoSetLcdPower(bool enabled);
bool wioTrackerL2IoSetLcdResetReleased(bool released);
bool wioTrackerL2IoSetTouchResetReleased(bool released);
bool wioTrackerL2IoSetGrovePower(bool enabled);
bool wioTrackerL2IoSetGnssPower(bool enabled);
bool wioTrackerL2IoSetGnssResetReleased(bool released);
bool wioTrackerL2IoSetUserLed(bool enabled);
bool wioTrackerL2IoSetUsbOtg(bool enabled);
bool wioTrackerL2IoSetAudioPaPower(bool enabled);
bool wioTrackerL2IoSetSdPower(bool enabled);
bool wioTrackerL2IoSetBatterySense(bool enabled);