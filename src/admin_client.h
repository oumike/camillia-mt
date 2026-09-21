#pragma once
// The remote admin session: one command in flight, a session passkey per peer,
// a transcript, and the parser that turns a typed line into a packet.
//
// Shared by the device terminal and the web terminal so the two cannot drift --
// one command table, one help text, one set of rules about what needs a
// confirmation. The UI owns the screen; everything below the screen is here.
//
// Transport and time are injected rather than reached for, which keeps the
// state machine testable on a host: the caller supplies `now` and a send
// callback, and this module never touches the radio, the broker or millis().
#include <stdint.h>
#include <stddef.h>

#include "admin_proto.h"

namespace AdminClient {

// ── Transport ────────────────────────────────────────────────────────────────
enum Transport : uint8_t {
    TRANSPORT_AUTO = 0,
    TRANSPORT_RF   = 1,
    TRANSPORT_MQTT = 2,
};

// What the caller must be able to do for us. Returning false means "could not
// send", which the session reports rather than retrying -- a transport that is
// down is a fact to show the operator, not something to paper over.
struct Hooks {
    // Sends an encoded AdminMessage to `nodeId` over `via` (never AUTO), PKI
    // encrypted. Returns the packet id it went out as, or 0 on failure.
    uint32_t (*send)(uint32_t nodeId, Transport via,
                     const uint8_t *payload, size_t len, void *ctx);

    // Whether each route is usable right now. `rfRecent` is "heard on the radio
    // recently"; `mqttUp` is "the bridge is connected". Together they decide
    // what AUTO resolves to.
    bool (*rfRecent)(uint32_t nodeId, void *ctx);
    bool (*mqttUp)(void *ctx);

    // Epoch seconds, for the peer list's lastVerified. 0 when the clock is
    // unset, which is fine -- it is display only.
    uint32_t (*epochNow)(void *ctx);

    // The admin peer list, for the `peers` command. Returns false past the end.
    // A hook rather than a direct call so this module stays free of NVS and the
    // node table, exactly as it stays free of the radio.
    bool (*peerAt)(int index, uint32_t &nodeId, const char *&state, void *ctx);

    void *ctx;
};

// ── Transcript ───────────────────────────────────────────────────────────────
// A ring buffer of lines, heap-allocated on open and freed on close. Never
// persisted: a transcript can hold a remote node's entire configuration, and
// that has no business outliving the window it was shown in.
constexpr int  kLineLen  = 80;
constexpr int  kMaxLines = 40;

enum LineKind : uint8_t {
    LINE_ECHO   = 0,   // what the operator typed
    LINE_INFO   = 1,   // progress: "fetching lora config"
    LINE_OK     = 2,   // success
    LINE_ERR    = 3,   // failure, including NAKs
    LINE_PLAIN  = 4,   // command output
};

struct Line {
    char    text[kLineLen];
    uint8_t kind;
};

// ── Session ──────────────────────────────────────────────────────────────────
// One per open terminal. The device allows a single session at a time; the web
// terminal shares it, so opening the browser onto a node the device already has
// open continues the same conversation rather than starting a rival one.
class Session {
public:
    bool open(uint32_t nodeId, const Hooks &hooks);
    void close();
    bool isOpen() const { return _open; }
    uint32_t nodeId() const { return _nodeId; }

    // Feeds one typed line. Returns false only when the line was refused
    // outright (nothing open); everything else reports through the transcript,
    // which is where the operator is looking.
    bool submit(const char *line, uint32_t nowMs);

    // Called from the RX path for an ADMIN_APP packet whose request_id matches
    // something we sent. `via` is how it actually arrived, which may differ from
    // how it went out -- replies are accepted from either transport.
    void onAdminReply(uint32_t requestId, const uint8_t *payload, size_t len,
                      Transport via, uint32_t nowMs);

    // Called from the RX path for a routing ACK/NAK carrying our request_id.
    // `errorReason` is Routing.error_reason; 0 is an ACK.
    void onRouting(uint32_t requestId, uint32_t errorReason, Transport via,
                   uint32_t nowMs);

    // Drives timeouts. Cheap enough for every loop pass.
    void service(uint32_t nowMs);

    // Transcript access for the UIs.
    int  lineCount() const { return _lineCount; }
    const Line *line(int i) const;   // 0 = oldest retained
    // Monotonic counter of lines ever appended. The web terminal polls with the
    // last value it saw, so it fetches only what is new.
    uint32_t revision() const { return _revision; }

    Transport transport() const { return _transport; }
    bool      busy() const { return _pending.active; }

    // Puts a line in the transcript that the session did not ask for.
    //
    // The favourites sweep uses it: when a probe discovers that the peer this
    // session is talking to has stopped accepting us, the operator should find
    // out among their command receipts rather than on whatever they type next.
    // Bumps the revision like any other line, so both terminals repaint.
    void notify(LineKind kind, const char *text);

    // True while the previous line was a destructive command awaiting the word
    // `confirm`. The UI shows it in the prompt so the state is never invisible.
    bool awaitingConfirm() const { return _confirm.armed; }

    // The help text, as lines. Static, so the web terminal can render it without
    // a session open.
    static int helpLineCount();
    static const char *helpLine(int i);
    static const char *helpFor(const char *command);

private:
    struct Pending {
        bool     active;
        uint32_t requestId;
        uint32_t sentAtMs;
        uint32_t field;        // the AdminMessage field we sent
        Transport via;
        bool     destructive;  // never auto-retried, never on a refreshed key
        bool     isRead;       // reads mint the passkey; writes consume it
        // For a read-modify-write, what to do with the response.
        //
        // Two shapes share this: a Config block (get_config -> splice -> set_config)
        // and the owner record (get_owner -> splice -> set_owner). The owner has
        // no enclosing block, which is what spliceBlock == 0 means.
        bool     splicePending;
        uint32_t spliceBlock;   // Config block being edited; 0 = the message itself
        uint32_t spliceField;   // field inside it
        uint32_t spliceSetField;// the AdminMessage field the result is sent as
        bool     spliceIsText;
        uint64_t spliceValue;
        char     spliceText[40];
        char     spliceLabel[32];
    };

    struct ConfirmArm {
        bool     armed;
        uint32_t field;
        uint64_t value;
        char     what[32];
    };

    bool      _open = false;
    uint32_t  _nodeId = 0;
    Hooks     _hooks{};
    Transport _transport = TRANSPORT_AUTO;

    uint8_t   _passkey[AdminProto::kSessionPasskeyMax] = {};
    size_t    _passkeyLen = 0;
    uint32_t  _passkeyAtMs = 0;

    Pending    _pending{};
    ConfirmArm _confirm{};

    Line     *_lines = nullptr;
    int       _lineCount = 0;
    int       _lineHead = 0;      // ring start
    uint32_t  _revision = 0;

    void print(uint8_t kind, const char *fmt, ...);
    // Walks a response payload and prints it, a field per line. Named where a
    // name exists, numbered where one does not -- an unknown field is still on
    // the remote, so it still gets a line.
    void renderPayload(const AdminProto::Response &r);
    void renderMessage(const uint8_t *buf, size_t len,
                       const void *table, size_t tableCount, int depth);
    Transport resolveTransport() const;
    bool sendEncoded(const uint8_t *buf, size_t len, uint32_t field,
                     bool destructive, bool isRead, uint32_t nowMs);
    bool passkeyFresh(uint32_t nowMs) const;

    // Command handlers. Each returns false only for "not this command".
    bool cmdMeta(int argc, char **argv, uint32_t nowMs);
    bool cmdRead(int argc, char **argv, uint32_t nowMs);
    bool cmdWrite(int argc, char **argv, uint32_t nowMs);
    bool cmdDanger(int argc, char **argv, uint32_t nowMs);
};

// Name for a routing error, for the transcript. Mirrors the firmware's own
// Routing.Error values; the two that matter to the gate are 33 and 37.
const char *routingErrorLabel(uint32_t reason);
bool        errorMeansUnauthorized(uint32_t reason);

}  // namespace AdminClient
