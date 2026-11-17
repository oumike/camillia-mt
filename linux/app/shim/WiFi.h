// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// arduino-esp32's WiFi object over the system's own network (app/net_linux.cpp).
// The OS (NetworkManager) owns the interfaces: mode() only records what the
// firmware asked for, so its Wi-Fi switch still gates network use, and
// "connected" means the system has a usable IPv4 address. Nothing here
// changes system networking.
#include <functional>

#include "Arduino.h"
#include "IPAddress.h"
#include "WiFiClient.h"

typedef enum { WIFI_MODE_NULL = 0, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA, WIFI_MODE_MAX } wifi_mode_t;
#define WIFI_OFF WIFI_MODE_NULL
#define WIFI_STA WIFI_MODE_STA
#define WIFI_AP WIFI_MODE_AP
#define WIFI_AP_STA WIFI_MODE_APSTA

typedef enum {
    WL_NO_SHIELD = 255,
    WL_IDLE_STATUS = 0,
    WL_NO_SSID_AVAIL,
    WL_SCAN_COMPLETED,
    WL_CONNECTED,
    WL_CONNECT_FAILED,
    WL_CONNECTION_LOST,
    WL_DISCONNECTED
} wl_status_t;

typedef enum { WIFI_PS_NONE, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM } wifi_ps_type_t;
typedef enum {
    WIFI_AUTH_OPEN = 0, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK, WIFI_AUTH_WPA_WPA2_PSK,
    WIFI_AUTH_WPA2_ENTERPRISE, WIFI_AUTH_WPA3_PSK, WIFI_AUTH_WPA2_WPA3_PSK, WIFI_AUTH_MAX
} wifi_auth_mode_t;
typedef enum {
    WIFI_POWER_19_5dBm = 78, WIFI_POWER_19dBm = 76, WIFI_POWER_18_5dBm = 74, WIFI_POWER_17dBm = 68,
    WIFI_POWER_15dBm = 60, WIFI_POWER_13dBm = 52, WIFI_POWER_11dBm = 44, WIFI_POWER_8_5dBm = 34,
    WIFI_POWER_7dBm = 28, WIFI_POWER_5dBm = 20, WIFI_POWER_2dBm = 8, WIFI_POWER_MINUS_1dBm = -4
} wifi_power_t;
#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED (-2)

typedef enum { ARDUINO_EVENT_WIFI_READY = 0, ARDUINO_EVENT_WIFI_STA_START, ARDUINO_EVENT_WIFI_AP_START, ARDUINO_EVENT_MAX } arduino_event_id_t;
typedef union { int dummy; } arduino_event_info_t;
typedef arduino_event_id_t WiFiEvent_t;
typedef arduino_event_info_t WiFiEventInfo_t;
typedef std::function<void(arduino_event_id_t, arduino_event_info_t)> WiFiEventFuncCb;
typedef void (*WiFiEventCb)(arduino_event_id_t);

class WiFiClass {
public:
    // Records STA mode; the system decides whether there is a connection.
    wl_status_t begin(const char *ssid, const char *pass = nullptr, int32_t channel = 0,
                      const uint8_t *bssid = nullptr, bool connect = true);
    wl_status_t status();
    bool isConnected() { return status() == WL_CONNECTED; }
    bool mode(wifi_mode_t m) { _mode = m; return true; }
    wifi_mode_t getMode() { return _mode; }
    bool disconnect(bool = false, bool = false) { return true; }
    bool setSleep(bool) { return true; }
    bool setSleep(wifi_ps_type_t) { return true; }
    bool setTxPower(wifi_power_t) { return true; }
    bool persistent(bool) { return true; }
    bool setAutoReconnect(bool) { return true; }
    bool setHostname(const char *) { return true; }
    const char *getHostname();
    int hostByName(const char *host, IPAddress &ip);
    IPAddress localIP();
    String macAddress();
    // No SoftAP: the OS owns the radio.
    IPAddress softAPIP() { return IPAddress(); }
    const char *softAPgetHostname() { return getHostname(); }
    bool softAP(const char *, const char * = nullptr, int = 1, int = 0, int = 4, bool = false) { return false; }
    bool softAPConfig(IPAddress, IPAddress, IPAddress) { return false; }
    bool softAPdisconnect(bool = false) { return true; }
    // The firmware does not manage networks here, so a scan finds none.
    int16_t scanNetworks(bool = false, bool = false, bool = false, uint32_t = 300, uint8_t = 0) { return 0; }
    int16_t scanComplete() { return 0; }
    void scanDelete() {}
    String SSID(uint8_t) const { return String(); }
    int32_t RSSI(uint8_t) { return 0; }
    wifi_auth_mode_t encryptionType(uint8_t) { return WIFI_AUTH_OPEN; }
    // The system's current Wi-Fi network; empty / 0 when not on Wi-Fi.
    String SSID() const;
    int32_t RSSI();
    int onEvent(WiFiEventFuncCb, arduino_event_id_t = ARDUINO_EVENT_MAX) { return 0; }
    int onEvent(WiFiEventCb, arduino_event_id_t = ARDUINO_EVENT_MAX) { return 0; }

private:
    wifi_mode_t _mode = WIFI_OFF;
};
extern WiFiClass WiFi;
