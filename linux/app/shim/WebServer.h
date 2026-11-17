// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// arduino-esp32 2.0.17's synchronous WebServer on a Linux listening socket
// (app/web_server_linux.cpp). handleClient() never waits for a connection:
// it accepts what is pending and serves at most one request whose data has
// arrived, start to finish, as the ESP32 server does.
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Arduino.h"
#include "WiFiClient.h"

typedef enum { HTTP_ANY, HTTP_GET, HTTP_HEAD, HTTP_POST, HTTP_PUT, HTTP_PATCH, HTTP_DELETE, HTTP_OPTIONS } HTTPMethod;
enum HTTPUploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
enum HTTPClientStatus { HC_NONE, HC_WAIT_READ, HC_WAIT_CLOSE };
#define HTTP_DOWNLOAD_UNIT_SIZE 1436
#define HTTP_UPLOAD_BUFLEN 1436
#define HTTP_MAX_DATA_WAIT 5000   // ms to wait for the client to send the request
#define HTTP_MAX_POST_WAIT 5000   // ms to wait for POST data to arrive
#define HTTP_MAX_SEND_WAIT 5000   // ms to wait for data chunk to be ACKed
#define HTTP_MAX_CLOSE_WAIT 2000  // ms to wait for the client to close the connection
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
#define CONTENT_LENGTH_NOT_SET ((size_t)-2)

struct HTTPUpload {
    HTTPUploadStatus status = UPLOAD_FILE_START;
    String filename, name, type;
    size_t totalSize = 0, currentSize = 0;
    uint8_t buf[HTTP_UPLOAD_BUFLEN];
};

class WebServer {
public:
    typedef std::function<void(void)> THandlerFunction;
    explicit WebServer(int port = 80) : _port(port) {}
    virtual ~WebServer() { close(); }

    virtual void begin();
    virtual void begin(uint16_t port) { _port = port; begin(); }
    virtual void handleClient();
    virtual void close();
    void stop() { close(); }
    void enableDelay(bool value) { _nullDelay = value; }
    void enableCORS(bool value = true) { _corsEnabled = value; }
    void enableCrossOrigin(bool value = true) { enableCORS(value); }

    // Exact-path routes; the first that matches uri and method handles it.
    void on(const String &uri, THandlerFunction fn) { on(uri, HTTP_ANY, fn); }
    void on(const String &uri, HTTPMethod method, THandlerFunction fn) { on(uri, method, fn, nullptr); }
    void on(const String &uri, HTTPMethod method, THandlerFunction fn, THandlerFunction ufn);
    void onNotFound(THandlerFunction fn) { _notFoundHandler = fn; }
    void onFileUpload(THandlerFunction ufn) { _fileUploadHandler = ufn; }

    String uri() { return _currentUri; }
    HTTPMethod method() { return _currentMethod; }
    WiFiClient &client() { return _currentClient; }
    HTTPUpload &upload() {
        if (!_currentUpload) _currentUpload.reset(new HTTPUpload());
        return *_currentUpload;
    }

    String arg(const String &name);
    String arg(int i);
    String argName(int i);
    int args() { return (int)_args.size(); }
    bool hasArg(const String &name);

    void collectHeaders(const char *headerKeys[], const size_t headerKeysCount);
    String header(const String &name);
    String header(int i);
    String headerName(int i);
    int headers() { return (int)_headers.size(); }
    bool hasHeader(const String &name);
    String hostHeader() { return _hostHeader; }

    void send(int code, const char *contentType = nullptr, const String &content = String(""));
    void send(int code, char *contentType, const String &content) { send(code, (const char *)contentType, content); }
    void send(int code, const String &contentType, const String &content) { send(code, contentType.c_str(), content); }
    void send(int code, const char *contentType, const char *content) { send_P(code, contentType, content); }
    void send_P(int code, PGM_P contentType, PGM_P content);
    void send_P(int code, PGM_P contentType, PGM_P content, size_t contentLength);
    void setContentLength(const size_t contentLength) { _contentLength = contentLength; }
    void sendHeader(const String &name, const String &value, bool first = false);
    void sendContent(const String &content) { sendContent(content.c_str(), content.length()); }
    // With CONTENT_LENGTH_UNKNOWN each call is one chunk; an empty one ends the response.
    void sendContent(const char *content, size_t contentLength);
    void sendContent_P(PGM_P content) { sendContent(content, strlen(content)); }
    void sendContent_P(PGM_P content, size_t size) { sendContent(content, size); }

    static String urlDecode(const String &text);

    template <typename T>
    size_t streamFile(T &file, const String &contentType, const int code = 200) {
        setContentLength(file.size());
        String name = file.name();
        if (name.endsWith(".gz") && contentType != "application/x-gzip" && contentType != "application/octet-stream")
            sendHeader("Content-Encoding", "gzip");
        send(code, contentType, "");
        uint8_t buf[HTTP_DOWNLOAD_UNIT_SIZE];
        size_t sent = 0;
        for (;;) {
            const size_t n = file.read(buf, sizeof buf);
            if (n == 0) break;
            const size_t w = _currentClient.write(buf, n);
            sent += w;
            if (w != n) break;
        }
        return sent;
    }

private:
    struct Route {
        String uri;
        HTTPMethod method;
        THandlerFunction fn, ufn;
    };
    struct Pending {
        WiFiClient client;
        uint32_t since;
    };
    void acceptPending();
    void serve(WiFiClient &client);
    bool parseRequest(WiFiClient &client);
    bool readLine(WiFiClient &client, String &line, uint32_t timeoutMs);
    bool parseMultipart(WiFiClient &client, const String &boundary, size_t len);
    void parseArguments(const String &data);
    void handleRequest();
    void finishResponse();
    void lingerClose(WiFiClient &client);
    void serviceLingering();
    void prepareHeader(String &response, int code, const char *contentType, size_t contentLength);
    void resetRequest();
    void callUpload(const Route *route);

    int _port;
    int _listenFd = -1;
    bool _nullDelay = true;
    bool _corsEnabled = false;

    std::vector<Route> _routes;
    THandlerFunction _notFoundHandler, _fileUploadHandler;
    std::vector<Pending> _pending;
    std::vector<Pending> _lingering;

    WiFiClient _currentClient;
    HTTPMethod _currentMethod = HTTP_ANY;
    String _currentUri;
    int _currentVersion = 1;
    const Route *_currentRoute = nullptr;
    std::unique_ptr<HTTPUpload> _currentUpload;
    std::vector<std::pair<String, String>> _args;
    std::vector<std::pair<String, String>> _headers;  // every request header, in arrival order
    String _hostHeader;
    String _responseHeaders;
    size_t _contentLength = CONTENT_LENGTH_NOT_SET;
    bool _chunked = false;
};
