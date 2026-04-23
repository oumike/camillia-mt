#pragma once
#include <Arduino.h>

void debugSetFlags(bool acksEnabled, bool messagesEnabled, bool gpsEnabled);

bool debugAcksEnabled();
bool debugMessagesEnabled();
bool debugGpsEnabled();

void debugLogAcks(const char *fmt, ...);
void debugLogMessages(const char *fmt, ...);
void debugLogGps(const char *fmt, ...);
