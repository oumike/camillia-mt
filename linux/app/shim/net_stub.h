// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Inert stand-ins for the arduino-esp32 network classes (WiFi, HTTPClient,
// WebServer, Update, PubSubClient). The CardputerZero app has no Wi-Fi path
// of its own yet, so WiFi.status() never reports a connection and every
// client fails to connect; the screens that use them show their offline state.
#include <functional>
#include <stdio.h>

#include "Arduino.h"

// ── IPAddress ────────────────────────────────────────────────────────────────
class IPAddress : public Printable {
public:
    IPAddress() : _a(0) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
        : _a((uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24)) {}
    IPAddress(uint32_t a) : _a(a) {}
    operator uint32_t() const { return _a; }
    bool operator==(const IPAddress &o) const { return _a == o._a; }
    bool operator!=(const IPAddress &o) const { return _a != o._a; }
    uint8_t operator[](int i) const { return (uint8_t)(_a >> (8 * i)); }
    bool fromString(const char *s) {
        unsigned a, b, c, d;
        if (!s || sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
            return false;
        *this = IPAddress(a, b, c, d);
        return true;
    }
    bool fromString(const String &s) { return fromString(s.c_str()); }
    String toString() const {
        char b[16];
        snprintf(b, sizeof b, "%u.%u.%u.%u", (*this)[0], (*this)[1], (*this)[2], (*this)[3]);
        return String(b);
    }
    size_t printTo(Print &p) const override { return p.print(toString()); }

private:
    uint32_t _a;
};
static const IPAddress INADDR_NONE(0, 0, 0, 0);

// ── WiFi ─────────────────────────────────────────────────────────────────────
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
    wl_status_t begin(const char *, const char * = nullptr, int32_t = 0, const uint8_t * = nullptr, bool = true) {
        return WL_DISCONNECTED;
    }
    wl_status_t status() { return _mode == WIFI_OFF ? WL_NO_SHIELD : WL_DISCONNECTED; }
    bool mode(wifi_mode_t m) { _mode = m; return true; }
    wifi_mode_t getMode() { return _mode; }
    bool disconnect(bool = false, bool = false) { return true; }
    bool isConnected() { return false; }
    bool setSleep(bool) { return true; }
    bool setSleep(wifi_ps_type_t) { return true; }
    bool setTxPower(wifi_power_t) { return true; }
    bool persistent(bool) { return true; }
    bool setAutoReconnect(bool) { return true; }
    bool setHostname(const char *) { return true; }
    const char *getHostname() { return "camillia"; }
    int hostByName(const char *, IPAddress &ip) { ip = IPAddress(); return 0; }
    IPAddress localIP() { return IPAddress(); }
    IPAddress softAPIP() { return IPAddress(); }
    const char *softAPgetHostname() { return "camillia"; }
    bool softAP(const char *, const char * = nullptr, int = 1, int = 0, int = 4, bool = false) { return false; }
    bool softAPConfig(IPAddress, IPAddress, IPAddress) { return false; }
    bool softAPdisconnect(bool = false) { return true; }
    int16_t scanNetworks(bool = false, bool = false, bool = false, uint32_t = 300, uint8_t = 0) { return WIFI_SCAN_FAILED; }
    int16_t scanComplete() { return WIFI_SCAN_FAILED; }
    void scanDelete() {}
    String SSID() const { return String(); }
    String SSID(uint8_t) const { return String(); }
    int32_t RSSI() { return 0; }
    int32_t RSSI(uint8_t) { return 0; }
    wifi_auth_mode_t encryptionType(uint8_t) { return WIFI_AUTH_OPEN; }
    String macAddress() { return String("00:00:00:00:00:00"); }
    int onEvent(WiFiEventFuncCb, arduino_event_id_t = ARDUINO_EVENT_MAX) { return 0; }
    int onEvent(WiFiEventCb, arduino_event_id_t = ARDUINO_EVENT_MAX) { return 0; }

private:
    wifi_mode_t _mode = WIFI_OFF;
};
inline WiFiClass WiFi;

// ── WiFiClient ───────────────────────────────────────────────────────────────
class Client : public Stream {};
class WiFiClient : public Client {
public:
    virtual ~WiFiClient() {}
    virtual int connect(IPAddress, uint16_t) { return 0; }
    virtual int connect(const char *, uint16_t) { return 0; }
    virtual int connect(IPAddress, uint16_t, int32_t) { return 0; }
    virtual int connect(const char *, uint16_t, int32_t) { return 0; }
    size_t write(uint8_t) override { return 0; }
    size_t write(const uint8_t *, size_t) override { return 0; }
    using Print::write;
    int available() override { return 0; }
    int read() override { return -1; }
    virtual int read(uint8_t *, size_t) { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    virtual void stop() {}
    virtual uint8_t connected() { return 0; }
    operator bool() { return false; }
    int fd() const { return -1; }
    int setNoDelay(bool) { return 0; }
    void setTimeout(uint32_t ms) { Stream::setTimeout(ms); }
    IPAddress remoteIP() const { return IPAddress(); }
};
class WiFiClientSecure : public WiFiClient {
public:
    void setInsecure() {}
    void setCACert(const char *) {}
};

// ── HTTPClient ───────────────────────────────────────────────────────────────
#define HTTP_CODE_OK 200
#define HTTPC_ERROR_CONNECTION_REFUSED (-1)
typedef enum { HTTPC_DISABLE_FOLLOW_REDIRECTS, HTTPC_STRICT_FOLLOW_REDIRECTS, HTTPC_FORCE_FOLLOW_REDIRECTS } followRedirects_t;

class HTTPClient {
public:
    bool begin(WiFiClient &, const String &) { return false; }
    bool begin(WiFiClient &, const char *) { return false; }
    bool begin(const String &) { return false; }
    bool begin(WiFiClient &, const char *, uint16_t, const char * = "/", bool = false) { return false; }
    void useHTTP10(bool = true) {}
    void end() {}
    bool connected() { return false; }
    void setReuse(bool) {}
    void setTimeout(uint16_t) {}
    void setConnectTimeout(int32_t) {}
    void setFollowRedirects(followRedirects_t) {}
    void setUserAgent(const String &) {}
    void addHeader(const String &, const String &, bool = false, bool = true) {}
    void collectHeaders(const char *[], const size_t) {}
    String header(const char *) { return String(); }
    bool hasHeader(const char *) { return false; }
    int GET() { return HTTPC_ERROR_CONNECTION_REFUSED; }
    int POST(const String &) { return HTTPC_ERROR_CONNECTION_REFUSED; }
    int POST(uint8_t *, size_t) { return HTTPC_ERROR_CONNECTION_REFUSED; }
    int getSize() { return -1; }
    String getString() { return String(); }
    WiFiClient &getStream() { return _c; }
    WiFiClient *getStreamPtr() { return &_c; }
    static String errorToString(int) { return String("no network"); }

private:
    WiFiClient _c;
};

// ── WebServer ────────────────────────────────────────────────────────────────
typedef enum { HTTP_ANY, HTTP_GET, HTTP_HEAD, HTTP_POST, HTTP_PUT, HTTP_PATCH, HTTP_DELETE, HTTP_OPTIONS } HTTPMethod;
enum HTTPUploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
#define HTTP_UPLOAD_BUFLEN 1436
struct HTTPUpload {
    HTTPUploadStatus status = UPLOAD_FILE_START;
    String filename, name, type;
    size_t totalSize = 0, currentSize = 0;
    uint8_t buf[HTTP_UPLOAD_BUFLEN];
};
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)

class WebServer {
public:
    typedef std::function<void(void)> THandlerFunction;
    explicit WebServer(int = 80) {}
    void begin() {}
    void stop() {}
    void close() {}
    void handleClient() {}
    void enableDelay(bool) {}
    void on(const String &, THandlerFunction) {}
    void on(const String &, HTTPMethod, THandlerFunction) {}
    void on(const String &, HTTPMethod, THandlerFunction, THandlerFunction) {}
    void onNotFound(THandlerFunction) {}
    void collectHeaders(const char *[], const size_t) {}
    String arg(const String &) { return String(); }
    String arg(int) { return String(); }
    bool hasArg(const String &) { return false; }
    String header(const String &) { return String(); }
    bool hasHeader(const String &) { return false; }
    String hostHeader() { return String(); }
    String uri() { return String(); }
    HTTPMethod method() { return HTTP_GET; }
    HTTPUpload &upload() { return _u; }
    WiFiClient &client() { return _c; }
    void send(int, const char * = nullptr, const String & = String()) {}
    void send(int, const char *, const char *) {}
    void send(int, const String &, const String &) {}
    void send_P(int, const char *, const char *) {}
    void send_P(int, const char *, const char *, size_t) {}
    void sendHeader(const String &, const String &, bool = false) {}
    void sendContent(const String &) {}
    void sendContent(const char *, size_t) {}
    void sendContent_P(const char *) {}
    void setContentLength(const size_t) {}
    template <typename T>
    size_t streamFile(T &, const String &, int = 200) { return 0; }

private:
    HTTPUpload _u;
    WiFiClient _c;
};

// ── Update (OTA) ─────────────────────────────────────────────────────────────
#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF
#define U_FLASH 0
#define UPDATE_ERROR_ABORT 8
class UpdateClass {
public:
    bool begin(size_t = UPDATE_SIZE_UNKNOWN, int = U_FLASH, int = -1, uint8_t = 0, const char * = nullptr) { return false; }
    size_t write(uint8_t *, size_t) { return 0; }
    size_t writeStream(Stream &) { return 0; }
    bool end(bool = false) { return false; }
    void abort() {}
    bool isFinished() { return false; }
    bool hasError() { return true; }
    uint8_t getError() { return UPDATE_ERROR_ABORT; }
    const char *errorString() { return "OTA is not available on this device"; }
    void printError(Print &) {}
};
inline UpdateClass Update;

// ── PubSubClient ─────────────────────────────────────────────────────────────
#define MQTT_SOCKET_TIMEOUT 15
#define MQTT_CONNECTION_TIMEOUT (-4)
#define MQTT_CALLBACK_SIGNATURE std::function<void(char *, uint8_t *, unsigned int)> callback
class PubSubClient {
public:
    PubSubClient() {}
    explicit PubSubClient(Client &) {}
    PubSubClient &setClient(Client &) { return *this; }
    PubSubClient &setServer(IPAddress, uint16_t) { return *this; }
    PubSubClient &setServer(const char *, uint16_t) { return *this; }
    PubSubClient &setCallback(MQTT_CALLBACK_SIGNATURE) { return *this; }
    PubSubClient &setSocketTimeout(uint16_t) { return *this; }
    PubSubClient &setKeepAlive(uint16_t) { return *this; }
    bool setBufferSize(uint16_t) { return true; }
    bool connect(const char *) { return false; }
    bool connect(const char *, const char *, const char *) { return false; }
    bool connect(const char *, const char *, const char *, const char *, uint8_t, bool, const char *, bool = true) { return false; }
    void disconnect() {}
    bool connected() { return false; }
    bool loop() { return false; }
    bool publish(const char *, const char *) { return false; }
    bool publish(const char *, const char *, bool) { return false; }
    bool publish(const char *, const uint8_t *, unsigned int) { return false; }
    bool publish(const char *, const uint8_t *, unsigned int, bool) { return false; }
    bool subscribe(const char *, uint8_t = 0) { return false; }
    bool unsubscribe(const char *) { return false; }
    int state() { return MQTT_CONNECTION_TIMEOUT; }
};
