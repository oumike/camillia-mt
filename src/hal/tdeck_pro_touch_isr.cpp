#include <Arduino.h>

DRAM_ATTR volatile bool g_tdeckProTouchIrq = false;

void IRAM_ATTR tdeckProTouchInterrupt() {
    g_tdeckProTouchIrq = true;
}