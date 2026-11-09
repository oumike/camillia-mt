#pragma once

#include <Arduino.h>

void liveFeedAddPrefixed(const char *prefix,
                         const char *text,
                         uint16_t color,
                         uint32_t packetId = 0,
                         bool trackAck = false,
                         bool viaMqtt = false);

void liveFeedAddLine(const char *text, uint16_t color);
