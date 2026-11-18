// SPDX-License-Identifier: GPL-3.0-or-later
// HTTPClient (app/shim/HTTPClient.h): HTTP/1.1 requests with Connection: close,
// so every request gets its own connection and the body ends where the server
// says (Content-Length, chunked) or at close.
#include "HTTPClient.h"

#include <strings.h>

static String base64(const String &in) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String out;
    const uint8_t *s = (const uint8_t *)in.c_str();
    const size_t n = in.length();
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t v = (uint32_t)s[i] << 16 | (i + 1 < n ? (uint32_t)s[i + 1] << 8 : 0) | (i + 2 < n ? s[i + 2] : 0);
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += i + 1 < n ? tbl[(v >> 6) & 63] : '=';
        out += i + 2 < n ? tbl[v & 63] : '=';
    }
    return out;
}

HTTPClient::~HTTPClient() {
    if (_client) _client->stop();
}

bool HTTPClient::begin(const String &url) {
    end();
    _ownClient.reset();
    return beginInternal(url, true);
}

bool HTTPClient::begin(const String &host, uint16_t port, const String &uri, bool https) {
    end();
    _ownClient.reset(https ? new WiFiClientSecure() : new WiFiClient());
    _client = _ownClient.get();
    _protocol = https ? "https" : "http";
    _secure = https;
    _host = host;
    _port = port;
    _uri = uri;
    return true;
}

bool HTTPClient::begin(WiFiClient &client, const String &url) {
    end();
    _ownClient.reset();
    _client = &client;
    return beginInternal(url, false);
}

bool HTTPClient::begin(WiFiClient &client, const String &host, uint16_t port, const String &uri, bool https) {
    end();
    _ownClient.reset();
    if (https && !dynamic_cast<WiFiClientSecure *>(&client)) {
        Serial.println("[http] https needs a WiFiClientSecure");
        return false;
    }
    _client = &client;
    _protocol = https ? "https" : "http";
    _secure = https;
    _host = host;
    _port = port;
    _uri = uri;
    return true;
}

// "scheme://[user:pass@]host[:port][/path]". With an internal client the
// scheme may change (a redirect to https); a caller's client is kept as is.
bool HTTPClient::beginInternal(const String &url, bool ownClient) {
    const int sep = url.indexOf("://");
    if (sep <= 0) return false;
    String protocol = url.substring(0, sep);
    protocol.toLowerCase();
    if (protocol != "http" && protocol != "https") return false;
    const bool secure = protocol == "https";
    if (ownClient) {
        if (!_ownClient || secure != _secure) {
            if (_client) _client->stop();
            _ownClient.reset(secure ? new WiFiClientSecure() : new WiFiClient());
        }
        _client = _ownClient.get();
    } else if (_client && secure && !dynamic_cast<WiFiClientSecure *>(_client)) {
        Serial.printf("[http] %s needs a WiFiClientSecure\n", url.c_str());
        return false;
    } else if (_protocol.length() && protocol != _protocol) {
        return false;  // a redirect cannot switch a caller's client between http and https
    }
    _protocol = protocol;
    _secure = secure;
    String rest = url.substring(sep + 3);
    int slash = rest.indexOf('/');
    String host = slash < 0 ? rest : rest.substring(0, slash);
    _uri = slash < 0 ? String("/") : rest.substring(slash);
    const int q = host.indexOf('?');
    if (slash < 0 && q >= 0) {
        _uri = "/" + host.substring(q);
        host = host.substring(0, q);
    }
    const int at = host.lastIndexOf('@');
    if (at >= 0) {
        _authorization = base64(host.substring(0, at));
        host = host.substring(at + 1);
    }
    _port = secure ? 443 : 80;
    const int colon = host.lastIndexOf(':');
    if (colon >= 0 && host.indexOf(']') < colon) {
        _port = (uint16_t)host.substring(colon + 1).toInt();
        host = host.substring(0, colon);
    }
    _host = host;
    return _host.length() > 0;
}

bool HTTPClient::setURL(const String &url) {
    disconnect(true);
    if (url.startsWith("/")) {
        _uri = url;
        return true;
    }
    return beginInternal(url, _ownClient && _client == _ownClient.get());
}

void HTTPClient::setAuthorization(const char *user, const char *password) {
    if (user && password) _authorization = base64(String(user) + ":" + password);
}

void HTTPClient::end() {
    disconnect(false);
    clearResponse();
    _protocol = "";
}

void HTTPClient::disconnect(bool preserveClient) {
    if (_client && _client->connected()) {
        if (_reuse && _canReuse) {
            while (_client->available() > 0) _client->read();
        } else {
            _client->stop();
        }
    }
    if (!preserveClient) _client = nullptr;
}

bool HTTPClient::connected() {
    return _client && (_client->available() > 0 || _client->connected());
}

void HTTPClient::clearResponse() {
    for (auto &h : _respHeaders) h.second = "";
    _returnCode = 0;
    _size = -1;
    _canReuse = false;
    _transferEncoding = HTTPC_TE_IDENTITY;
    _location = "";
}

bool HTTPClient::connect() {
    if (!_client) return false;
    if (_client->connected()) {
        // Already open (the caller connected it): reuse, dropping anything stale.
        while (_client->available() > 0) _client->read();
        _client->setTimeoutMs(_tcpTimeout);
        return true;
    }
    if (!_client->connect(_host.c_str(), _port, _connectTimeout)) return false;
    _client->setTimeoutMs(_tcpTimeout);
    return true;
}

void HTTPClient::addHeader(const String &name, const String &value, bool first, bool replace) {
    if (name.equalsIgnoreCase("Connection") || name.equalsIgnoreCase("Host")) return;
    if (name.equalsIgnoreCase("Authorization") && _authorization.length()) return;
    if (name.equalsIgnoreCase("User-Agent")) {
        _userAgent = value;
        return;
    }
    const String line = name + ": " + value + "\r\n";
    if (replace) {
        String lower = "\n" + _headers;
        lower.toLowerCase();
        String key = "\n" + name + ":";
        key.toLowerCase();
        const int at = lower.indexOf(key);
        if (at >= 0) {
            const int end = _headers.indexOf('\n', at);
            _headers.remove(at, end < 0 ? _headers.length() - at : end - at + 1);
        }
    }
    _headers = first ? line + _headers : _headers + line;
}

void HTTPClient::collectHeaders(const char *headerKeys[], const size_t headerKeysCount) {
    _respHeaders.clear();
    for (size_t i = 0; i < headerKeysCount; i++) _respHeaders.emplace_back(String(headerKeys[i]), String());
}

String HTTPClient::header(const char *name) {
    for (auto &h : _respHeaders)
        if (h.first.equalsIgnoreCase(name)) return h.second;
    return String();
}

String HTTPClient::header(size_t i) { return i < _respHeaders.size() ? _respHeaders[i].second : String(); }

String HTTPClient::headerName(size_t i) { return i < _respHeaders.size() ? _respHeaders[i].first : String(); }

bool HTTPClient::hasHeader(const char *name) { return header(name).length() > 0; }

int HTTPClient::GET() { return sendRequest("GET"); }

int HTTPClient::sendRequest(const char *type, uint8_t *payload, size_t size) {
    String method = type;
    uint16_t redirects = 0;
    int code;
    for (;;) {
        clearResponse();
        if (!connect()) return returnError(HTTPC_ERROR_CONNECTION_REFUSED);
        if (!sendHeader(method.c_str(), payload ? size : (size_t)-1))
            return returnError(HTTPC_ERROR_SEND_HEADER_FAILED);
        if (payload && size > 0 && _client->write(payload, size) != size)
            return returnError(HTTPC_ERROR_SEND_PAYLOAD_FAILED);
        code = handleHeaderResponse();
        if (code <= 0 || !_location.length() || redirects >= _redirectLimit) break;
        const bool getOrHead = method == "GET" || method == "HEAD";
        const bool sameMethod = code == HTTP_CODE_TEMPORARY_REDIRECT || code == HTTP_CODE_PERMANENT_REDIRECT;
        const bool other = code == HTTP_CODE_MOVED_PERMANENTLY || code == HTTP_CODE_FOUND || code == HTTP_CODE_SEE_OTHER;
        bool follow = false;
        if (_followRedirects == HTTPC_FORCE_FOLLOW_REDIRECTS) follow = sameMethod || other;
        else if (_followRedirects == HTTPC_STRICT_FOLLOW_REDIRECTS)
            follow = sameMethod || (other && (getOrHead || code == HTTP_CODE_SEE_OTHER));
        if (!follow || !setURL(_location)) break;
        redirects++;
        if (code == HTTP_CODE_SEE_OTHER && !getOrHead) {
            method = "GET";
            payload = nullptr;
            size = 0;
        }
    }
    return returnError(code);
}

// payloadSize (size_t)-1: no body, no Content-Length.
bool HTTPClient::sendHeader(const char *type, size_t payloadSize) {
    String h = String(type) + " " + (_uri.length() ? _uri : String("/")) + (_useHTTP10 ? " HTTP/1.0\r\n" : " HTTP/1.1\r\n");
    h += "Host: " + _host;
    if (_port != (_secure ? 443 : 80)) h += ":" + String(_port);
    h += "\r\nUser-Agent: " + _userAgent + "\r\nConnection: close\r\n";
    if (!_useHTTP10) h += "Accept-Encoding: identity;q=1,chunked;q=0.1,*;q=0\r\n";
    if (_authorization.length()) h += "Authorization: Basic " + _authorization + "\r\n";
    if (payloadSize != (size_t)-1) h += "Content-Length: " + String((unsigned long)payloadSize) + "\r\n";
    h += _headers + "\r\n";
    return _client->write((const uint8_t *)h.c_str(), h.length()) == h.length();
}

bool HTTPClient::readLine(String &line) {
    line = "";
    for (;;) {
        if (!_client->waitAvailable(_tcpTimeout)) return false;
        const int c = _client->read();
        if (c < 0) continue;
        if (c == '\n') break;
        if (c != '\r' && line.length() < 8192) line += (char)c;
    }
    return true;
}

int HTTPClient::handleHeaderResponse() {
    if (!_client) return HTTPC_ERROR_NOT_CONNECTED;
    int code = 0;
    bool statusLine = true;
    String line;
    for (;;) {
        if (!readLine(line)) return _client->connected() ? HTTPC_ERROR_READ_TIMEOUT : HTTPC_ERROR_CONNECTION_LOST;
        if (statusLine) {
            if (!line.startsWith("HTTP/1.")) return HTTPC_ERROR_NO_HTTP_SERVER;
            code = line.substring(line.indexOf(' ') + 1).toInt();
            statusLine = false;
            continue;
        }
        if (!line.length()) {
            if (code == HTTP_CODE_CONTINUE) {  // an interim reply; the real one follows
                statusLine = true;
                continue;
            }
            break;
        }
        const int colon = line.indexOf(':');
        if (colon <= 0) continue;
        String name = line.substring(0, colon);
        String value = line.substring(colon + 1);
        name.trim();
        value.trim();
        if (name.equalsIgnoreCase("Content-Length")) _size = value.toInt();
        else if (name.equalsIgnoreCase("Transfer-Encoding") && value.equalsIgnoreCase("chunked"))
            _transferEncoding = HTTPC_TE_CHUNKED;
        else if (name.equalsIgnoreCase("Location")) _location = value;
        for (auto &h : _respHeaders) {
            if (!h.first.equalsIgnoreCase(name)) continue;
            h.second = h.second.length() ? h.second + "," + value : value;
        }
    }
    if (_transferEncoding == HTTPC_TE_CHUNKED) _size = -1;
    _returnCode = code;
    return code > 0 ? code : HTTPC_ERROR_NO_HTTP_SERVER;
}

// Reads the body into out or stream; returns the byte count or an HTTPC_ERROR_*.
int HTTPClient::readBody(std::string *out, Stream *stream) {
    if (!_client || !_returnCode) return HTTPC_ERROR_NOT_CONNECTED;
    uint8_t buf[HTTP_TCP_BUFFER_SIZE];
    int total = 0;
    // len < 0: to the end of the stream.
    auto copy = [&](long len) -> int {
        while (len != 0) {
            if (!_client->waitAvailable(_tcpTimeout)) {
                if (len < 0 && !_client->connected()) return 0;
                return _client->connected() ? HTTPC_ERROR_READ_TIMEOUT : HTTPC_ERROR_CONNECTION_LOST;
            }
            const size_t want = len < 0 ? sizeof buf : std::min((size_t)len, sizeof buf);
            const int n = _client->read(buf, want);
            if (n <= 0) continue;
            if (out) out->append((const char *)buf, (size_t)n);
            if (stream && stream->write(buf, (size_t)n) != (size_t)n) return HTTPC_ERROR_STREAM_WRITE;
            total += n;
            if (len > 0) len -= n;
        }
        return 0;
    };
    int err = 0;
    if (_transferEncoding == HTTPC_TE_CHUNKED) {
        String line;
        for (;;) {
            if (!readLine(line)) return HTTPC_ERROR_READ_TIMEOUT;
            const long chunk = strtol(line.c_str(), nullptr, 16);
            if (chunk < 0) return HTTPC_ERROR_ENCODING;
            if (chunk == 0) {
                while (readLine(line) && line.length()) {}  // trailers
                break;
            }
            if ((err = copy(chunk)) < 0) return err;
            if (!readLine(line)) return HTTPC_ERROR_READ_TIMEOUT;
        }
    } else if ((err = copy(_size >= 0 ? _size : -1)) < 0) {
        return err;
    }
    return total;
}

int HTTPClient::writeToStream(Stream *stream) {
    if (!stream) return returnError(HTTPC_ERROR_NO_STREAM);
    const int r = readBody(nullptr, stream);
    return r < 0 ? returnError(r) : r;
}

String HTTPClient::getString() {
    std::string body;
    if (_size > 0) body.reserve((size_t)_size);
    if (readBody(&body, nullptr) < 0 && body.empty()) return String();
    return String(body.c_str(), body.size());
}

WiFiClient &HTTPClient::getStream() {
    static WiFiClient none;
    return _client ? *_client : none;
}

WiFiClient *HTTPClient::getStreamPtr() { return connected() ? _client : nullptr; }

int HTTPClient::returnError(int error) {
    if (error < 0 && _client && _client->connected()) _client->stop();
    return error;
}

String HTTPClient::errorToString(int error) {
    switch (error) {
    case HTTPC_ERROR_CONNECTION_REFUSED: return "connection refused";
    case HTTPC_ERROR_SEND_HEADER_FAILED: return "send header failed";
    case HTTPC_ERROR_SEND_PAYLOAD_FAILED: return "send payload failed";
    case HTTPC_ERROR_NOT_CONNECTED: return "not connected";
    case HTTPC_ERROR_CONNECTION_LOST: return "connection lost";
    case HTTPC_ERROR_NO_STREAM: return "no stream";
    case HTTPC_ERROR_NO_HTTP_SERVER: return "no HTTP server";
    case HTTPC_ERROR_TOO_LESS_RAM: return "too less ram";
    case HTTPC_ERROR_ENCODING: return "Transfer-Encoding not supported";
    case HTTPC_ERROR_STREAM_WRITE: return "Stream write error";
    case HTTPC_ERROR_READ_TIMEOUT: return "read Timeout";
    default: return String();
    }
}
