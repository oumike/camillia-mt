// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// arduino-esp32's WiFiClient as a non-blocking Linux TCP socket
// (app/net_linux.cpp). Copies share the socket; the last one closes it.
#include <memory>

#include "Arduino.h"
#include "Client.h"
#include "IPAddress.h"

struct NetSocket;

class WiFiClient : public Client {
public:
    WiFiClient();
    explicit WiFiClient(int fd);  // takes ownership of a connected socket
    ~WiFiClient() override;

    int connect(IPAddress ip, uint16_t port) override { return connect(ip, port, -1); }
    int connect(const char *host, uint16_t port) override { return connect(host, port, -1); }
    // timeoutMs <= 0 uses the client's timeout (setTimeout(), default 3 s).
    virtual int connect(IPAddress ip, uint16_t port, int32_t timeoutMs);
    virtual int connect(const char *host, uint16_t port, int32_t timeoutMs);

    size_t write(uint8_t c) override { return write(&c, 1); }
    // Blocks until everything is sent, giving up after a full timeout without progress.
    size_t write(const uint8_t *buf, size_t size) override;
    size_t write(Stream &stream);
    using Print::write;

    int available() override;
    // read()/read(buf, n) never block; readBytes() waits up to the Stream timeout.
    int read() override;
    int read(uint8_t *buf, size_t size) override;
    size_t readBytes(char *buf, size_t size) override;
    size_t readBytes(uint8_t *buf, size_t size) override { return readBytes((char *)buf, size); }
    int peek() override;
    void flush() override {}
    void stop() override;
    // True while the socket is open or received data is still unread.
    uint8_t connected() override;
    operator bool() override { return connected(); }
    bool operator==(const WiFiClient &o) const { return _sock == o._sock; }
    bool operator!=(const WiFiClient &o) const { return !(*this == o); }

    int fd() const;
    int setNoDelay(bool nodelay);
    bool getNoDelay();
    // arduino-esp32 semantics: SECONDS. Bounds connect(), write stalls and readBytes().
    int setTimeout(uint32_t seconds);
    IPAddress remoteIP() const;
    uint16_t remotePort() const;
    IPAddress localIP() const;
    uint16_t localPort() const;

    // Linux additions for HTTPClient / WebServer: wait for unread data.
    bool waitAvailable(uint32_t timeoutMs);
    void setTimeoutMs(uint32_t ms) { _timeoutMs = ms; Stream::setTimeout(ms); }
    uint32_t timeoutMs() const { return _timeoutMs; }

protected:
    std::shared_ptr<NetSocket> _sock;
    uint32_t _timeoutMs = 3000;
};
