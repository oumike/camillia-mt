#include "admin_client.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace AdminClient {

using namespace AdminProto;

// ── Timings ──────────────────────────────────────────────────────────────────
// The target regenerates its session passkey when the cached one is older than
// 150 s at response time and accepts it for 300 s. Refreshing at 120 s keeps us
// clear of both edges without a round trip before every write.
static constexpr uint32_t kRequestTimeoutMs = 30000;
static constexpr uint32_t kPasskeyMaxAgeMs  = 120000;
static constexpr size_t   kMaxPayload       = 240;

// ── Routing errors ───────────────────────────────────────────────────────────
const char *routingErrorLabel(uint32_t reason) {
    switch (reason) {
        case 0:  return "none";
        case 1:  return "no route";
        case 2:  return "got nak";
        case 3:  return "timeout";
        case 4:  return "no interface";
        case 5:  return "max retransmit";
        case 6:  return "no channel";
        case 7:  return "too large";
        case 8:  return "no response";
        case 9:  return "duty cycle limit";
        case 32: return "bad request";
        case 33: return "not authorized";
        case 34: return "pki failed";
        case 35: return "pki unknown pubkey";
        case 36: return "admin bad session key";
        case 37: return "admin public key unauthorized";
        default: return "unknown";
    }
}

bool errorMeansUnauthorized(uint32_t reason) {
    // The two the gate acts on. A timeout is deliberately not here: out of
    // range, or a broker that is down, is not the same as being refused.
    return reason == 33 || reason == 37;
}

// ── Settable fields ──────────────────────────────────────────────────────────
// Field numbers verified against meshtastic/protobufs config.proto rather than
// written from memory -- a wrong number here does not fail, it writes the right
// value into the wrong setting on somebody else's radio.
namespace {

enum ValueKind : uint8_t { V_UINT, V_BOOL, V_INT, V_STRING };

struct SettableField {
    uint32_t   block;       // ConfigBlock
    const char *name;
    uint32_t   field;
    ValueKind  kind;
    uint64_t   maxValue;    // V_UINT/V_INT only; 0 = unbounded
};

const SettableField kSettable[] = {
    // LoRaConfig
    { CONFIG_LORA, "modem_preset",        2,  V_UINT,   8 },
    { CONFIG_LORA, "region",              7,  V_UINT,  30 },
    { CONFIG_LORA, "hop_limit",           8,  V_UINT,   7 },
    { CONFIG_LORA, "tx_enabled",          9,  V_BOOL,   0 },
    { CONFIG_LORA, "tx_power",           10,  V_INT,   30 },
    { CONFIG_LORA, "channel_num",        11,  V_UINT, 255 },
    { CONFIG_LORA, "override_duty_cycle",12,  V_BOOL,   0 },
    { CONFIG_LORA, "rx_boosted_gain",    13,  V_BOOL,   0 },
    // DeviceConfig
    { CONFIG_DEVICE, "role",                     1,  V_UINT, 12 },
    { CONFIG_DEVICE, "rebroadcast_mode",         6,  V_UINT,  5 },
    { CONFIG_DEVICE, "node_info_broadcast_secs", 7,  V_UINT,  0 },
    { CONFIG_DEVICE, "tzdef",                   11,  V_STRING, 0 },
    // PositionConfig
    { CONFIG_POSITION, "position_broadcast_secs",  1, V_UINT, 0 },
    { CONFIG_POSITION, "gps_update_interval",      5, V_UINT, 0 },
    { CONFIG_POSITION, "position_flags",           7, V_UINT, 0 },
    { CONFIG_POSITION, "gps_mode",                13, V_UINT, 2 },
};

const SettableField *findSettable(uint32_t block, const char *name) {
    for (const auto &f : kSettable) {
        if (f.block == block && !strcmp(f.name, name)) return &f;
    }
    return nullptr;
}

bool parseValue(const SettableField &f, const char *text, uint64_t &out) {
    if (f.kind == V_BOOL) {
        if (!strcmp(text, "true")  || !strcmp(text, "1") || !strcmp(text, "on"))  { out = 1; return true; }
        if (!strcmp(text, "false") || !strcmp(text, "0") || !strcmp(text, "off")) { out = 0; return true; }
        return false;
    }
    char *end = nullptr;
    const long long v = strtoll(text, &end, 10);
    if (!end || *end) return false;
    if (f.kind == V_UINT && v < 0) return false;
    if (f.maxValue && (unsigned long long)(v < 0 ? -v : v) > f.maxValue) return false;
    // Negative int32 fields go on the wire as their unsigned two's complement,
    // which is what a plain (non-zigzag) protobuf int32 expects.
    out = (uint64_t)(int64_t)v;
    return true;
}

// ── Help ─────────────────────────────────────────────────────────────────────
const char *kHelp[] = {
    "Meta   help [cmd]  whoami  session  transport [rf|mqtt|auto]",
    "       verify  clear  exit",
    "Read   info                 firmware, hardware, role",
    "       owner                names, id, public key",
    "       config <block>       device position power network display",
    "                            lora bluetooth security",
    "       module <block>       mqtt serial telemetry neighborinfo ...",
    "       channel <0-7>        name, role, uplink/downlink",
    "Write  set owner long <text> | set owner short <text>",
    "       set lora <field> <value>",
    "            modem_preset region hop_limit tx_enabled tx_power",
    "            channel_num override_duty_cycle rx_boosted_gain",
    "       set device <field> <value>",
    "            role rebroadcast_mode node_info_broadcast_secs tzdef",
    "       set position <field> <value>",
    "       fixedpos <lat> <lon> [alt] | fixedpos clear",
    "       fav add|rm <!id>     ignore add|rm <!id>",
    "       time                 push our clock",
    "Danger reboot [secs]  shutdown [secs]",
    "       reset nodedb | reset config | reset device",
    "       (each needs `confirm` on the next line)",
};

struct HelpDetail { const char *cmd; const char *text; };
const HelpDetail kHelpDetail[] = {
    { "info",      "info - device metadata: firmware version, hardware model, role." },
    { "owner",     "owner - long and short name, node id, public key, licensed flag." },
    { "config",    "config <block> - reads one Config block whole. Writes splice it." },
    { "module",    "module <block> - reads one ModuleConfig block." },
    { "channel",   "channel <0-7> - name, role, key presence, uplink/downlink." },
    { "set",       "set <block> <field> <value> - read-modify-write; other fields kept." },
    { "fixedpos",  "fixedpos <lat> <lon> [alt] sets a fixed position; `clear` removes it." },
    { "fav",       "fav add|rm <!id> - the remote's favorite list." },
    { "ignore",    "ignore add|rm <!id> - the remote's ignore list." },
    { "time",      "time - pushes our clock to the remote (set_time_only)." },
    { "reboot",    "reboot [secs] - default 5. Needs `confirm`." },
    { "shutdown",  "shutdown [secs] - default 5. Needs `confirm`. Check canShutdown first." },
    { "reset",     "reset nodedb|config|device - destructive. Needs `confirm`." },
    { "transport", "transport [rf|mqtt|auto] - shows or sets the route. auto prefers RF." },
    { "session",   "session - age of the cached passkey, and the pending request if any." },
    { "verify",    "verify - re-runs the authorization probe." },
    { "whoami",    "whoami - the node this terminal is pointed at." },
};

int tokenize(char *line, char **argv, int maxArgs) {
    int argc = 0;
    char *p = line;
    while (*p && argc < maxArgs) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

}  // namespace

// ── Session ──────────────────────────────────────────────────────────────────

bool Session::open(uint32_t nodeId, const Hooks &hooks) {
    if (_open) close();
    _lines = (Line *)calloc(kMaxLines, sizeof(Line));
    if (!_lines) return false;   // no-PSRAM boards: fail visibly rather than half-open
    _open = true;
    _nodeId = nodeId;
    _hooks = hooks;
    _transport = TRANSPORT_AUTO;
    _lineCount = 0;
    _lineHead = 0;
    _pending = Pending{};
    _confirm = ConfirmArm{};
    // The passkey is deliberately not cleared: reopening the terminal inside the
    // remote's 300 s window should not cost a fresh round trip.
    print(LINE_INFO, "session open - type help");
    return true;
}

void Session::close() {
    _open = false;
    if (_lines) { free(_lines); _lines = nullptr; }
    _lineCount = 0;
    _lineHead = 0;
    _pending = Pending{};
    _confirm = ConfirmArm{};
}

const Line *Session::line(int i) const {
    if (!_lines || i < 0 || i >= _lineCount) return nullptr;
    return &_lines[(_lineHead + i) % kMaxLines];
}

void Session::print(uint8_t kind, const char *fmt, ...) {
    if (!_lines) return;
    Line *slot;
    if (_lineCount < kMaxLines) {
        slot = &_lines[(_lineHead + _lineCount) % kMaxLines];
        _lineCount++;
    } else {
        slot = &_lines[_lineHead];
        _lineHead = (_lineHead + 1) % kMaxLines;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(slot->text, sizeof(slot->text), fmt, ap);
    va_end(ap);
    slot->kind = kind;
    _revision++;
}

Transport Session::resolveTransport() const {
    if (_transport != TRANSPORT_AUTO) return _transport;
    const bool rf = _hooks.rfRecent && _hooks.rfRecent(_nodeId, _hooks.ctx);
    const bool mq = _hooks.mqttUp && _hooks.mqttUp(_hooks.ctx);
    // RF on a tie, and RF whenever the node was heard on the air recently: it is
    // the route that does not depend on anyone else's infrastructure.
    if (rf) return TRANSPORT_RF;
    if (mq) return TRANSPORT_MQTT;
    return TRANSPORT_RF;
}

bool Session::passkeyFresh(uint32_t nowMs) const {
    if (!_passkeyLen) return false;
    return (uint32_t)(nowMs - _passkeyAtMs) < kPasskeyMaxAgeMs;
}

bool Session::sendEncoded(const uint8_t *buf, size_t len, uint32_t field,
                          bool destructive, bool isRead, uint32_t nowMs) {
    if (!_hooks.send) { print(LINE_ERR, "no transport"); return false; }

    const Transport via = resolveTransport();
    if (via == TRANSPORT_MQTT && _hooks.mqttUp && !_hooks.mqttUp(_hooks.ctx)) {
        print(LINE_ERR, "mqtt bridge is down");
        return false;
    }

    const uint32_t id = _hooks.send(_nodeId, via, buf, len, _hooks.ctx);
    if (!id) { print(LINE_ERR, "send failed (%s)", via == TRANSPORT_RF ? "rf" : "mqtt"); return false; }

    _pending = Pending{};
    _pending.active = true;
    _pending.requestId = id;
    _pending.sentAtMs = nowMs;
    _pending.field = field;
    _pending.via = via;
    _pending.destructive = destructive;
    _pending.isRead = isRead;
    print(LINE_INFO, "sent id %08lx via %s", (unsigned long)id,
          via == TRANSPORT_RF ? "rf" : "mqtt");
    return true;
}

void Session::service(uint32_t nowMs) {
    if (!_open || !_pending.active) return;
    if ((uint32_t)(nowMs - _pending.sentAtMs) < kRequestTimeoutMs) return;
    print(LINE_ERR, "timeout after %us - %s",
          (unsigned)(kRequestTimeoutMs / 1000), fieldName(_pending.field));
    // Deliberately no demotion here. A timeout means out of range, asleep, or a
    // broker that is down -- none of which is "not authorized".
    _pending = Pending{};
}

void Session::onRouting(uint32_t requestId, uint32_t errorReason, Transport via,
                        uint32_t nowMs) {
    if (!_open || !_pending.active || _pending.requestId != requestId) return;
    const char *viaName = (via == TRANSPORT_RF) ? "rf" : "mqtt";

    if (errorReason == 0) {
        // Writes produce no admin reply at all, only this ACK.
        if (!_pending.isRead) {
            print(LINE_OK, "ack (%s)", viaName);
            _pending = Pending{};
        }
        return;
    }

    print(LINE_ERR, "nak: %s (%s)", routingErrorLabel(errorReason), viaName);

    if (errorReason == 36 && !_pending.destructive && _passkeyLen) {
        // Stale session key. One retry, and only after a fresh read mints a new
        // one -- which is what dropping the cached key forces on the next
        // command. A destructive command never travels this path.
        print(LINE_INFO, "session key rejected - run the command again");
        _passkeyLen = 0;
    }
    if (errorMeansUnauthorized(errorReason)) {
        print(LINE_ERR, "authorization lost - closing");
        _pending = Pending{};
        // The UI watches this and demotes the peer; the session cannot demote
        // itself because it does not own the peer list.
        _open = false;
        return;
    }
    _pending = Pending{};
    (void)nowMs;
}

void Session::onAdminReply(uint32_t requestId, const uint8_t *payload, size_t len,
                           Transport via, uint32_t nowMs) {
    if (!_open || !_pending.active || _pending.requestId != requestId) return;

    Response r;
    if (!decodeResponse(payload, len, r)) {
        print(LINE_ERR, "undecodable reply");
        _pending = Pending{};
        return;
    }

    if (r.hasPasskey) {
        memcpy(_passkey, r.passkey, r.passkeyLen);
        _passkeyLen = r.passkeyLen;
        _passkeyAtMs = nowMs;
    }

    const char *viaName = (via == TRANSPORT_RF) ? "rf" : "mqtt";

    // A read-modify-write's GET has come back: splice the one field and send the
    // whole block straight back, unchanged in every other respect.
    if (_pending.splicePending && r.field == GET_CONFIG_RESPONSE && r.payload) {
        // r.payload is a Config message; the block we asked for is the field
        // inside it that the oneof selected.
        int wt = 0; const uint8_t *blockPtr = nullptr; size_t blockLen = 0; uint64_t v = 0;
        if (!findField(r.payload, r.payloadLen, _pending.spliceBlock, wt, blockPtr, blockLen, v)
            || wt != WT_LEN) {
            print(LINE_ERR, "reply carried no %s block",
                  configBlockName(_pending.spliceBlock));
            _pending = Pending{};
            return;
        }

        uint8_t spliced[kMaxPayload];
        const size_t sn = spliceVarint(blockPtr, blockLen, _pending.spliceField,
                                       _pending.spliceValue, spliced, sizeof(spliced));
        if (!sn) { print(LINE_ERR, "splice failed - nothing sent"); _pending = Pending{}; return; }

        // Rebuild Config { <block> = spliced }, then set_config { Config }.
        uint8_t cfg[kMaxPayload];
        const size_t cn = writeBytesField(cfg, sizeof(cfg), 0, _pending.spliceBlock,
                                          spliced, sn);
        uint8_t msg[kMaxPayload];
        size_t mn = cn ? encodeBytesCommand(msg, sizeof(msg), SET_CONFIG, cfg, cn,
                                            _passkeyLen ? _passkey : nullptr, _passkeyLen)
                       : 0;
        if (!mn) { print(LINE_ERR, "set_config too large - nothing sent"); _pending = Pending{}; return; }

        print(LINE_INFO, "splicing %s", _pending.spliceLabel);
        const uint32_t block = _pending.spliceBlock;
        _pending = Pending{};
        if (!sendEncoded(msg, mn, SET_CONFIG, false, false, nowMs)) return;
        (void)block;
        return;
    }

    print(LINE_OK, "%s (%s)", fieldName(r.field), viaName);
    if (r.payload && r.payloadLen) {
        print(LINE_PLAIN, "  %u bytes", (unsigned)r.payloadLen);
    } else if (r.varint) {
        print(LINE_PLAIN, "  %llu", (unsigned long long)r.varint);
    }
    _pending = Pending{};
}

// ── Command dispatch ─────────────────────────────────────────────────────────

bool Session::submit(const char *line, uint32_t nowMs) {
    if (!_open || !line) return false;

    char buf[kLineLen];
    snprintf(buf, sizeof(buf), "%s", line);
    // Trim trailing whitespace so "reboot " is still "reboot".
    for (int i = (int)strlen(buf) - 1; i >= 0 && (buf[i] == ' ' || buf[i] == '\r'); i--) buf[i] = '\0';
    if (!buf[0]) return true;

    print(LINE_ECHO, "> %s", buf);

    // A destructive command armed on the previous line: this one is the answer
    // and nothing else.
    if (_confirm.armed) {
        const bool yes = !strcmp(buf, "confirm");
        const ConfirmArm arm = _confirm;
        _confirm = ConfirmArm{};
        if (!yes) { print(LINE_INFO, "cancelled"); return true; }
        if (!passkeyFresh(nowMs)) {
            // Destructive commands never ride an auto-refreshed key: the refresh
            // is another round trip, and "I typed confirm four seconds ago" is
            // not consent for whatever the state is now.
            print(LINE_ERR, "no fresh session key - run a read first, then retry");
            return true;
        }
        uint8_t msg[kMaxPayload];
        const size_t n = encodeVarintCommand(msg, sizeof(msg), arm.field, arm.value,
                                             _passkey, _passkeyLen);
        if (!n) { print(LINE_ERR, "encode failed"); return true; }
        print(LINE_INFO, "%s", arm.what);
        sendEncoded(msg, n, arm.field, true, false, nowMs);
        return true;
    }

    char work[kLineLen];
    memcpy(work, buf, sizeof(work));
    char *argv[8];
    const int argc = tokenize(work, argv, 8);
    if (!argc) return true;

    if (cmdMeta(argc, argv, nowMs)) return true;

    if (_pending.active) {
        // One request in flight. Refused rather than queued: the operator is
        // typing, and a line that silently waits 30 s behind a timeout reads as
        // the terminal having ignored it.
        print(LINE_ERR, "busy - %s is still outstanding", fieldName(_pending.field));
        return true;
    }

    if (cmdRead(argc, argv, nowMs)) return true;
    if (cmdDanger(argc, argv, nowMs)) return true;
    if (cmdWrite(argc, argv, nowMs)) return true;

    print(LINE_ERR, "unknown command: %s (try help)", argv[0]);
    return true;
}

bool Session::cmdMeta(int argc, char **argv, uint32_t nowMs) {
    const char *c = argv[0];

    if (!strcmp(c, "help")) {
        if (argc >= 2) {
            const char *d = helpFor(argv[1]);
            print(d ? LINE_PLAIN : LINE_ERR, "%s", d ? d : "no help for that");
        } else {
            for (int i = 0; i < helpLineCount(); i++) print(LINE_PLAIN, "%s", helpLine(i));
        }
        return true;
    }
    if (!strcmp(c, "whoami")) {
        print(LINE_PLAIN, "peer !%08lx  transport %s", (unsigned long)_nodeId,
              _transport == TRANSPORT_RF ? "rf" : _transport == TRANSPORT_MQTT ? "mqtt" : "auto");
        return true;
    }
    if (!strcmp(c, "session")) {
        if (_passkeyLen) {
            print(LINE_PLAIN, "passkey %u bytes, age %lus",
                  (unsigned)_passkeyLen, (unsigned long)((nowMs - _passkeyAtMs) / 1000));
        } else {
            print(LINE_PLAIN, "no session key cached");
        }
        if (_pending.active) print(LINE_PLAIN, "pending %s", fieldName(_pending.field));
        return true;
    }
    if (!strcmp(c, "transport")) {
        if (argc >= 2) {
            if      (!strcmp(argv[1], "rf"))   _transport = TRANSPORT_RF;
            else if (!strcmp(argv[1], "mqtt")) _transport = TRANSPORT_MQTT;
            else if (!strcmp(argv[1], "auto")) _transport = TRANSPORT_AUTO;
            else { print(LINE_ERR, "transport: rf, mqtt or auto"); return true; }
        }
        const Transport eff = resolveTransport();
        print(LINE_PLAIN, "transport %s (now: %s)",
              _transport == TRANSPORT_RF ? "rf" : _transport == TRANSPORT_MQTT ? "mqtt" : "auto",
              eff == TRANSPORT_RF ? "rf" : "mqtt");
        return true;
    }
    if (!strcmp(c, "clear")) {
        _lineCount = 0; _lineHead = 0; _revision++;
        return true;
    }
    if (!strcmp(c, "verify")) {
        if (_pending.active) { print(LINE_ERR, "busy"); return true; }
        uint8_t msg[kMaxPayload];
        const size_t n = encodeGetRequest(msg, sizeof(msg), GET_DEVICE_METADATA_REQUEST, 1);
        print(LINE_INFO, "probing authorization");
        sendEncoded(msg, n, GET_DEVICE_METADATA_REQUEST, false, true, nowMs);
        return true;
    }
    if (!strcmp(c, "exit")) { print(LINE_INFO, "closing"); _open = false; return true; }
    return false;
}

bool Session::cmdRead(int argc, char **argv, uint32_t nowMs) {
    const char *c = argv[0];
    uint8_t msg[kMaxPayload];

    if (!strcmp(c, "info")) {
        const size_t n = encodeGetRequest(msg, sizeof(msg), GET_DEVICE_METADATA_REQUEST, 1);
        sendEncoded(msg, n, GET_DEVICE_METADATA_REQUEST, false, true, nowMs);
        return true;
    }
    if (!strcmp(c, "owner")) {
        const size_t n = encodeGetRequest(msg, sizeof(msg), GET_OWNER_REQUEST, 1);
        sendEncoded(msg, n, GET_OWNER_REQUEST, false, true, nowMs);
        return true;
    }
    if (!strcmp(c, "config")) {
        if (argc < 2) { print(LINE_ERR, "config <block>"); return true; }
        const uint32_t b = configBlockFromName(argv[1]);
        if (!b) { print(LINE_ERR, "unknown block: %s", argv[1]); return true; }
        const size_t n = encodeGetRequest(msg, sizeof(msg), GET_CONFIG_REQUEST, b);
        sendEncoded(msg, n, GET_CONFIG_REQUEST, false, true, nowMs);
        return true;
    }
    if (!strcmp(c, "module")) {
        if (argc < 2) { print(LINE_ERR, "module <block>"); return true; }
        // ModuleConfig block numbers are the request enum directly; the parser
        // keeps them numeric rather than inventing a second name table that
        // could disagree with upstream.
        const long b = strtol(argv[1], nullptr, 10);
        if (b <= 0 || b > 20) { print(LINE_ERR, "module <1-20>"); return true; }
        const size_t n = encodeGetRequest(msg, sizeof(msg), GET_MODULE_CONFIG_REQUEST, (uint64_t)b);
        sendEncoded(msg, n, GET_MODULE_CONFIG_REQUEST, false, true, nowMs);
        return true;
    }
    if (!strcmp(c, "channel")) {
        if (argc < 2) { print(LINE_ERR, "channel <0-7>"); return true; }
        const long idx = strtol(argv[1], nullptr, 10);
        if (idx < 0 || idx > 7) { print(LINE_ERR, "channel index is 0-7"); return true; }
        const size_t n = encodeGetRequest(msg, sizeof(msg), GET_CHANNEL_REQUEST, (uint64_t)(idx + 1));
        sendEncoded(msg, n, GET_CHANNEL_REQUEST, false, true, nowMs);
        return true;
    }
    return false;
}

bool Session::cmdWrite(int argc, char **argv, uint32_t nowMs) {
    const char *c = argv[0];
    uint8_t msg[kMaxPayload];

    if (!strcmp(c, "set")) {
        if (argc < 4) { print(LINE_ERR, "set <block> <field> <value>"); return true; }
        const uint32_t block = configBlockFromName(argv[1]);
        if (!block) { print(LINE_ERR, "unknown block: %s", argv[1]); return true; }
        const SettableField *f = findSettable(block, argv[2]);
        if (!f) { print(LINE_ERR, "%s has no settable field %s", argv[1], argv[2]); return true; }
        if (f->kind == V_STRING) {
            print(LINE_ERR, "string fields are not spliced yet (%s)", f->name);
            return true;
        }
        uint64_t value = 0;
        if (!parseValue(*f, argv[3], value)) {
            print(LINE_ERR, "bad value for %s: %s", f->name, argv[3]);
            return true;
        }

        // Read first, always, and say so. set_config replaces the whole
        // sub-message, so a write without the read before it blanks everything
        // else in the block.
        const size_t n = encodeGetRequest(msg, sizeof(msg), GET_CONFIG_REQUEST, block);
        print(LINE_INFO, "fetching %s config", argv[1]);
        if (!sendEncoded(msg, n, GET_CONFIG_REQUEST, false, true, nowMs)) return true;
        _pending.splicePending = true;
        _pending.spliceBlock = block;
        _pending.spliceField = f->field;
        _pending.spliceValue = value;
        snprintf(_pending.spliceLabel, sizeof(_pending.spliceLabel), "%s = %s",
                 f->name, argv[3]);
        return true;
    }

    if (!strcmp(c, "fav") || !strcmp(c, "ignore")) {
        if (argc < 3) { print(LINE_ERR, "%s add|rm <!id>", c); return true; }
        const bool add = !strcmp(argv[1], "add");
        if (!add && strcmp(argv[1], "rm")) { print(LINE_ERR, "%s add|rm <!id>", c); return true; }
        const char *idText = argv[2][0] == '!' ? argv[2] + 1 : argv[2];
        const uint32_t id = (uint32_t)strtoul(idText, nullptr, 16);
        if (!id) { print(LINE_ERR, "bad node id: %s", argv[2]); return true; }
        if (!passkeyFresh(nowMs)) { print(LINE_ERR, "no fresh session key - run `info` first"); return true; }
        const uint32_t field = !strcmp(c, "fav")
            ? (add ? SET_FAVORITE_NODE : REMOVE_FAVORITE_NODE)
            : (add ? SET_IGNORED_NODE  : REMOVE_IGNORED_NODE);
        const size_t n = encodeVarintCommand(msg, sizeof(msg), field, id, _passkey, _passkeyLen);
        sendEncoded(msg, n, field, false, false, nowMs);
        return true;
    }

    if (!strcmp(c, "time")) {
        if (!passkeyFresh(nowMs)) { print(LINE_ERR, "no fresh session key - run `info` first"); return true; }
        const uint32_t epoch = _hooks.epochNow ? _hooks.epochNow(_hooks.ctx) : 0;
        if (!epoch) { print(LINE_ERR, "our clock is not set"); return true; }
        const size_t n = encodeVarintCommand(msg, sizeof(msg), SET_TIME_ONLY, epoch,
                                             _passkey, _passkeyLen);
        sendEncoded(msg, n, SET_TIME_ONLY, false, false, nowMs);
        return true;
    }
    return false;
}

bool Session::cmdDanger(int argc, char **argv, uint32_t nowMs) {
    const char *c = argv[0];
    (void)nowMs;

    auto arm = [&](uint32_t field, uint64_t value, const char *what) {
        _confirm.armed = true;
        _confirm.field = field;
        _confirm.value = value;
        snprintf(_confirm.what, sizeof(_confirm.what), "%s", what);
        print(LINE_ERR, "%s - type `confirm` to proceed", what);
    };

    if (!strcmp(c, "reboot") || !strcmp(c, "shutdown")) {
        const bool reboot = !strcmp(c, "reboot");
        const long secs = (argc >= 2) ? strtol(argv[1], nullptr, 10) : 5;
        if (secs < 0 || secs > 3600) { print(LINE_ERR, "seconds must be 0-3600"); return true; }
        arm(reboot ? REBOOT_SECONDS : SHUTDOWN_SECONDS, (uint64_t)secs,
            reboot ? "reboot the remote" : "shut the remote down");
        return true;
    }
    if (!strcmp(c, "reset")) {
        if (argc < 2) { print(LINE_ERR, "reset nodedb|config|device"); return true; }
        if (!strcmp(argv[1], "nodedb")) { arm(NODEDB_RESET, 1, "erase the remote's node database"); return true; }
        if (!strcmp(argv[1], "config")) { arm(FACTORY_RESET_CONFIG, 1, "factory reset the remote's config"); return true; }
        if (!strcmp(argv[1], "device")) { arm(FACTORY_RESET_DEVICE, 1, "factory reset the remote device"); return true; }
        print(LINE_ERR, "reset nodedb|config|device");
        return true;
    }
    return false;
}

// ── Help accessors ───────────────────────────────────────────────────────────
int Session::helpLineCount() { return (int)(sizeof(kHelp) / sizeof(kHelp[0])); }

const char *Session::helpLine(int i) {
    if (i < 0 || i >= helpLineCount()) return "";
    return kHelp[i];
}

const char *Session::helpFor(const char *command) {
    if (!command) return nullptr;
    for (const auto &d : kHelpDetail) {
        if (!strcmp(d.cmd, command)) return d.text;
    }
    return nullptr;
}

}  // namespace AdminClient
