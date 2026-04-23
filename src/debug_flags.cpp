#include "debug_flags.h"
#include <stdarg.h>
#include <stdio.h>

static bool sDebugAcks = false;
static bool sDebugMessages = false;
static bool sDebugGps = false;

void debugSetFlags(bool acksEnabled, bool messagesEnabled, bool gpsEnabled) {
    sDebugAcks = acksEnabled;
    sDebugMessages = messagesEnabled;
    sDebugGps = gpsEnabled;
}

bool debugAcksEnabled() {
    return sDebugAcks;
}

bool debugMessagesEnabled() {
    return sDebugMessages;
}

bool debugGpsEnabled() {
    return sDebugGps;
}

void debugLogAcks(const char *fmt, ...) {
    if (!sDebugAcks) return;
    va_list ap;
    va_start(ap, fmt);
    char buf[320];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    Serial.print(buf);
    va_end(ap);
}

void debugLogMessages(const char *fmt, ...) {
    if (!sDebugMessages) return;
    va_list ap;
    va_start(ap, fmt);
    char buf[320];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    Serial.print(buf);
    va_end(ap);
}

void debugLogGps(const char *fmt, ...) {
    if (!sDebugGps) return;
    va_list ap;
    va_start(ap, fmt);
    char buf[320];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    Serial.print(buf);
    va_end(ap);
}
