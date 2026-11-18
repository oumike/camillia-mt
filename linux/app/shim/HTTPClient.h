// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// arduino-esp32's HTTPClient (HTTP/1.1, one request per connection) over the
// Linux WiFiClient / WiFiClientSecure (app/http_client_linux.cpp).
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Arduino.h"
#include "WiFiClient.h"
#include "WiFiClientSecure.h"

#define HTTPCLIENT_DEFAULT_TCP_TIMEOUT (5000)

#define HTTPC_ERROR_CONNECTION_REFUSED  (-1)
#define HTTPC_ERROR_SEND_HEADER_FAILED  (-2)
#define HTTPC_ERROR_SEND_PAYLOAD_FAILED (-3)
#define HTTPC_ERROR_NOT_CONNECTED       (-4)
#define HTTPC_ERROR_CONNECTION_LOST     (-5)
#define HTTPC_ERROR_NO_STREAM           (-6)
#define HTTPC_ERROR_NO_HTTP_SERVER      (-7)
#define HTTPC_ERROR_TOO_LESS_RAM        (-8)
#define HTTPC_ERROR_ENCODING            (-9)
#define HTTPC_ERROR_STREAM_WRITE        (-10)
#define HTTPC_ERROR_READ_TIMEOUT        (-11)

#define HTTP_TCP_BUFFER_SIZE (1460)

typedef enum {
    HTTP_CODE_CONTINUE = 100,
    HTTP_CODE_SWITCHING_PROTOCOLS = 101,
    HTTP_CODE_PROCESSING = 102,
    HTTP_CODE_OK = 200,
    HTTP_CODE_CREATED = 201,
    HTTP_CODE_ACCEPTED = 202,
    HTTP_CODE_NON_AUTHORITATIVE_INFORMATION = 203,
    HTTP_CODE_NO_CONTENT = 204,
    HTTP_CODE_RESET_CONTENT = 205,
    HTTP_CODE_PARTIAL_CONTENT = 206,
    HTTP_CODE_MULTI_STATUS = 207,
    HTTP_CODE_ALREADY_REPORTED = 208,
    HTTP_CODE_IM_USED = 226,
    HTTP_CODE_MULTIPLE_CHOICES = 300,
    HTTP_CODE_MOVED_PERMANENTLY = 301,
    HTTP_CODE_FOUND = 302,
    HTTP_CODE_SEE_OTHER = 303,
    HTTP_CODE_NOT_MODIFIED = 304,
    HTTP_CODE_USE_PROXY = 305,
    HTTP_CODE_TEMPORARY_REDIRECT = 307,
    HTTP_CODE_PERMANENT_REDIRECT = 308,
    HTTP_CODE_BAD_REQUEST = 400,
    HTTP_CODE_UNAUTHORIZED = 401,
    HTTP_CODE_PAYMENT_REQUIRED = 402,
    HTTP_CODE_FORBIDDEN = 403,
    HTTP_CODE_NOT_FOUND = 404,
    HTTP_CODE_METHOD_NOT_ALLOWED = 405,
    HTTP_CODE_NOT_ACCEPTABLE = 406,
    HTTP_CODE_PROXY_AUTHENTICATION_REQUIRED = 407,
    HTTP_CODE_REQUEST_TIMEOUT = 408,
    HTTP_CODE_CONFLICT = 409,
    HTTP_CODE_GONE = 410,
    HTTP_CODE_LENGTH_REQUIRED = 411,
    HTTP_CODE_PRECONDITION_FAILED = 412,
    HTTP_CODE_PAYLOAD_TOO_LARGE = 413,
    HTTP_CODE_URI_TOO_LONG = 414,
    HTTP_CODE_UNSUPPORTED_MEDIA_TYPE = 415,
    HTTP_CODE_RANGE_NOT_SATISFIABLE = 416,
    HTTP_CODE_EXPECTATION_FAILED = 417,
    HTTP_CODE_MISDIRECTED_REQUEST = 421,
    HTTP_CODE_UNPROCESSABLE_ENTITY = 422,
    HTTP_CODE_LOCKED = 423,
    HTTP_CODE_FAILED_DEPENDENCY = 424,
    HTTP_CODE_UPGRADE_REQUIRED = 426,
    HTTP_CODE_PRECONDITION_REQUIRED = 428,
    HTTP_CODE_TOO_MANY_REQUESTS = 429,
    HTTP_CODE_REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
    HTTP_CODE_INTERNAL_SERVER_ERROR = 500,
    HTTP_CODE_NOT_IMPLEMENTED = 501,
    HTTP_CODE_BAD_GATEWAY = 502,
    HTTP_CODE_SERVICE_UNAVAILABLE = 503,
    HTTP_CODE_GATEWAY_TIMEOUT = 504,
    HTTP_CODE_HTTP_VERSION_NOT_SUPPORTED = 505,
    HTTP_CODE_VARIANT_ALSO_NEGOTIATES = 506,
    HTTP_CODE_INSUFFICIENT_STORAGE = 507,
    HTTP_CODE_LOOP_DETECTED = 508,
    HTTP_CODE_NOT_EXTENDED = 510,
    HTTP_CODE_NETWORK_AUTHENTICATION_REQUIRED = 511
} t_http_codes;

typedef enum { HTTPC_TE_IDENTITY, HTTPC_TE_CHUNKED } transferEncoding_t;

// DISABLE: none. STRICT: 307/308 always, 301/302/303 for GET/HEAD only.
// FORCE: all of them, with the same method and body (303 turns into GET).
typedef enum { HTTPC_DISABLE_FOLLOW_REDIRECTS, HTTPC_STRICT_FOLLOW_REDIRECTS, HTTPC_FORCE_FOLLOW_REDIRECTS } followRedirects_t;

class HTTPClient {
public:
    HTTPClient() = default;
    ~HTTPClient();
    HTTPClient(const HTTPClient &) = delete;
    HTTPClient &operator=(const HTTPClient &) = delete;

    // With an internal client: plain or TLS (system CA bundle) by the URL's scheme.
    bool begin(const String &url);
    bool begin(const String &host, uint16_t port, const String &uri = "/", bool https = false);
    // The client must outlive the request. A plain WiFiClient cannot do https.
    bool begin(WiFiClient &client, const String &url);
    bool begin(WiFiClient &client, const char *url) { return begin(client, String(url)); }
    bool begin(WiFiClient &client, const String &host, uint16_t port, const String &uri = "/", bool https = false);
    bool begin(WiFiClient &client, const char *host, uint16_t port, const char *uri = "/", bool https = false) {
        return begin(client, String(host), port, String(uri), https);
    }

    void end();
    bool connected();
    void setReuse(bool reuse) { _reuse = reuse; }
    void setUserAgent(const String &userAgent) { _userAgent = userAgent; }
    void setAuthorization(const char *user, const char *password);
    void setAuthorization(const char *auth) { _authorization = auth ? auth : ""; }
    void setConnectTimeout(int32_t ms) { _connectTimeout = ms; }
    void setTimeout(uint16_t ms) { _tcpTimeout = ms; }
    void setFollowRedirects(followRedirects_t follow) { _followRedirects = follow; }
    void setRedirectLimit(uint16_t limit) { _redirectLimit = limit; }
    bool setURL(const String &url);
    void useHTTP10(bool usehttp10 = true) { _useHTTP10 = usehttp10; }

    int GET();
    int HEAD() { return sendRequest("HEAD"); }
    int POST(uint8_t *payload, size_t size) { return sendRequest("POST", payload, size); }
    int POST(const String &payload) { return POST((uint8_t *)payload.c_str(), payload.length()); }
    int PUT(uint8_t *payload, size_t size) { return sendRequest("PUT", payload, size); }
    int PUT(const String &payload) { return PUT((uint8_t *)payload.c_str(), payload.length()); }
    int PATCH(uint8_t *payload, size_t size) { return sendRequest("PATCH", payload, size); }
    int PATCH(const String &payload) { return PATCH((uint8_t *)payload.c_str(), payload.length()); }
    int sendRequest(const char *type, const String &payload) {
        return sendRequest(type, (uint8_t *)payload.c_str(), payload.length());
    }
    int sendRequest(const char *type, uint8_t *payload = nullptr, size_t size = 0);

    // Host, Connection and (with setAuthorization) Authorization are the
    // client's own. A User-Agent header replaces setUserAgent()'s value.
    void addHeader(const String &name, const String &value, bool first = false, bool replace = true);
    void collectHeaders(const char *headerKeys[], const size_t headerKeysCount);
    String header(const char *name);
    String header(size_t i);
    String headerName(size_t i);
    int headers() { return (int)_respHeaders.size(); }
    bool hasHeader(const char *name);

    int getSize() { return _size; }
    const String &getLocation() { return _location; }
    WiFiClient &getStream();
    WiFiClient *getStreamPtr();
    int writeToStream(Stream *stream);
    String getString();
    static String errorToString(int error);

private:
    bool beginInternal(const String &url, bool allowSchemeChange);
    bool connect();
    void disconnect(bool preserveClient);
    void clearResponse();
    bool sendHeader(const char *type, size_t payloadSize);
    int handleHeaderResponse();
    bool readLine(String &line);
    int readBody(std::string *out, Stream *stream);
    int returnError(int error);

    WiFiClient *_client = nullptr;
    std::unique_ptr<WiFiClient> _ownClient;  // begin(url): ours to create and free
    bool _secure = false;
    String _host, _uri, _protocol;
    uint16_t _port = 0;
    String _authorization, _userAgent = "ESP32HTTPClient", _headers;
    bool _reuse = true, _useHTTP10 = false;
    uint16_t _tcpTimeout = HTTPCLIENT_DEFAULT_TCP_TIMEOUT;
    int32_t _connectTimeout = -1;
    followRedirects_t _followRedirects = HTTPC_DISABLE_FOLLOW_REDIRECTS;
    uint16_t _redirectLimit = 10;

    std::vector<std::pair<String, String>> _respHeaders;  // collected keys, values per response
    int _returnCode = 0;
    int _size = -1;
    bool _canReuse = false;
    transferEncoding_t _transferEncoding = HTTPC_TE_IDENTITY;
    String _location;
};
