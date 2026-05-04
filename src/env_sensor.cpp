#include "env_sensor.h"
#include "config.h"

#if defined(DEVICE_HELTEC_V4_EXPANSION)

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <math.h>

static Adafruit_BME280 gBme280;
static bool gTriedInit = false;
static bool gReady = false;
static uint8_t gI2cAddr = 0;

static TwoWire &envWire() {
#if (TOUCH_I2C_PORT == 1)
    return Wire1;
#else
    return Wire;
#endif
}

bool envBegin() {
    if (gTriedInit) return gReady;
    gTriedInit = true;

    TwoWire &w = envWire();
    w.begin(TOUCH_SDA, TOUCH_SCL, 400000U);

    const uint8_t candidates[] = { 0x76, 0x77 };
    for (size_t i = 0; i < sizeof(candidates); i++) {
        uint8_t addr = candidates[i];
        if (gBme280.begin(addr, &w)) {
            gReady = true;
            gI2cAddr = addr;
            break;
        }
    }

    return gReady;
}

bool envHasSensor() {
    return gReady;
}

bool envRead(EnvReading &out) {
    if (!gReady) return false;

    float t = gBme280.readTemperature();
    float h = gBme280.readHumidity();
    float p = gBme280.readPressure() / 100.0f;
    if (isnan(t) || isnan(h) || isnan(p)) return false;

    out.temperatureC = t;
    out.humidityPct = h;
    out.pressureHpa = p;
    return true;
}

const char *envSensorName() {
    if (!gReady) return "none";
    return (gI2cAddr == 0x77) ? "BME280@0x77" : "BME280@0x76";
}

#else

bool envBegin() {
    return false;
}

bool envHasSensor() {
    return false;
}

bool envRead(EnvReading &out) {
    (void)out;
    return false;
}

const char *envSensorName() {
    return "none";
}

#endif
