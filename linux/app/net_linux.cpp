// SPDX-License-Identifier: GPL-3.0-or-later
// WiFi, WiFiClient and WiFiClientSecure (app/shim) over the system network:
// the OS owns the interfaces, these only use the connection it has.
#include "WiFi.h"
#include "WiFiClientSecure.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

WiFiClass WiFi;

static uint32_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000);
}

static void waitFd(int fd, short events, uint32_t ms) {
    struct pollfd p = {fd, events, 0};
    poll(&p, 1, (int)ms);
}

static bool resolveHost(const char *host, IPAddress &ip) {
    ip = IPAddress();
    if (!host || !*host) return false;
    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) return false;
    ip = IPAddress((uint32_t)((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(res);
    return (uint32_t)ip != 0;
}

// ── The system's connection ──────────────────────────────────────────────────

namespace {

struct SysIface {
    bool up = false;
    std::string name;
    uint32_t addr = 0;
};

// A usable IPv4 address on an up, non-loopback interface; wlan0 preferred.
// getifaddrs() is a netlink dump and status() is polled from the main loop,
// so the answer is reused for a second.
SysIface sysIface() {
    static SysIface cached;
    static uint32_t at = 0;
    static bool valid = false;
    const uint32_t now = nowMs();
    if (valid && now - at < 1000) return cached;
    SysIface best;
    struct ifaddrs *list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (struct ifaddrs *i = list; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
            if ((i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP) || !(i->ifa_flags & IFF_RUNNING))
                continue;
            const uint32_t a = ((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr;
            if (a == 0 || (ntohl(a) >> 16) == 0xA9FE) continue;  // none, or link-local
            const bool wlan = strcmp(i->ifa_name, "wlan0") == 0;
            if (!best.up || wlan) {
                best.up = true;
                best.name = i->ifa_name;
                best.addr = a;
                if (wlan) break;
            }
        }
        freeifaddrs(list);
    }
    cached = best;
    at = now;
    valid = true;
    return best;
}

// The current Wi-Fi network from NetworkManager, refreshed off the main loop:
// nmcli takes tens of milliseconds and can stall far longer while NM is busy.
struct WifiInfo {
    std::mutex lock;
    std::string ssid;
    int signal = -1;  // nmcli SIGNAL, percent
    std::atomic<uint32_t> lastAsked{0};
    std::once_flag started;
};
WifiInfo &wifiInfo() {
    static WifiInfo w;
    return w;
}

void refreshWifiInfo() {
    std::string ssid;
    int signal = -1;
    FILE *p = popen("nmcli -t -f ACTIVE,SSID,SIGNAL dev wifi list --rescan no 2>/dev/null", "r");
    if (p) {
        char line[256];
        while (fgets(line, sizeof line, p)) {
            if (strncmp(line, "yes:", 4) != 0) continue;
            // SSID:SIGNAL with ':' and '\' in the SSID escaped by a backslash.
            std::string s;
            const char *c = line + 4;
            for (; *c && *c != ':' && *c != '\n'; c++) {
                if (*c == '\\' && c[1]) c++;
                s += *c;
            }
            if (*c == ':') signal = atoi(c + 1);
            ssid = s;
            break;
        }
        pclose(p);
    }
    WifiInfo &w = wifiInfo();
    std::lock_guard<std::mutex> g(w.lock);
    w.ssid = ssid;
    w.signal = signal;
}

void wifiInfoWanted() {
    WifiInfo &w = wifiInfo();
    w.lastAsked = nowMs();
    std::call_once(w.started, [] {
        std::thread([] {
            WifiInfo &w = wifiInfo();
            for (;;) {
                // Only while someone is asking; an idle app runs no nmcli.
                if (nowMs() - w.lastAsked < 60000) refreshWifiInfo();
                std::this_thread::sleep_for(std::chrono::seconds(15));
            }
        }).detach();
    });
}

// Signal level of an interface in dBm from /proc/net/wireless, 0 if absent.
int procWirelessLevel(const std::string &iface) {
    FILE *f = fopen("/proc/net/wireless", "r");
    if (!f) return 0;
    char line[256];
    int level = 0;
    while (fgets(line, sizeof line, f)) {
        char name[32];
        int status;
        float link, lvl;
        if (sscanf(line, " %31[^:]: %x %f %f", name, &status, &link, &lvl) == 4 && iface == name) {
            level = (int)lvl;
            break;
        }
    }
    fclose(f);
    return level;
}

}  // namespace

wl_status_t WiFiClass::begin(const char *, const char *, int32_t, const uint8_t *, bool) {
    if (_mode == WIFI_OFF) _mode = WIFI_STA;
    else if (_mode == WIFI_AP) _mode = WIFI_AP_STA;
    return status();
}

wl_status_t WiFiClass::status() {
    if (_mode == WIFI_OFF) return WL_NO_SHIELD;
    if (!(_mode & WIFI_STA)) return WL_DISCONNECTED;
    return sysIface().up ? WL_CONNECTED : WL_DISCONNECTED;
}

IPAddress WiFiClass::localIP() {
    if (!(_mode & WIFI_STA)) return IPAddress();
    const SysIface i = sysIface();
    return i.up ? IPAddress(i.addr) : IPAddress();
}

String WiFiClass::macAddress() {
    const SysIface i = sysIface();
    const std::string path = "/sys/class/net/" + (i.up ? i.name : std::string("wlan0")) + "/address";
    char mac[32] = "00:00:00:00:00:00";
    if (FILE *f = fopen(path.c_str(), "r")) {
        if (fscanf(f, "%31s", mac) != 1) strcpy(mac, "00:00:00:00:00:00");
        fclose(f);
    }
    String s(mac);
    s.toUpperCase();
    return s;
}

const char *WiFiClass::getHostname() {
    static char name[65];
    if (gethostname(name, sizeof name - 1) != 0) strcpy(name, "camillia");
    name[sizeof name - 1] = '\0';
    return name;
}

int WiFiClass::hostByName(const char *host, IPAddress &ip) { return resolveHost(host, ip) ? 1 : 0; }

String WiFiClass::SSID() const {
    wifiInfoWanted();
    if (!(_mode & WIFI_STA)) return String();
    WifiInfo &w = wifiInfo();
    std::lock_guard<std::mutex> g(w.lock);
    return String(w.ssid.c_str());
}

int32_t WiFiClass::RSSI() {
    wifiInfoWanted();
    if (!(_mode & WIFI_STA)) return 0;
    const SysIface i = sysIface();
    const int level = procWirelessLevel(i.up ? i.name : std::string("wlan0"));
    if (level != 0) return level;
    WifiInfo &w = wifiInfo();
    std::lock_guard<std::mutex> g(w.lock);
    return (w.ssid.empty() || w.signal < 0) ? 0 : w.signal / 2 - 100;
}

// ── Sockets ──────────────────────────────────────────────────────────────────

struct TlsSession {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt ca;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context entropy;
    int fd = -1;
    TlsSession() {
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_x509_crt_init(&ca);
        mbedtls_ctr_drbg_init(&drbg);
        mbedtls_entropy_init(&entropy);
    }
    ~TlsSession() {
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_x509_crt_free(&ca);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
    }
};

struct NetSocket {
    int fd = -1;
    bool eof = false;  // peer closed or the connection failed; buffered data stays readable
    std::unique_ptr<TlsSession> tls;
    uint8_t rx[16384];
    size_t rxLen = 0, rxOff = 0;

    explicit NetSocket(int f) : fd(f) {}
    ~NetSocket() { close(); }

    void close() {
        if (tls && fd >= 0) mbedtls_ssl_close_notify(&tls->ssl);  // best effort; never waits
        tls.reset();
        if (fd >= 0) ::close(fd);
        fd = -1;
        eof = true;
        rxLen = rxOff = 0;
    }

    size_t buffered() const { return rxLen - rxOff; }

    // >0 bytes, 0 end of stream, -1 would block (wait for *ev), -2 failed.
    ssize_t recvSome(uint8_t *buf, size_t len, short *ev) {
        if (!tls) {
            const ssize_t n = recv(fd, buf, len, MSG_DONTWAIT);
            if (n >= 0) return n;
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                *ev = POLLIN;
                return -1;
            }
            return -2;
        }
        for (;;) {
            const int r = mbedtls_ssl_read(&tls->ssl, buf, len);
            if (r > 0) return r;
            if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
            if (r == MBEDTLS_ERR_SSL_WANT_READ) { *ev = POLLIN; return -1; }
            if (r == MBEDTLS_ERR_SSL_WANT_WRITE) { *ev = POLLOUT; return -1; }
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
            if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
#endif
            return -2;
        }
    }

    // Makes data available, waiting up to waitMs for some to arrive.
    bool fill(uint32_t waitMs) {
        if (buffered()) return true;
        if (fd < 0 || eof) return false;
        const uint32_t start = nowMs();
        for (;;) {
            short ev = 0;
            const ssize_t n = recvSome(rx, sizeof rx, &ev);
            if (n > 0) {
                rxLen = (size_t)n;
                rxOff = 0;
                return true;
            }
            if (n == 0 || n == -2) {
                eof = true;
                return false;
            }
            const uint32_t spent = nowMs() - start;
            if (spent >= waitMs) return false;
            waitFd(fd, ev, waitMs - spent);
        }
    }

    // Sends everything unless stallMs pass with no progress or the connection fails.
    size_t sendAll(const uint8_t *buf, size_t len, uint32_t stallMs) {
        size_t off = 0;
        uint32_t last = nowMs();
        while (off < len && fd >= 0) {
            short ev = POLLOUT;
            ssize_t n;
            if (tls) {
                const int r = mbedtls_ssl_write(&tls->ssl, buf + off, len - off);
                if (r > 0) n = r;
                else if (r == MBEDTLS_ERR_SSL_WANT_WRITE) n = -1;
                else if (r == MBEDTLS_ERR_SSL_WANT_READ) { n = -1; ev = POLLIN; }
                else n = -2;
            } else {
                n = ::send(fd, buf + off, len - off, MSG_NOSIGNAL | MSG_DONTWAIT);
                if (n < 0) n = (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? -1 : -2;
            }
            if (n > 0) {
                off += (size_t)n;
                last = nowMs();
                continue;
            }
            if (n == -2) {
                eof = true;
                break;
            }
            const uint32_t spent = nowMs() - last;
            if (spent >= stallMs) break;
            waitFd(fd, ev, stallMs - spent);
        }
        return off;
    }
};

WiFiClient::WiFiClient() {}

WiFiClient::WiFiClient(int fd) {
    if (fd < 0) return;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    _sock = std::make_shared<NetSocket>(fd);
}

WiFiClient::~WiFiClient() {}

int WiFiClient::connect(const char *host, uint16_t port, int32_t timeoutMs) {
    IPAddress ip;
    if (!resolveHost(host, ip)) return 0;
    return WiFiClient::connect(ip, port, timeoutMs);
}

int WiFiClient::connect(IPAddress ip, uint16_t port, int32_t timeoutMs) {
    stop();
    const uint32_t limit = timeoutMs > 0 ? (uint32_t)timeoutMs : _timeoutMs;
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return 0;
    struct sockaddr_in sa = {};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = (uint32_t)ip;
    int rc = ::connect(fd, (struct sockaddr *)&sa, sizeof sa);
    if (rc != 0 && errno == EINPROGRESS) {
        struct pollfd p = {fd, POLLOUT, 0};
        const uint32_t start = nowMs();
        int pr;
        do {
            const uint32_t spent = nowMs() - start;
            pr = spent >= limit ? 0 : poll(&p, 1, (int)(limit - spent));
        } while (pr < 0 && errno == EINTR);
        int err = ETIMEDOUT;
        socklen_t el = sizeof err;
        if (pr > 0) getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
        rc = err == 0 ? 0 : -1;
    }
    if (rc != 0) {
        ::close(fd);
        return 0;
    }
    _sock = std::make_shared<NetSocket>(fd);
    return 1;
}

size_t WiFiClient::write(const uint8_t *buf, size_t size) {
    if (!_sock || _sock->fd < 0 || !buf) return 0;
    return _sock->sendAll(buf, size, _timeoutMs);
}

size_t WiFiClient::write(Stream &stream) {
    uint8_t buf[1460];
    size_t total = 0;
    for (;;) {
        const size_t n = stream.readBytes(buf, sizeof buf);
        if (n == 0) break;
        const size_t w = write(buf, n);
        total += w;
        if (w != n) break;
    }
    return total;
}

int WiFiClient::available() {
    if (!_sock || _sock->fd < 0) return 0;
    if (!_sock->buffered()) _sock->fill(0);
    int n = (int)_sock->buffered();
    int queued = 0;
    if (!_sock->tls && ioctl(_sock->fd, FIONREAD, &queued) == 0 && queued > 0) n += queued;
    return n;
}

int WiFiClient::read() {
    uint8_t c;
    return read(&c, 1) == 1 ? c : -1;
}

int WiFiClient::read(uint8_t *buf, size_t size) {
    if (!_sock || _sock->fd < 0) return -1;
    if (!_sock->buffered()) _sock->fill(0);
    const size_t n = std::min(size, _sock->buffered());
    memcpy(buf, _sock->rx + _sock->rxOff, n);
    _sock->rxOff += n;
    return (int)n;
}

size_t WiFiClient::readBytes(char *buf, size_t size) {
    if (!_sock) return 0;
    const uint32_t start = nowMs();
    size_t got = 0;
    while (got < size && _sock->fd >= 0) {
        const uint32_t spent = nowMs() - start;
        if (!_sock->fill(spent >= _timeout ? 0 : (uint32_t)(_timeout - spent))) break;
        const int n = read((uint8_t *)buf + got, size - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    return got;
}

int WiFiClient::peek() {
    if (!_sock || _sock->fd < 0) return -1;
    if (!_sock->buffered()) _sock->fill(0);
    return _sock->buffered() ? _sock->rx[_sock->rxOff] : -1;
}

bool WiFiClient::waitAvailable(uint32_t timeoutMs) { return _sock && _sock->fd >= 0 && _sock->fill(timeoutMs); }

void WiFiClient::stop() {
    if (_sock) _sock->close();
    _sock.reset();
}

uint8_t WiFiClient::connected() {
    if (!_sock || _sock->fd < 0) return 0;
    if (_sock->buffered()) return 1;
    if (_sock->eof) return 0;
    if (_sock->tls) {
        _sock->fill(0);
        return _sock->buffered() || !_sock->eof;
    }
    uint8_t b;
    const ssize_t n = recv(_sock->fd, &b, 1, MSG_PEEK | MSG_DONTWAIT);
    if (n > 0) return 1;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 1;
    _sock->eof = true;
    return 0;
}

int WiFiClient::fd() const { return _sock ? _sock->fd : -1; }

int WiFiClient::setNoDelay(bool nodelay) {
    const int v = nodelay ? 1 : 0;
    return fd() < 0 ? -1 : setsockopt(fd(), IPPROTO_TCP, TCP_NODELAY, &v, sizeof v);
}

bool WiFiClient::getNoDelay() {
    int v = 0;
    socklen_t l = sizeof v;
    return fd() >= 0 && getsockopt(fd(), IPPROTO_TCP, TCP_NODELAY, &v, &l) == 0 && v;
}

int WiFiClient::setTimeout(uint32_t seconds) {
    setTimeoutMs(seconds * 1000);
    return 0;
}

static bool sockName(int fd, bool peer, IPAddress *ip, uint16_t *port) {
    struct sockaddr_in sa = {};
    socklen_t l = sizeof sa;
    if (fd < 0 || (peer ? getpeername(fd, (struct sockaddr *)&sa, &l) : getsockname(fd, (struct sockaddr *)&sa, &l)) != 0 ||
        sa.sin_family != AF_INET)
        return false;
    if (ip) *ip = IPAddress((uint32_t)sa.sin_addr.s_addr);
    if (port) *port = ntohs(sa.sin_port);
    return true;
}

IPAddress WiFiClient::remoteIP() const {
    IPAddress ip;
    sockName(fd(), true, &ip, nullptr);
    return ip;
}

uint16_t WiFiClient::remotePort() const {
    uint16_t p = 0;
    sockName(fd(), true, nullptr, &p);
    return p;
}

IPAddress WiFiClient::localIP() const {
    IPAddress ip;
    sockName(fd(), false, &ip, nullptr);
    return ip;
}

uint16_t WiFiClient::localPort() const {
    uint16_t p = 0;
    sockName(fd(), false, nullptr, &p);
    return p;
}

// ── TLS ──────────────────────────────────────────────────────────────────────

static const char kCaBundle[] = "/etc/ssl/certs/ca-certificates.crt";

static int bioSend(void *ctx, const unsigned char *buf, size_t len) {
    const ssize_t n = ::send(((TlsSession *)ctx)->fd, buf, len, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (n >= 0) return (int)n;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return errno == EPIPE || errno == ECONNRESET ? MBEDTLS_ERR_NET_CONN_RESET : MBEDTLS_ERR_NET_SEND_FAILED;
}

static int bioRecv(void *ctx, unsigned char *buf, size_t len) {
    const ssize_t n = recv(((TlsSession *)ctx)->fd, buf, len, MSG_DONTWAIT);
    if (n >= 0) return (int)n;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return MBEDTLS_ERR_SSL_WANT_READ;
    return errno == ECONNRESET ? MBEDTLS_ERR_NET_CONN_RESET : MBEDTLS_ERR_NET_RECV_FAILED;
}

int WiFiClientSecure::connect(IPAddress ip, uint16_t port, int32_t timeoutMs) {
    if (!WiFiClient::connect(ip, port, timeoutMs)) return 0;
    return startTls(ip.toString().c_str(), timeoutMs);
}

int WiFiClientSecure::connect(const char *host, uint16_t port, int32_t timeoutMs) {
    IPAddress ip;
    if (!resolveHost(host, ip) || !WiFiClient::connect(ip, port, timeoutMs)) return 0;
    return startTls(host, timeoutMs);
}

int WiFiClientSecure::startTls(const char *host, int32_t) {
    static std::once_flag psaOnce;
    std::call_once(psaOnce, [] { psa_crypto_init(); });

    std::unique_ptr<TlsSession> t(new TlsSession());
    t->fd = _sock->fd;
    static const char pers[] = "camillia-tls";
    int r = mbedtls_ctr_drbg_seed(&t->drbg, mbedtls_entropy_func, &t->entropy, (const unsigned char *)pers,
                                  sizeof pers - 1);
    if (r == 0)
        r = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT);
    if (r == 0) {
        int authmode = MBEDTLS_SSL_VERIFY_REQUIRED;
        if (_insecure) {
            authmode = MBEDTLS_SSL_VERIFY_NONE;
        } else if (!_ca.empty()) {
            r = mbedtls_x509_crt_parse(&t->ca, (const unsigned char *)_ca.c_str(), _ca.size() + 1);
        } else if (mbedtls_x509_crt_parse_file(&t->ca, kCaBundle) < 0 || !t->ca.version) {
            static std::once_flag warned;
            std::call_once(warned, [] { Serial.printf("[tls] no CA bundle at %s; not verifying servers\n", kCaBundle); });
            authmode = MBEDTLS_SSL_VERIFY_NONE;
        }
        mbedtls_ssl_conf_authmode(&t->conf, authmode);
        if (authmode != MBEDTLS_SSL_VERIFY_NONE) mbedtls_ssl_conf_ca_chain(&t->conf, &t->ca, nullptr);
        mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &t->drbg);
    }
    if (r == 0) r = mbedtls_ssl_setup(&t->ssl, &t->conf);
    if (r == 0) r = mbedtls_ssl_set_hostname(&t->ssl, host);
    if (r == 0) {
        mbedtls_ssl_set_bio(&t->ssl, t.get(), bioSend, bioRecv, nullptr);
        const uint32_t start = nowMs();
        while ((r = mbedtls_ssl_handshake(&t->ssl)) != 0) {
            short ev;
            if (r == MBEDTLS_ERR_SSL_WANT_READ) ev = POLLIN;
            else if (r == MBEDTLS_ERR_SSL_WANT_WRITE) ev = POLLOUT;
            else break;
            const uint32_t spent = nowMs() - start;
            if (spent >= _handshakeMs) {
                r = MBEDTLS_ERR_SSL_TIMEOUT;
                break;
            }
            waitFd(t->fd, ev, _handshakeMs - spent);
        }
    }
    _lastError = r;
    if (r != 0) {
        char msg[128];
        mbedtls_strerror(r, msg, sizeof msg);
        Serial.printf("[tls] %s: %s\n", host, msg);
        stop();
        return 0;
    }
    _sock->tls = std::move(t);
    return 1;
}

int WiFiClientSecure::lastError(char *buf, size_t size) {
    if (buf && size) mbedtls_strerror(_lastError, buf, size);
    return _lastError;
}
