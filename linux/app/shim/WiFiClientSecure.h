// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// arduino-esp32's WiFiClientSecure over mbedtls (app/net_linux.cpp). With
// neither setInsecure() nor setCACert(), the server is verified against the
// system CA bundle.
#include <string>

#include "WiFiClient.h"

class WiFiClientSecure : public WiFiClient {
public:
    using WiFiClient::connect;
    int connect(IPAddress ip, uint16_t port, int32_t timeoutMs) override;
    int connect(const char *host, uint16_t port, int32_t timeoutMs) override;
    void setInsecure() { _insecure = true; }
    void setCACert(const char *pem) { _ca = pem ? pem : ""; _insecure = false; }
    void setHandshakeTimeout(unsigned long seconds) { _handshakeMs = seconds * 1000; }
    int lastError(char *buf, size_t size);

private:
    int startTls(const char *host, int32_t timeoutMs);
    bool _insecure = false;
    std::string _ca;
    uint32_t _handshakeMs = 15000;
    int _lastError = 0;
};
