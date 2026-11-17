// SPDX-License-Identifier: GPL-3.0-or-later
// WebServer (app/shim/WebServer.h): arduino-esp32 2.0.17's request handling
// and response framing on a non-blocking Linux listening socket.
#include "WebServer.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>

static const size_t kMaxBodyBytes = 4u << 20;  // urlencoded / raw bodies held in memory
static const size_t kMaxPending = 16;
static const uint32_t kIdleClientMs = 10000;  // a connection that never sends a request

static const char *statusText(int code) {
    switch (code) {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Time-out";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Request Entity Too Large";
    case 414: return "Request-URI Too Large";
    case 415: return "Unsupported Media Type";
    case 416: return "Requested range not satisfiable";
    case 417: return "Expectation Failed";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Time-out";
    case 505: return "HTTP Version not supported";
    default: return "";
    }
}

// Value of `key=` in a header such as Content-Disposition, unquoted.
static String headerParam(const String &h, const char *key) {
    String lower = h;
    lower.toLowerCase();
    const String k = String(key) + "=";
    int at = -1;
    for (int from = 0; (at = lower.indexOf(k, from)) >= 0; from = at + 1)
        if (at == 0 || lower[at - 1] == ' ' || lower[at - 1] == ';') break;
    if (at < 0) return String();
    at += k.length();
    if (at < (int)h.length() && h[at] == '"') {
        const int end = h.indexOf('"', at + 1);
        return h.substring(at + 1, end < 0 ? h.length() : end);
    }
    const int end = h.indexOf(';', at);
    String v = h.substring(at, end < 0 ? h.length() : end);
    v.trim();
    return v;
}

void WebServer::begin() {
    if (_listenFd >= 0) return;
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    const int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in sa = {};
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)_port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0 || listen(fd, 16) != 0) {
        Serial.printf("[web] cannot listen on port %d: %s\n", _port, strerror(errno));
        ::close(fd);
        return;
    }
    _listenFd = fd;
}

void WebServer::close() {
    if (_listenFd >= 0) ::close(_listenFd);
    _listenFd = -1;
    for (auto &p : _pending) p.client.stop();
    for (auto &p : _lingering) p.client.stop();
    _pending.clear();
    _lingering.clear();
    _currentClient.stop();
}

void WebServer::on(const String &uri, HTTPMethod method, THandlerFunction fn, THandlerFunction ufn) {
    _routes.push_back(Route{uri, method, fn, ufn});
}

void WebServer::acceptPending() {
    for (;;) {
        const int fd = accept4(_listenFd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) return;
        if (_pending.size() >= kMaxPending) {
            _pending.front().client.stop();
            _pending.erase(_pending.begin());
        }
        _pending.push_back(Pending{WiFiClient(fd), millis()});
    }
}

void WebServer::handleClient() {
    if (_listenFd < 0) return;
    serviceLingering();
    acceptPending();
    // Browsers open connections ahead of need; serve one that has sent
    // something and let the idle ones wait (within reason).
    for (size_t i = 0; i < _pending.size(); i++) {
        WiFiClient c = _pending[i].client;
        if (c.available() > 0) {
            _pending.erase(_pending.begin() + i);
            serve(c);
            return;
        }
        if (!c.connected() || millis() - _pending[i].since > kIdleClientMs) {
            c.stop();
            _pending.erase(_pending.begin() + i);
            i--;
        }
    }
    if (_nullDelay) delay(1);
}

void WebServer::serve(WiFiClient &client) {
    resetRequest();
    _currentClient = client;
    _currentClient.setTimeoutMs(HTTP_MAX_SEND_WAIT);
    if (parseRequest(_currentClient)) {
        _contentLength = CONTENT_LENGTH_NOT_SET;
        handleRequest();
        finishResponse();
    }
    lingerClose(_currentClient);
    _currentClient = WiFiClient();
    _currentRoute = nullptr;
}

void WebServer::resetRequest() {
    _args.clear();
    _headers.clear();
    _hostHeader = "";
    _responseHeaders = "";
    _currentUri = "";
    _currentMethod = HTTP_GET;
    _currentVersion = 1;
    _currentRoute = nullptr;
    _contentLength = CONTENT_LENGTH_NOT_SET;
    _chunked = false;
}

bool WebServer::readLine(WiFiClient &client, String &line, uint32_t timeoutMs) {
    line = "";
    for (;;) {
        if (!client.waitAvailable(timeoutMs)) return false;
        const int c = client.read();
        if (c < 0) continue;
        if (c == '\n') return true;
        if (c == '\r') continue;
        if (line.length() >= 8192) return false;
        line += (char)c;
    }
}

bool WebServer::parseRequest(WiFiClient &client) {
    String req;
    if (!readLine(client, req, HTTP_MAX_DATA_WAIT)) return false;
    const int sp1 = req.indexOf(' ');
    const int sp2 = req.indexOf(' ', sp1 + 1);
    if (sp1 <= 0 || sp2 <= sp1) return false;
    const String methodStr = req.substring(0, sp1);
    String url = req.substring(sp1 + 1, sp2);
    _currentVersion = req.substring(sp2 + 1) == "HTTP/1.0" ? 0 : 1;

    static const struct { const char *name; HTTPMethod m; } kMethods[] = {
        {"GET", HTTP_GET}, {"HEAD", HTTP_HEAD}, {"POST", HTTP_POST}, {"PUT", HTTP_PUT},
        {"PATCH", HTTP_PATCH}, {"DELETE", HTTP_DELETE}, {"OPTIONS", HTTP_OPTIONS}};
    _currentMethod = HTTP_GET;
    for (const auto &m : kMethods)
        if (methodStr == m.name) _currentMethod = m.m;

    String search;
    const int q = url.indexOf('?');
    if (q >= 0) {
        search = url.substring(q + 1);
        url = url.substring(0, q);
    }
    _currentUri = url;

    String contentType;
    long contentLength = -1;
    bool expectContinue = false;
    String line;
    for (;;) {
        if (!readLine(client, line, HTTP_MAX_DATA_WAIT)) return false;
        if (!line.length()) break;
        const int colon = line.indexOf(':');
        if (colon <= 0) continue;
        String name = line.substring(0, colon);
        String value = line.substring(colon + 1);
        name.trim();
        value.trim();
        if (_headers.size() < 64) _headers.emplace_back(name, value);
        if (name.equalsIgnoreCase("Host")) _hostHeader = value;
        else if (name.equalsIgnoreCase("Content-Type")) contentType = value;
        else if (name.equalsIgnoreCase("Content-Length")) contentLength = value.toInt();
        else if (name.equalsIgnoreCase("Expect")) expectContinue = value.equalsIgnoreCase("100-continue");
    }

    for (const Route &r : _routes) {
        if ((r.method == HTTP_ANY || r.method == _currentMethod) && r.uri == _currentUri) {
            _currentRoute = &r;
            break;
        }
    }
    parseArguments(search);

    if (contentLength <= 0) return true;
    if (expectContinue && _currentVersion) client.print("HTTP/1.1 100 Continue\r\n\r\n");
    String lowerType = contentType;
    lowerType.toLowerCase();
    if (lowerType.startsWith("multipart/form-data")) {
        const String boundary = headerParam(contentType, "boundary");
        if (!boundary.length() || !parseMultipart(client, boundary, (size_t)contentLength)) {
            send(400, "text/plain", "Bad Request");
            return false;
        }
        return true;
    }
    if ((size_t)contentLength > kMaxBodyBytes) {
        send(413, "text/plain", "Request Entity Too Large");
        return false;
    }
    std::string body;
    body.reserve((size_t)contentLength);
    uint8_t buf[4096];
    while (body.size() < (size_t)contentLength) {
        if (!client.waitAvailable(HTTP_MAX_POST_WAIT)) return false;
        const int n = client.read(buf, std::min(sizeof buf, (size_t)contentLength - body.size()));
        if (n > 0) body.append((const char *)buf, (size_t)n);
    }
    const String data(body.c_str(), body.size());
    if (lowerType.startsWith("application/x-www-form-urlencoded")) parseArguments(data);
    else _args.emplace_back(String("plain"), data);
    return true;
}

void WebServer::parseArguments(const String &data) {
    int pos = 0;
    while (pos < (int)data.length()) {
        int amp = data.indexOf('&', pos);
        if (amp < 0) amp = data.length();
        const String pair = data.substring(pos, amp);
        pos = amp + 1;
        if (!pair.length()) continue;
        const int eq = pair.indexOf('=');
        if (eq < 0) _args.emplace_back(urlDecode(pair), String());
        else _args.emplace_back(urlDecode(pair.substring(0, eq)), urlDecode(pair.substring(eq + 1)));
    }
}

void WebServer::callUpload(const Route *route) {
    if (route && route->ufn) route->ufn();
    else if (_fileUploadHandler) _fileUploadHandler();
}

// Fields become args; file parts go to the route's upload handler in
// HTTP_UPLOAD_BUFLEN pieces, bracketed by START and END (ABORTED if the body
// ends early).
bool WebServer::parseMultipart(WiFiClient &client, const String &boundary, size_t len) {
    size_t remaining = len;
    std::string win;  // read but not yet consumed
    auto more = [&]() -> bool {
        if (!remaining || !client.waitAvailable(HTTP_MAX_POST_WAIT)) return false;
        char buf[4096];
        const int n = client.read((uint8_t *)buf, std::min(sizeof buf, remaining));
        if (n <= 0) return client.connected();
        win.append(buf, (size_t)n);
        remaining -= (size_t)n;
        return true;
    };
    auto takeLine = [&](String &line) -> bool {
        for (;;) {
            const size_t eol = win.find("\r\n");
            if (eol != std::string::npos) {
                line = String(win.c_str(), eol);
                win.erase(0, eol + 2);
                return true;
            }
            if (win.size() > 8192 || !more()) return false;
        }
    };

    const std::string delim = std::string("--") + boundary.c_str();
    const std::string marker = "\r\n" + delim;
    String line;
    do {  // preamble
        if (!takeLine(line)) return false;
    } while (std::string(line.c_str()) != delim);

    HTTPUpload &up = upload();
    for (;;) {
        String name, filename, type;
        bool isFile = false;
        for (;;) {
            if (!takeLine(line)) return false;
            if (!line.length()) break;
            const int colon = line.indexOf(':');
            if (colon <= 0) continue;
            String h = line.substring(0, colon);
            String v = line.substring(colon + 1);
            h.trim();
            v.trim();
            if (h.equalsIgnoreCase("Content-Disposition")) {
                name = headerParam(v, "name");
                isFile = v.indexOf("filename=") >= 0;
                filename = headerParam(v, "filename");
            } else if (h.equalsIgnoreCase("Content-Type")) {
                type = v;
            }
        }
        if (isFile) {
            up.status = UPLOAD_FILE_START;
            up.name = name;
            up.filename = filename;
            up.type = type.length() ? type : String("text/plain");
            up.totalSize = 0;
            up.currentSize = 0;
            callUpload(_currentRoute);
        }
        std::string value;
        bool found = false;
        for (;;) {
            const size_t at = win.find(marker);
            // Everything that cannot be the start of the marker is part data.
            const size_t safe = at != std::string::npos ? at
                                : win.size() > marker.size() ? win.size() - marker.size() : 0;
            size_t off = 0;
            while (off < safe) {
                if (isFile) {
                    const size_t n = std::min(safe - off, (size_t)HTTP_UPLOAD_BUFLEN - up.currentSize);
                    memcpy(up.buf + up.currentSize, win.data() + off, n);
                    up.currentSize += n;
                    off += n;
                    if (up.currentSize == HTTP_UPLOAD_BUFLEN) {
                        up.status = UPLOAD_FILE_WRITE;
                        callUpload(_currentRoute);
                        up.totalSize += up.currentSize;
                        up.currentSize = 0;
                    }
                } else {
                    if (value.size() < kMaxBodyBytes) value.append(win, off, std::min(safe - off, kMaxBodyBytes - value.size()));
                    off = safe;
                }
            }
            win.erase(0, safe);
            if (at != std::string::npos) {
                win.erase(0, marker.size());
                found = true;
                break;
            }
            if (!more()) break;
        }
        if (!found) {
            if (isFile) {
                up.status = UPLOAD_FILE_ABORTED;
                callUpload(_currentRoute);
            }
            return false;
        }
        if (isFile) {
            if (up.currentSize) {
                up.status = UPLOAD_FILE_WRITE;
                callUpload(_currentRoute);
                up.totalSize += up.currentSize;
                up.currentSize = 0;
            }
            up.status = UPLOAD_FILE_END;
            callUpload(_currentRoute);
        } else {
            _args.emplace_back(name, String(value.c_str(), value.size()));
        }
        // "--" closes the body; CRLF starts the next part.
        while (win.size() < 2)
            if (!more()) return false;
        if (win.compare(0, 2, "--") == 0) break;
        win.erase(0, 2);
    }
    // Drain the epilogue so the response is not cut off by a reset.
    while (remaining && more()) win.clear();
    return true;
}

void WebServer::handleRequest() {
    if (_currentRoute) _currentRoute->fn();
    else if (_notFoundHandler) _notFoundHandler();
    else send(404, "text/plain", String("Not found: ") + _currentUri);
}

String WebServer::arg(const String &name) {
    for (auto &a : _args)
        if (a.first == name) return a.second;
    return String();
}

String WebServer::arg(int i) { return i >= 0 && i < (int)_args.size() ? _args[i].second : String(); }

String WebServer::argName(int i) { return i >= 0 && i < (int)_args.size() ? _args[i].first : String(); }

bool WebServer::hasArg(const String &name) {
    for (auto &a : _args)
        if (a.first == name) return true;
    return false;
}

// Every request header is kept, so there is nothing to select.
void WebServer::collectHeaders(const char *[], const size_t) {}

String WebServer::header(const String &name) {
    for (auto &h : _headers)
        if (h.first.equalsIgnoreCase(name)) return h.second;
    return String();
}

String WebServer::header(int i) { return i >= 0 && i < (int)_headers.size() ? _headers[i].second : String(); }

String WebServer::headerName(int i) { return i >= 0 && i < (int)_headers.size() ? _headers[i].first : String(); }

bool WebServer::hasHeader(const String &name) { return header(name).length() > 0; }

String WebServer::urlDecode(const String &text) {
    String out;
    const size_t n = text.length();
    for (size_t i = 0; i < n; i++) {
        const char c = text[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < n && isxdigit((unsigned char)text[i + 1]) && isxdigit((unsigned char)text[i + 2])) {
            const char hex[3] = {text[i + 1], text[i + 2], 0};
            out += (char)strtol(hex, nullptr, 16);
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

void WebServer::sendHeader(const String &name, const String &value, bool first) {
    const String line = name + ": " + value + "\r\n";
    _responseHeaders = first ? line + _responseHeaders : _responseHeaders + line;
}

void WebServer::prepareHeader(String &response, int code, const char *contentType, size_t contentLength) {
    response = "HTTP/1." + String(_currentVersion) + " " + String(code) + " " + statusText(code) + "\r\n";
    sendHeader("Content-Type", contentType ? contentType : "text/html", true);
    if (_contentLength == CONTENT_LENGTH_NOT_SET) {
        sendHeader("Content-Length", String((unsigned long)contentLength));
    } else if (_contentLength != CONTENT_LENGTH_UNKNOWN) {
        sendHeader("Content-Length", String((unsigned long)_contentLength));
    } else if (_currentVersion) {
        _chunked = true;
        sendHeader("Accept-Ranges", "none");
        sendHeader("Transfer-Encoding", "chunked");
    }
    if (_corsEnabled) {
        sendHeader("Access-Control-Allow-Origin", "*");
        sendHeader("Access-Control-Allow-Methods", "*");
        sendHeader("Access-Control-Allow-Headers", "*");
    }
    sendHeader("Connection", "close");
    response += _responseHeaders;
    response += "\r\n";
    _responseHeaders = "";
    _contentLength = CONTENT_LENGTH_NOT_SET;
}

void WebServer::send(int code, const char *contentType, const String &content) {
    send_P(code, contentType, content.c_str(), content.length());
}

void WebServer::send_P(int code, PGM_P contentType, PGM_P content) {
    send_P(code, contentType, content, content ? strlen(content) : 0);
}

void WebServer::send_P(int code, PGM_P contentType, PGM_P content, size_t contentLength) {
    String header;
    prepareHeader(header, code, contentType, contentLength);
    _currentClient.write((const uint8_t *)header.c_str(), header.length());
    if (contentLength) sendContent(content, contentLength);
}

void WebServer::sendContent(const char *content, size_t contentLength) {
    if (!_chunked) {
        if (contentLength) _currentClient.write((const uint8_t *)content, contentLength);
        return;
    }
    // One write per chunk: size line, data and CRLF in separate segments
    // would each wait out the client's delayed ACK.
    char head[16];
    const int hn = snprintf(head, sizeof head, "%zx\r\n", contentLength);
    std::string frame;
    frame.reserve((size_t)hn + contentLength + 4);
    frame.append(head, (size_t)hn);
    frame.append(content ? content : "", contentLength);
    frame.append("\r\n");
    _currentClient.write((const uint8_t *)frame.data(), frame.size());
    if (!contentLength) _chunked = false;
}

void WebServer::finishResponse() {
    if (_chunked) sendContent("", 0);
}

// Half-close and let the client hang up first, so a request it is still
// sending cannot turn the close into a reset that eats the response.
void WebServer::lingerClose(WiFiClient &client) {
    if (client.fd() < 0) return;
    shutdown(client.fd(), SHUT_WR);
    _lingering.push_back(Pending{client, millis()});
}

void WebServer::serviceLingering() {
    for (size_t i = 0; i < _lingering.size(); i++) {
        WiFiClient &c = _lingering[i].client;
        uint8_t buf[512];
        while (c.available() > 0 && c.read(buf, sizeof buf) > 0) {}
        if (!c.connected() || millis() - _lingering[i].since > HTTP_MAX_CLOSE_WAIT) {
            c.stop();
            _lingering.erase(_lingering.begin() + i);
            i--;
        }
    }
}
