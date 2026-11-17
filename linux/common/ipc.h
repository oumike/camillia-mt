// SPDX-License-Identifier: GPL-3.0-or-later
// Line protocol between the CardputerZero radio daemon (camillia-czero) and
// its screen client (camillia-czero-ui), over a Unix stream socket.
//
// One record per line, fields separated by tabs. Text fields are escaped
// (backslash, tab and newline) so a message can carry any of them.
//
// Daemon -> client (the current state is replayed on connect, then live):
//   I  me long short region preset version role freqMHz rxOnly
//   C  idx name                      a configured channel
//   N  id short long hasKey lastHeard rssi snr hops     node upsert
//   M  pktId epoch conv from to state rssi snr hops text  message
//   S  pktId state                   state change of a message we sent
//   G  fix lat lon alt sats          our GPS
//   E                                end of the initial replay
//
// Client -> daemon:
//   T  conv text                     send text in a conversation
//   P                                broadcast our NodeInfo now
//
// conv is "c<idx>" for a channel or "d<8 hex>" for a direct message. Node ids
// are 8 lower-case hex digits. lastHeard and epoch are Unix seconds (0 when
// unknown). Message states:
//   r received   p sent, waiting for an ACK   s sent
//   R relayed by a neighbour (a broadcast's implicit ACK)
//   A acknowledged   N refused (NAK)   F failed to send
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace ipc {

// /run/camillia when the systemd unit created it, else the user's runtime
// dir, else /tmp. Both programs resolve the same path the same way.
inline std::string defaultSocketPath() {
    if (const char *env = getenv("CAMILLIA_SOCKET"); env && *env) return env;
    struct stat st;
    if (stat("/run/camillia", &st) == 0 && S_ISDIR(st.st_mode)) return "/run/camillia/camillia.sock";
    if (const char *rt = getenv("XDG_RUNTIME_DIR"); rt && *rt) return std::string(rt) + "/camillia.sock";
    return "/tmp/camillia-" + std::to_string(getuid()) + ".sock";
}

inline std::string esc(const std::string &s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\t') o += "\\t";
        else if (c == '\n') o += "\\n";
        else if (c != '\r') o += c;
    }
    return o;
}

inline std::string unesc(const std::string &s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '\\' || i + 1 == s.size()) { o += s[i]; continue; }
        char n = s[++i];
        o += n == 't' ? '\t' : n == 'n' ? '\n' : n;
    }
    return o;
}

// Splits on tabs and unescapes every field.
inline std::vector<std::string> split(const std::string &line) {
    std::vector<std::string> f;
    size_t start = 0;
    for (;;) {
        size_t tab = line.find('\t', start);
        f.push_back(unesc(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start)));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    return f;
}

// Joins fields with tabs. Free text must already have been through esc().
template <typename... A>
inline std::string join(const A &...parts) {
    std::string o;
    bool first = true;
    ((o += (first ? "" : "\t"), o += parts, first = false), ...);
    return o;
}

inline std::string hex32(uint32_t v) {
    char b[9];
    snprintf(b, sizeof b, "%08x", v);
    return b;
}

inline std::string fmt(const char *f, double v) {
    char b[32];
    snprintf(b, sizeof b, f, v);
    return b;
}

}  // namespace ipc
