#include "vnc_host.h"

#if HAS_VNC_HOST

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include "web_icon.h"   // the site icon, embedded in the viewer page below
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <mbedtls/sha1.h>
#include <mbedtls/version.h>
#include <string.h>

namespace {

static constexpr uint16_t kVncPort = 8765;
static constexpr size_t kBandBytes = 4096;
static constexpr size_t kPacketBytes = kBandBytes + 16;
static constexpr size_t kHandshakeBytes = 1536;
static constexpr uint32_t kHandshakeTimeoutMs = 5000;
static constexpr uint32_t kFrameIntervalMs = 50;
static constexpr uint32_t kWriteTimeoutMs = 350;
static constexpr int kKeyRingSize = 256;

static WiFiServer s_server(kVncPort);
static WiFiClient s_client;
static TaskHandle_t s_task = nullptr;

static volatile bool s_enabled = false;
static volatile bool s_running = false;
static volatile bool s_clientConnected = false;
static volatile bool s_fullRepaintRequested = false;
static uint16_t s_screenW = 0;
static uint16_t s_screenH = 0;
static char s_ip[16] = {};

static uint16_t *s_framebuffer = nullptr;
static uint8_t *s_bandBuffer = nullptr;
static uint8_t *s_rleBuffer = nullptr;
static uint8_t *s_packetBuffer = nullptr;
static SemaphoreHandle_t s_frameMutex = nullptr;

static portMUX_TYPE s_dirtyMux = portMUX_INITIALIZER_UNLOCKED;
static bool s_dirty = false;
static int16_t s_dirtyX1 = 0;
static int16_t s_dirtyY1 = 0;
static int16_t s_dirtyX2 = 0;
static int16_t s_dirtyY2 = 0;

static portMUX_TYPE s_pointerMux = portMUX_INITIALIZER_UNLOCKED;
static int16_t s_pointerX = 0;
static int16_t s_pointerY = 0;
static bool s_pointerPressed = false;
static bool s_pointerKnown = false;

static portMUX_TYPE s_keyMux = portMUX_INITIALIZER_UNLOCKED;
static uint16_t s_keys[kKeyRingSize] = {};
static uint16_t s_keyHead = 0;
static uint16_t s_keyTail = 0;
// Stamped when the browser sends a key, not when the UI consumes one: what a
// redraw pacer needs to know is when the operator last typed, and consumption
// is the thing being delayed.
static volatile uint32_t s_lastKeyMs = 0;

// Access control. A PIN is drawn each time the host is enabled and shown on the
// device; the web config trades it for a session token, which the viewer must
// present on the WebSocket upgrade. Redeeming runs on the main loop (a web
// handler) while validation runs on the VNC task, so all of it sits behind
// s_authMux.
static constexpr int kPinDigits = 6;
static constexpr int kTokenSlots = 4;
static constexpr size_t kTokenBytes = 16;   // 128 bits
static constexpr size_t kTokenHexLen = kTokenBytes * 2;
static constexpr uint8_t kMaxPinFailures = 5;
static constexpr uint32_t kPinRetryDelayMs = 1000;
static portMUX_TYPE s_authMux = portMUX_INITIALIZER_UNLOCKED;
static char s_pin[kPinDigits + 1] = {};
static char s_tokens[kTokenSlots][kTokenHexLen + 1] = {};
static uint8_t s_tokenNext = 0;
static uint8_t s_pinFailures = 0;
static uint32_t s_lastPinFailMs = 0;
static bool s_pinFailedRecently = false;
static uint32_t s_session = 0;
static uint32_t s_pinSerial = 0;

static char s_handshake[kHandshakeBytes] = {};
static size_t s_handshakeLen = 0;
static bool s_wsReady = false;
static bool s_metaSent = false;
static uint32_t s_clientAcceptedMs = 0;

enum WsRxState : uint8_t {
    WS_RX_HEADER_0,
    WS_RX_HEADER_1,
    WS_RX_MASK,
    WS_RX_PAYLOAD,
};

static WsRxState s_wsRxState = WS_RX_HEADER_0;
static uint8_t s_wsOpcode = 0;
static bool s_wsFinal = false;
static bool s_wsMasked = false;
static uint64_t s_wsPayloadLen = 0;
static uint64_t s_wsPayloadRead = 0;
static uint8_t s_wsMask[4] = {};
static uint8_t s_wsMaskRead = 0;
static uint8_t s_wsPayload[32] = {};
static size_t s_wsStored = 0;
static uint32_t s_lastFrameMs = 0;

static const char kViewerHttpHeader[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Cache-Control: no-store\r\n"
    "Connection: close\r\n"
    "\r\n";

static const char kViewerPage[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Camillia VNC</title>
<link rel="icon" href=")HTML" CAMELLIA_FAVICON_DATA_URI R"HTML(">
<style>
:root{color-scheme:light;--paper:#f3f1ea;--ink:#171816;--muted:#686b65;--line:#c9c8bf;--live:#087f72;--bad:#b33b31}
*{box-sizing:border-box}html,body{margin:0;width:100%;height:100%;overflow:hidden;background:var(--paper);color:var(--ink);font-family:"Avenir Next","Trebuchet MS",sans-serif;letter-spacing:0}
body{display:grid;place-items:center;padding:6px;touch-action:none;user-select:none;-webkit-user-select:none}
main{width:min(760px,100%);height:100%;min-width:0;min-height:0;display:grid;grid-template-rows:30px minmax(0,1fr);gap:5px;justify-items:center}
header{width:100%;display:flex;align-items:center;justify-content:space-between;gap:10px;border-bottom:1px solid var(--line);padding:0 1px 4px}
h1{font-size:15px;line-height:1;margin:0;font-weight:700;white-space:nowrap}.tools{display:flex;align-items:center;justify-content:flex-end;gap:8px;min-width:0}
#status{font:600 11px "Avenir Next","Trebuchet MS",sans-serif;color:var(--muted);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}#status.live{color:var(--live)}#status.bad{color:var(--bad)}
.viewer{width:100%;height:100%;min-width:0;min-height:0;display:flex;align-items:center;justify-content:center;overflow:hidden}
#screen{display:block;flex:none;background:#000;width:auto;height:auto;box-shadow:0 8px 22px #0003;image-rendering:auto;cursor:crosshair}
#screen:focus{outline:2px solid var(--live);outline-offset:2px}
button{height:25px;border:1px solid var(--ink);background:transparent;color:var(--ink);font:700 11px "Avenir Next","Trebuchet MS",sans-serif;padding:2px 8px;border-radius:3px;cursor:pointer;white-space:nowrap}button:active{background:var(--ink);color:var(--paper)}
#keys{position:fixed;left:0;top:0;width:1px;height:1px;opacity:0;border:0;padding:0;font-size:16px}
</style>
</head>
<body>
<main>
<header><h1>Camillia VNC</h1><div class="tools"><span id="status">Connecting</span><button id="keyboard" type="button">Keyboard</button></div></header>
<div class="viewer" id="viewer"><canvas id="screen" width="320" height="240" tabindex="0"></canvas></div>
</main>
<textarea id="keys" autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false"></textarea>
<script>
const canvas=document.getElementById('screen'),ctx=canvas.getContext('2d'),viewer=document.getElementById('viewer');
const statusEl=document.getElementById('status'),keys=document.getElementById('keys');
let width=320,height=240,socket=null,pointerDown=false,lastMove=0,retryTimer=0,token='',failures=0,locked=false;
const setStatus=(text,kind='')=>{statusEl.textContent=text;statusEl.className=kind;statusEl.title=text};
const readToken=()=>{const m=/(?:^#|&)t=([0-9a-f]{32,64})(?:&|$)/.exec(location.hash);return m?m[1]:''};
function lock(){
  locked=true;clearTimeout(retryTimer);
  setStatus('Locked \u2014 open Remote in the web config and enter the PIN shown on the device','bad');
  try{if(parent!==window)parent.postMessage({vnc:'locked'},'*')}catch(_){ }
}
function fitScreen(){
    const box=viewer.getBoundingClientRect();if(!box.width||!box.height||!width||!height)return;
    const scale=Math.min(1,Math.max(1,box.width-4)/width,Math.max(1,box.height-4)/height);
    canvas.style.width=`${Math.max(1,Math.floor(width*scale))}px`;
    canvas.style.height=`${Math.max(1,Math.floor(height*scale))}px`;
}
function connect(){
  clearTimeout(retryTimer);
  if(!token){lock();return}
  locked=false;let opened=false;
  const ws=new WebSocket(`ws://${location.host}/mirror?t=${token}`);ws.binaryType='arraybuffer';socket=ws;
  ws.onopen=()=>{opened=true;failures=0;setStatus('Connected','live')};
  ws.onerror=()=>{if(!locked)setStatus('Connection error','bad')};
  ws.onclose=()=>{
    if(socket!==ws||locked)return;
    // A refused upgrade (bad token) and a stopped host look the same from
    // here; two in a row without ever opening means stop and ask for a PIN.
    if(!opened&&++failures>=2){lock();return}
    setStatus('Reconnecting','bad');retryTimer=setTimeout(connect,1500);
  };
  ws.onmessage=event=>{
    const bytes=new Uint8Array(event.data);if(!bytes.length)return;
    if(bytes[0]===2){width=bytes[1]|(bytes[2]<<8);height=bytes[3]|(bytes[4]<<8);canvas.width=width;canvas.height=height;fitScreen();return}
    if(bytes[0]!==1||bytes.length<10)return;
    const flags=bytes[1],x=bytes[2]|(bytes[3]<<8),y=bytes[4]|(bytes[5]<<8),w=bytes[6]|(bytes[7]<<8),h=bytes[8]|(bytes[9]<<8);
    const image=ctx.createImageData(w,h),rgba=image.data;let src=10,dst=0;
    const pixel=value=>{rgba[dst++]=(value>>8)&248;rgba[dst++]=(value>>3)&252;rgba[dst++]=(value<<3)&248;rgba[dst++]=255};
    if(flags&1){while(dst<rgba.length&&src+2<bytes.length){let count=bytes[src++],value=bytes[src++]|(bytes[src++]<<8);while(count--&&dst<rgba.length)pixel(value)}}
    else{while(dst<rgba.length&&src+1<bytes.length)pixel(bytes[src++]|(bytes[src++]<<8))}
    ctx.putImageData(image,x,y);
  };
}
function point(event){const rect=canvas.getBoundingClientRect();return [Math.max(0,Math.min(width-1,Math.floor((event.clientX-rect.left)*width/rect.width))),Math.max(0,Math.min(height-1,Math.floor((event.clientY-rect.top)*height/rect.height)))]}
function sendPointer(event,pressed){if(!socket||socket.readyState!==1)return;const [x,y]=point(event);socket.send(new Uint8Array([1,x&255,x>>8,y&255,y>>8,pressed?1:0]))}
canvas.addEventListener('pointerdown',event=>{pointerDown=true;try{canvas.setPointerCapture(event.pointerId)}catch(_){ }try{canvas.focus({preventScroll:true})}catch(_){canvas.focus()}sendPointer(event,true);event.preventDefault()});
canvas.addEventListener('pointermove',event=>{if(!pointerDown)return;const now=Date.now();if(now-lastMove<33)return;lastMove=now;sendPointer(event,true)});
function release(event){if(!pointerDown)return;pointerDown=false;sendPointer(event,false)}
canvas.addEventListener('pointerup',release);canvas.addEventListener('pointercancel',release);
function sendKey(codepoint){if(socket&&socket.readyState===1&&codepoint>=0&&codepoint<=65535)socket.send(new Uint8Array([2,codepoint&255,codepoint>>8]))}
document.getElementById('keyboard').addEventListener('click',()=>{keys.value=' ';keys.focus();keys.setSelectionRange(1,1)});
keys.addEventListener('beforeinput',event=>{if(event.inputType==='insertText'&&event.data){for(const char of event.data)sendKey(char.codePointAt(0));event.preventDefault()}else if(event.inputType==='deleteContentBackward'){sendKey(8);event.preventDefault()}else if(event.inputType==='insertLineBreak'||event.inputType==='insertParagraph'){sendKey(13);event.preventDefault()}keys.value=' ';try{keys.setSelectionRange(1,1)}catch(_){ }});
window.addEventListener('keydown',event=>{if(document.activeElement===keys||event.ctrlKey||event.metaKey||event.altKey)return;let code=0;if(event.key==='Backspace')code=8;else if(event.key==='Enter')code=13;else if(event.key==='Escape')code=27;else if(event.key.length===1)code=event.key.codePointAt(0);if(code){sendKey(code);event.preventDefault()}});
if('ResizeObserver' in window)new ResizeObserver(fitScreen).observe(viewer);
window.addEventListener('resize',fitScreen);requestAnimationFrame(fitScreen);
window.addEventListener('hashchange',()=>{token=readToken();failures=0;if(socket){const old=socket;socket=null;try{old.close()}catch(_){ }}connect()});
token=readToken();
connect();
</script>
</body>
</html>
)HTML";

static bool socketWritable(WiFiClient &client) {
    const int socket = client.fd();
    if (socket < 0) return false;
    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(socket, &writeSet);
    timeval timeout = {};
    return select(socket + 1, nullptr, &writeSet, nullptr, &timeout) > 0
        && FD_ISSET(socket, &writeSet);
}

static bool writeAll(WiFiClient &client, const uint8_t *data, size_t length,
                     uint32_t timeoutMs) {
    size_t sent = 0;
    const uint32_t started = millis();
    while (sent < length) {
        if (!client.connected()) return false;
        if (socketWritable(client)) {
            const size_t written = client.write(data + sent, length - sent);
            if (written > 0) {
                sent += written;
                continue;
            }
        }
        if ((uint32_t)(millis() - started) >= timeoutMs) return false;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return true;
}

static void base64Sha1(const uint8_t digest[20], char output[29]) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out = 0;
    for (size_t i = 0; i < 20; i += 3) {
        const size_t remaining = 20 - i;
        uint32_t value = (uint32_t)digest[i] << 16;
        if (remaining > 1) value |= (uint32_t)digest[i + 1] << 8;
        if (remaining > 2) value |= digest[i + 2];
        output[out++] = alphabet[(value >> 18) & 0x3F];
        output[out++] = alphabet[(value >> 12) & 0x3F];
        output[out++] = remaining > 1 ? alphabet[(value >> 6) & 0x3F] : '=';
        output[out++] = remaining > 2 ? alphabet[value & 0x3F] : '=';
    }
    output[out] = '\0';
}

static void resetWsParser() {
    s_wsRxState = WS_RX_HEADER_0;
    s_wsOpcode = 0;
    s_wsFinal = false;
    s_wsMasked = false;
    s_wsPayloadLen = 0;
    s_wsPayloadRead = 0;
    s_wsMaskRead = 0;
    s_wsStored = 0;
}

static void closeClient() {
    if (s_client) s_client.stop();
    s_client = WiFiClient();
    s_handshakeLen = 0;
    s_clientAcceptedMs = 0;
    s_wsReady = false;
    s_metaSent = false;
    s_clientConnected = false;
    resetWsParser();
    portENTER_CRITICAL(&s_pointerMux);
    s_pointerKnown = false;
    s_pointerPressed = false;
    portEXIT_CRITICAL(&s_pointerMux);
    portENTER_CRITICAL(&s_keyMux);
    s_keyTail = s_keyHead;
    portEXIT_CRITICAL(&s_keyMux);
}

static void stopNetwork() {
    closeClient();
    if (s_running) s_server.stop();
    s_running = false;
    s_ip[0] = '\0';
}

static bool sendWsFrame(uint8_t opcode, const uint8_t *data, size_t length) {
    if (!s_wsReady || !s_client.connected() || length > 0xFFFF) return false;
    uint8_t header[4];
    size_t headerLength = 2;
    header[0] = (uint8_t)(0x80 | (opcode & 0x0F));
    if (length < 126) {
        header[1] = (uint8_t)length;
    } else {
        header[1] = 126;
        header[2] = (uint8_t)(length >> 8);
        header[3] = (uint8_t)length;
        headerLength = 4;
    }
    return writeAll(s_client, header, headerLength, kWriteTimeoutMs)
        && (length == 0 || writeAll(s_client, data, length, kWriteTimeoutMs));
}

static bool sendWsBinary(const uint8_t *data, size_t length) {
    return length > 0 && sendWsFrame(0x02, data, length);
}

static void pushKey(uint16_t codepoint) {
    portENTER_CRITICAL(&s_keyMux);
    const uint16_t next = (uint16_t)((s_keyHead + 1) % kKeyRingSize);
    if (next != s_keyTail) {
        s_keys[s_keyHead] = codepoint;
        s_keyHead = next;
    }
    portEXIT_CRITICAL(&s_keyMux);
    // Outside the ring update, and set even when a full ring drops the key: the
    // operator typed either way, which is what this timestamp answers.
    s_lastKeyMs = millis();
}

static void dispatchWsPayload() {
    if (s_wsOpcode == 0x08) {
        (void)sendWsFrame(0x08, s_wsPayload, s_wsStored);
        closeClient();
        return;
    }
    if (s_wsOpcode == 0x09) {
        if (!sendWsFrame(0x0A, s_wsPayload, s_wsStored)) closeClient();
        return;
    }
    if (s_wsOpcode == 0x0A) return;
    if (s_wsOpcode != 0x02 || s_wsStored == 0) return;
    if (s_wsPayload[0] == 0x01 && s_wsStored >= 6) {
        const int16_t x = (int16_t)(s_wsPayload[1] | (s_wsPayload[2] << 8));
        const int16_t y = (int16_t)(s_wsPayload[3] | (s_wsPayload[4] << 8));
        portENTER_CRITICAL(&s_pointerMux);
        s_pointerX = x;
        s_pointerY = y;
        s_pointerPressed = s_wsPayload[5] != 0;
        s_pointerKnown = true;
        portEXIT_CRITICAL(&s_pointerMux);
    } else if (s_wsPayload[0] == 0x02 && s_wsStored >= 3) {
        pushKey((uint16_t)(s_wsPayload[1] | (s_wsPayload[2] << 8)));
    }
}

static void serviceWsInput() {
    int budget = 1024;
    while (budget-- > 0 && s_client.connected() && s_client.available() > 0) {
        const uint8_t byte = (uint8_t)s_client.read();
        switch (s_wsRxState) {
            case WS_RX_HEADER_0:
                s_wsFinal = (byte & 0x80) != 0;
                s_wsOpcode = byte & 0x0F;
                if (!s_wsFinal || (byte & 0x70) != 0
                    || (s_wsOpcode != 0x02 && s_wsOpcode != 0x08
                        && s_wsOpcode != 0x09 && s_wsOpcode != 0x0A)) {
                    closeClient();
                    return;
                }
                s_wsRxState = WS_RX_HEADER_1;
                break;
            case WS_RX_HEADER_1: {
                s_wsMasked = (byte & 0x80) != 0;
                const uint8_t shortLength = byte & 0x7F;
                s_wsPayloadRead = 0;
                s_wsStored = 0;
                if (!s_wsMasked || shortLength >= 126
                    || shortLength > sizeof(s_wsPayload)) {
                    closeClient();
                    return;
                }
                s_wsPayloadLen = shortLength;
                s_wsMaskRead = 0;
                s_wsRxState = WS_RX_MASK;
                break;
            }
            case WS_RX_MASK:
                s_wsMask[s_wsMaskRead++] = byte;
                if (s_wsMaskRead == 4) {
                    if (s_wsPayloadLen == 0) {
                        dispatchWsPayload();
                        if (!s_client.connected()) return;
                        resetWsParser();
                    } else {
                        s_wsRxState = WS_RX_PAYLOAD;
                    }
                }
                break;
            case WS_RX_PAYLOAD: {
                const uint8_t decoded = byte ^ s_wsMask[s_wsPayloadRead % 4];
                if (s_wsStored < sizeof(s_wsPayload)) s_wsPayload[s_wsStored++] = decoded;
                ++s_wsPayloadRead;
                if (s_wsPayloadRead == s_wsPayloadLen) {
                    dispatchWsPayload();
                    if (!s_client.connected()) return;
                    resetWsParser();
                }
                break;
            }
        }
    }
}

static bool constantTimeEqual(const char *a, const char *b, size_t length) {
    uint8_t diff = 0;
    for (size_t i = 0; i < length; ++i) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

static void randomHex(char *out, size_t bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < bytes; i += 4) {
        const uint32_t value = esp_random();
        for (size_t j = 0; j < 4 && i + j < bytes; ++j) {
            const uint8_t byte = (uint8_t)(value >> (8 * j));
            out[2 * (i + j)] = kHex[byte >> 4];
            out[2 * (i + j) + 1] = kHex[byte & 0x0F];
        }
    }
    out[2 * bytes] = '\0';
}

// Call with s_authMux held. Uniform over 000000-999999: values past the last
// whole multiple of a million are drawn again rather than folded in.
static void generatePinLocked() {
    static constexpr uint32_t kLimit = 4294000000u;   // 4294 * 10^6
    uint32_t value = esp_random();
    while (value >= kLimit) value = esp_random();
    value %= 1000000u;
    for (int i = kPinDigits - 1; i >= 0; --i) {
        s_pin[i] = (char)('0' + value % 10u);
        value /= 10u;
    }
    s_pin[kPinDigits] = '\0';
    s_pinFailures = 0;
    s_pinFailedRecently = false;
    ++s_pinSerial;
}

static void clearAuthLocked() {
    memset(s_pin, 0, sizeof(s_pin));
    memset(s_tokens, 0, sizeof(s_tokens));
    s_tokenNext = 0;
    s_pinFailures = 0;
    s_pinFailedRecently = false;
}

static bool tokenValid(const char *token, size_t length) {
    if (length != kTokenHexLen) return false;
    bool match = false;
    portENTER_CRITICAL(&s_authMux);
    for (int i = 0; i < kTokenSlots; ++i) {
        const bool slotMatch = s_tokens[i][0] != '\0'
            && constantTimeEqual(s_tokens[i], token, kTokenHexLen);
        match = match || slotMatch;
    }
    portEXIT_CRITICAL(&s_authMux);
    return match;
}

// The value of a request header, matched case-insensitively at a line start.
// Returns its length, or -1 when absent; *value points into s_handshake.
static int findHeader(const char *name, const char **value) {
    const size_t nameLength = strlen(name);
    const char *line = strstr(s_handshake, "\r\n");
    while (line) {
        line += 2;
        if (*line == '\r' || *line == '\0') break;
        if (strncasecmp(line, name, nameLength) == 0 && line[nameLength] == ':') {
            const char *start = line + nameLength + 1;
            while (*start == ' ' || *start == '\t') ++start;
            const char *end = start;
            while (*end && *end != '\r' && *end != '\n') ++end;
            while (end > start && (end[-1] == ' ' || end[-1] == '\t')) --end;
            *value = start;
            return (int)(end - start);
        }
        line = strstr(line, "\r\n");
    }
    return -1;
}

// The host part of "host[:port]", ignoring the port: the web config on :80
// embeds the viewer from :8765, and the two must still count as one device.
static size_t hostPartLength(const char *authority, size_t length) {
    size_t n = 0;
    while (n < length && authority[n] != ':' && authority[n] != '/') ++n;
    return n;
}

static bool originMatchesHost() {
    const char *origin = nullptr;
    const int originLength = findHeader("Origin", &origin);
    if (originLength < 0) return true;   // not a browser page; the token decides
    const char *host = nullptr;
    const int hostLength = findHeader("Host", &host);
    if (hostLength <= 0) return false;
    const char *scheme = strstr(origin, "://");
    if (!scheme || scheme >= origin + originLength) return false;
    const char *originHost = scheme + 3;
    const size_t originRest = (size_t)(origin + originLength - originHost);
    const size_t a = hostPartLength(originHost, originRest);
    const size_t b = hostPartLength(host, (size_t)hostLength);
    return a > 0 && a == b && strncasecmp(originHost, host, a) == 0;
}

// "GET /mirror?t=<token> HTTP/1.1" with a currently valid token.
static bool requestAuthorized() {
    static constexpr char kPrefix[] = "GET /mirror?";
    if (strncmp(s_handshake, kPrefix, sizeof(kPrefix) - 1) != 0) return false;
    const char *query = s_handshake + sizeof(kPrefix) - 1;
    const char *queryEnd = query;
    while (*queryEnd && *queryEnd != ' ' && *queryEnd != '\r') ++queryEnd;
    const char *param = query;
    while (param < queryEnd) {
        const char *paramEnd = param;
        while (paramEnd < queryEnd && *paramEnd != '&') ++paramEnd;
        if (paramEnd - param > 2 && param[0] == 't' && param[1] == '=') {
            return tokenValid(param + 2, (size_t)(paramEnd - param - 2));
        }
        param = paramEnd < queryEnd ? paramEnd + 1 : queryEnd;
    }
    return false;
}

static void refuseHandshake(const char *why) {
    static const char kForbidden[] =
        "HTTP/1.1 403 Forbidden\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 10\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Forbidden\n";
    Serial.printf("[vnc] refused browser: %s\n", why);
    (void)writeAll(s_client, (const uint8_t *)kForbidden, sizeof(kForbidden) - 1, 500);
}

static bool completeWebSocketHandshake() {
    static constexpr char kKeyHeader[] = "Sec-WebSocket-Key:";
    char *key = strstr(s_handshake, kKeyHeader);
    if (!key || strncmp(s_handshake, "GET /mirror", 11) != 0) return false;
    if (!originMatchesHost()) {
        refuseHandshake("cross-origin");
        return false;
    }
    if (!requestAuthorized()) {
        refuseHandshake("missing or invalid token");
        return false;
    }
    key += sizeof(kKeyHeader) - 1;
    while (*key == ' ' || *key == '\t') ++key;
    char *end = key;
    while (*end && *end != '\r' && *end != '\n') ++end;
    const size_t keyLength = (size_t)(end - key);
    if (keyLength == 0 || keyLength > 80) return false;

    static constexpr char kMagic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char source[128];
    memcpy(source, key, keyLength);
    memcpy(source + keyLength, kMagic, sizeof(kMagic) - 1);
    uint8_t digest[20];
#if MBEDTLS_VERSION_MAJOR >= 3
    if (mbedtls_sha1((const unsigned char *)source,
                     keyLength + sizeof(kMagic) - 1, digest) != 0) {
#else
    if (mbedtls_sha1_ret((const unsigned char *)source,
                         keyLength + sizeof(kMagic) - 1, digest) != 0) {
#endif
        return false;
    }
    char accept[29];
    base64Sha1(digest, accept);
    char response[192];
    const int responseLength = snprintf(
        response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept);
    if (responseLength <= 0 || (size_t)responseLength >= sizeof(response)) return false;
    if (!writeAll(s_client, (const uint8_t *)response, (size_t)responseLength, 1000)) return false;
    s_client.setNoDelay(true);
    s_wsReady = true;
    s_metaSent = false;
    s_clientConnected = true;
    resetWsParser();
    Serial.println("[vnc] browser connected");
    return true;
}

static void serviceHandshake() {
    while (s_client.connected() && s_client.available() > 0
           && s_handshakeLen < sizeof(s_handshake) - 1) {
        s_handshake[s_handshakeLen++] = (char)s_client.read();
        s_handshake[s_handshakeLen] = '\0';
        if (s_handshakeLen >= 4
            && memcmp(s_handshake + s_handshakeLen - 4, "\r\n\r\n", 4) == 0) {
            const bool upgrade = strstr(s_handshake, "Upgrade: websocket") != nullptr;
            if (upgrade) {
                if (!completeWebSocketHandshake()) closeClient();
            } else {
                if (writeAll(s_client, (const uint8_t *)kViewerHttpHeader,
                             strlen(kViewerHttpHeader), 1000)) {
                    (void)writeAll(s_client, (const uint8_t *)kViewerPage,
                                   strlen(kViewerPage), 1500);
                }
                closeClient();
            }
            return;
        }
    }
    if (s_handshakeLen >= sizeof(s_handshake) - 1) closeClient();
}

static size_t encodeRle(const uint16_t *pixels, size_t count,
                        uint8_t *output, size_t capacity) {
    size_t source = 0;
    size_t encoded = 0;
    while (source < count) {
        const uint16_t value = pixels[source];
        size_t run = 1;
        while (source + run < count && pixels[source + run] == value && run < 255) ++run;
        if (encoded + 3 > capacity) return 0;
        output[encoded++] = (uint8_t)run;
        output[encoded++] = (uint8_t)value;
        output[encoded++] = (uint8_t)(value >> 8);
        source += run;
    }
    return encoded;
}

static bool takeDirtyRegion(int16_t &x, int16_t &y, int16_t &width, int16_t &height) {
    portENTER_CRITICAL(&s_dirtyMux);
    if (!s_dirty) {
        portEXIT_CRITICAL(&s_dirtyMux);
        return false;
    }
    x = s_dirtyX1;
    y = s_dirtyY1;
    width = (int16_t)(s_dirtyX2 - s_dirtyX1 + 1);
    height = (int16_t)(s_dirtyY2 - s_dirtyY1 + 1);
    s_dirty = false;
    portEXIT_CRITICAL(&s_dirtyMux);
    return true;
}

static bool sendRegion(int16_t x, int16_t y, int16_t width, int16_t height) {
    if (!s_framebuffer || !s_bandBuffer || !s_rleBuffer || !s_packetBuffer) return false;
    // Bands are whole rows, so the panel width sets the floor: one row must fit
    // in kBandBytes or the clamp below hands memcpy a band wider than the
    // buffer. That caps the panel at 2048 px wide (T-Deck 320, Pager 480).
    int rowsPerBand = (int)(kBandBytes / ((size_t)width * sizeof(uint16_t)));
    if (rowsPerBand < 1) rowsPerBand = 1;
    uint16_t *band = (uint16_t *)s_bandBuffer;
    for (int rowOffset = 0; rowOffset < height; rowOffset += rowsPerBand) {
        const int bandHeight = min(rowsPerBand, (int)height - rowOffset);
        if (!s_frameMutex || xSemaphoreTake(s_frameMutex, portMAX_DELAY) != pdTRUE) {
            return false;
        }
        for (int row = 0; row < bandHeight; ++row) {
            memcpy(band + (size_t)row * width,
                   s_framebuffer + (size_t)(y + rowOffset + row) * s_screenW + x,
                   (size_t)width * sizeof(uint16_t));
        }
        xSemaphoreGive(s_frameMutex);
        const size_t rawLength = (size_t)width * bandHeight * sizeof(uint16_t);
        const size_t rleLength = encodeRle(band, (size_t)width * bandHeight,
                                           s_rleBuffer, rawLength);
        const bool useRle = rleLength > 0 && rleLength < rawLength;
        const uint8_t *body = useRle ? s_rleBuffer : s_bandBuffer;
        const size_t bodyLength = useRle ? rleLength : rawLength;
        const int bandY = y + rowOffset;
        uint8_t *packet = s_packetBuffer;
        packet[0] = 0x01;
        packet[1] = useRle ? 0x01 : 0x00;
        packet[2] = (uint8_t)x;
        packet[3] = (uint8_t)(x >> 8);
        packet[4] = (uint8_t)bandY;
        packet[5] = (uint8_t)(bandY >> 8);
        packet[6] = (uint8_t)width;
        packet[7] = (uint8_t)(width >> 8);
        packet[8] = (uint8_t)bandHeight;
        packet[9] = (uint8_t)(bandHeight >> 8);
        memcpy(packet + 10, body, bodyLength);
        if (!sendWsBinary(packet, bodyLength + 10)) return false;
    }
    return true;
}

static bool stationConnected();

static void serviceFrames() {
    if (!s_wsReady) return;
    if (!s_metaSent) {
        const uint8_t meta[5] = {
            0x02,
            (uint8_t)s_screenW, (uint8_t)(s_screenW >> 8),
            (uint8_t)s_screenH, (uint8_t)(s_screenH >> 8),
        };
        if (!sendWsBinary(meta, sizeof(meta))) {
            closeClient();
            return;
        }
        s_metaSent = true;
        __atomic_store_n(&s_fullRepaintRequested, true, __ATOMIC_RELEASE);
        return;
    }
    const uint32_t now = millis();
    if ((uint32_t)(now - s_lastFrameMs) < kFrameIntervalMs) return;
    int16_t x = 0, y = 0, width = 0, height = 0;
    if (!takeDirtyRegion(x, y, width, height)) return;
    s_lastFrameMs = now;
    if (!sendRegion(x, y, width, height)) {
        Serial.println("[vnc] browser write failed");
        closeClient();
        __atomic_store_n(&s_fullRepaintRequested, true, __ATOMIC_RELEASE);
    }
}

static void vncTask(void *) {
    for (;;) {
        if (!s_enabled || !stationConnected()) {
            if (s_running || s_client) stopNetwork();
            vTaskDelay(pdMS_TO_TICKS(25));
            continue;
        }
        if (!s_running) {
            s_server.begin();
            s_running = true;
            WiFi.localIP().toString().toCharArray(s_ip, sizeof(s_ip));
            Serial.printf("[vnc] ready at http://%s:%u/\n", s_ip, (unsigned)kVncPort);
        }
        if (!s_client || !s_client.connected()) {
            closeClient();
            WiFiClient incoming = s_server.available();
            if (incoming) {
                s_client = incoming;
                s_clientAcceptedMs = millis();
                s_handshakeLen = 0;
                s_wsReady = false;
                s_metaSent = false;
            }
        } else if (!s_wsReady) {
            if ((uint32_t)(millis() - s_clientAcceptedMs) >= kHandshakeTimeoutMs) {
                closeClient();
            } else {
                serviceHandshake();
            }
        } else {
            serviceWsInput();
            serviceFrames();
        }
        vTaskDelay(pdMS_TO_TICKS(s_clientConnected ? 2 : 10));
    }
}

static bool ensureBuffers() {
    if (s_framebuffer && s_bandBuffer && s_rleBuffer && s_packetBuffer && s_frameMutex) {
        return true;
    }
    if (s_screenW == 0 || s_screenH == 0) return false;
    if (!s_frameMutex) s_frameMutex = xSemaphoreCreateMutex();
    const size_t frameBytes = (size_t)s_screenW * s_screenH * sizeof(uint16_t);
    s_framebuffer = (uint16_t *)heap_caps_malloc(
        frameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_bandBuffer = (uint8_t *)heap_caps_malloc(
        kBandBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_rleBuffer = (uint8_t *)heap_caps_malloc(
        kBandBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_packetBuffer = (uint8_t *)heap_caps_malloc(
        kPacketBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_framebuffer || !s_bandBuffer || !s_rleBuffer || !s_packetBuffer || !s_frameMutex) {
        free(s_framebuffer);
        free(s_bandBuffer);
        free(s_rleBuffer);
        free(s_packetBuffer);
        s_framebuffer = nullptr;
        s_bandBuffer = nullptr;
        s_rleBuffer = nullptr;
        s_packetBuffer = nullptr;
        if (s_frameMutex) {
            vSemaphoreDelete(s_frameMutex);
            s_frameMutex = nullptr;
        }
        return false;
    }
    memset(s_framebuffer, 0, frameBytes);
    return true;
}

static bool stationConnected() {
    return WiFi.status() == WL_CONNECTED
        && WiFi.getMode() != WIFI_AP
        && (uint32_t)WiFi.localIP() != 0;
}

// Clip one flush rectangle to the mirror, and answer whether any of it lands.
// Shared by the two capture entry points so the RGB565 and 1 bpp paths cannot
// drift apart on what counts as visible.
static bool clipFlush(int32_t x, int32_t y, int32_t width, int32_t height,
                      int32_t &x1, int32_t &y1, int32_t &x2, int32_t &y2) {
    if (!s_enabled || !s_clientConnected || !s_framebuffer
        || width <= 0 || height <= 0) {
        return false;
    }
    x1 = x < 0 ? 0 : x;
    y1 = y < 0 ? 0 : y;
    x2 = x + width - 1;
    y2 = y + height - 1;
    if (x2 >= s_screenW) x2 = (int32_t)s_screenW - 1;
    if (y2 >= s_screenH) y2 = (int32_t)s_screenH - 1;
    return x1 <= x2 && y1 <= y2;
}

// Union a just-written rectangle into the region the sender still owes the
// browser. Call with the frame mutex held.
static void markDirty(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    __sync_synchronize();
    portENTER_CRITICAL(&s_dirtyMux);
    if (!s_dirty) {
        s_dirtyX1 = (int16_t)x1;
        s_dirtyY1 = (int16_t)y1;
        s_dirtyX2 = (int16_t)x2;
        s_dirtyY2 = (int16_t)y2;
        s_dirty = true;
    } else {
        if (x1 < s_dirtyX1) s_dirtyX1 = (int16_t)x1;
        if (y1 < s_dirtyY1) s_dirtyY1 = (int16_t)y1;
        if (x2 > s_dirtyX2) s_dirtyX2 = (int16_t)x2;
        if (y2 > s_dirtyY2) s_dirtyY2 = (int16_t)y2;
    }
    portEXIT_CRITICAL(&s_dirtyMux);
}

}  // namespace

void vncHostInit(uint16_t width, uint16_t height) {
    s_screenW = width;
    s_screenH = height;
}

bool vncHostSetEnabled(bool enabled) {
    if (enabled) {
        if (!stationConnected()) {
            Serial.println("[vnc] enable rejected: WiFi station is not connected");
            return false;
        }
        if (!ensureBuffers()) {
            Serial.println("[vnc] PSRAM framebuffer allocation failed");
            return false;
        }
        if (!s_task) {
            if (xTaskCreatePinnedToCore(vncTask, "vnc_host", 6144, nullptr, 1,
                                        &s_task, 0) != pdPASS) {
                Serial.println("[vnc] server task creation failed");
                s_task = nullptr;
                return false;
            }
        }
        __atomic_store_n(&s_fullRepaintRequested, true, __ATOMIC_RELEASE);
        if (!s_enabled) {
            portENTER_CRITICAL(&s_authMux);
            clearAuthLocked();
            generatePinLocked();
            ++s_session;
            portEXIT_CRITICAL(&s_authMux);
        }
    }
    s_enabled = enabled;
    if (!enabled) {
        portENTER_CRITICAL(&s_authMux);
        clearAuthLocked();
        portEXIT_CRITICAL(&s_authMux);
        portENTER_CRITICAL(&s_dirtyMux);
        s_dirty = false;
        portEXIT_CRITICAL(&s_dirtyMux);
        portENTER_CRITICAL(&s_pointerMux);
        s_pointerKnown = false;
        s_pointerPressed = false;
        portEXIT_CRITICAL(&s_pointerMux);
        portENTER_CRITICAL(&s_keyMux);
        s_keyTail = s_keyHead;
        portEXIT_CRITICAL(&s_keyMux);
        s_lastKeyMs = 0;
    }
    return true;
}

bool vncHostEnabled() { return s_enabled; }

const char *vncHostPin() { return s_enabled ? s_pin : ""; }
uint32_t vncHostPinSerial() { return s_pinSerial; }
uint32_t vncHostSession() { return s_session; }

uint32_t vncHostUnlockRetryMs() {
    portENTER_CRITICAL(&s_authMux);
    const uint32_t elapsed = (uint32_t)(millis() - s_lastPinFailMs);
    const uint32_t wait = (s_pinFailedRecently && elapsed < kPinRetryDelayMs)
        ? kPinRetryDelayMs - elapsed : 0;
    portEXIT_CRITICAL(&s_authMux);
    return wait;
}

bool vncHostRedeemPin(const char *pin, char *tokenOut, size_t tokenLen) {
    if (tokenOut && tokenLen) tokenOut[0] = '\0';
    if (!s_enabled || !pin || !tokenOut || tokenLen < kTokenHexLen + 1) return false;
    bool wellFormed = strlen(pin) == (size_t)kPinDigits;
    for (int i = 0; wellFormed && i < kPinDigits; ++i) {
        wellFormed = pin[i] >= '0' && pin[i] <= '9';
    }
    // Drawn outside the critical section: esp_random() is cheap, but there is
    // no reason to hold a spinlock across sixteen bytes of hex formatting.
    char token[kTokenHexLen + 1];
    randomHex(token, kTokenBytes);

    bool ok = false;
    bool regenerated = false;
    portENTER_CRITICAL(&s_authMux);
    const uint32_t now = millis();
    const bool throttled = s_pinFailedRecently
        && (uint32_t)(now - s_lastPinFailMs) < kPinRetryDelayMs;
    if (s_pin[0] && !throttled) {
        ok = wellFormed && constantTimeEqual(pin, s_pin, kPinDigits);
        if (ok) {
            memcpy(s_tokens[s_tokenNext], token, sizeof(token));
            s_tokenNext = (uint8_t)((s_tokenNext + 1) % kTokenSlots);
            s_pinFailures = 0;
            s_pinFailedRecently = false;
        } else {
            s_lastPinFailMs = now;
            s_pinFailedRecently = true;
            if (++s_pinFailures >= kMaxPinFailures) {
                // Only the PIN changes: tokens already issued stay valid.
                generatePinLocked();
                s_lastPinFailMs = now;
                s_pinFailedRecently = true;
                regenerated = true;
            }
        }
    }
    portEXIT_CRITICAL(&s_authMux);

    if (ok) {
        memcpy(tokenOut, token, sizeof(token));
        Serial.println("[vnc] PIN accepted, session token issued");
    } else if (regenerated) {
        Serial.println("[vnc] too many wrong PINs, new PIN generated");
    } else if (!throttled) {
        Serial.println("[vnc] wrong PIN");
    }
    memset(token, 0, sizeof(token));
    return ok;
}

bool vncHostRunning() { return s_running; }
bool vncHostClientConnected() { return s_clientConnected; }
const char *vncHostIP() { return s_ip; }
uint16_t vncHostPort() { return kVncPort; }

void vncHostCaptureFlush(int32_t x, int32_t y, int32_t width, int32_t height,
                         const uint16_t *pixels) {
    if (!pixels) return;
    int32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    if (!clipFlush(x, y, width, height, x1, y1, x2, y2)) return;
    if (!s_frameMutex || xSemaphoreTake(s_frameMutex, portMAX_DELAY) != pdTRUE) {
        __atomic_store_n(&s_fullRepaintRequested, true, __ATOMIC_RELEASE);
        return;
    }
    const size_t copyWidth = (size_t)(x2 - x1 + 1);
    for (int32_t row = y1; row <= y2; ++row) {
        const size_t sourceOffset = (size_t)(row - y) * width + (x1 - x);
        memcpy(s_framebuffer + (size_t)row * s_screenW + x1,
               pixels + sourceOffset, copyWidth * sizeof(uint16_t));
    }
    markDirty(x1, y1, x2, y2);
    xSemaphoreGive(s_frameMutex);
}

void vncHostCaptureFlushI1(int32_t x, int32_t y, int32_t width, int32_t height,
                           const uint8_t *bits, size_t strideBytes) {
    if (!bits || strideBytes == 0) return;
    int32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    if (!clipFlush(x, y, width, height, x1, y1, x2, y2)) return;
    if (!s_frameMutex || xSemaphoreTake(s_frameMutex, portMAX_DELAY) != pdTRUE) {
        __atomic_store_n(&s_fullRepaintRequested, true, __ATOMIC_RELEASE);
        return;
    }
    // 0xFFFF and 0x0000 are the same bytes either way round, so the two colours
    // a 1 bpp panel can produce need no endian handling to reach the wire.
    for (int32_t row = y1; row <= y2; ++row) {
        const uint8_t *source = bits + (size_t)(row - y) * strideBytes;
        uint16_t *dest = s_framebuffer + (size_t)row * s_screenW + x1;
        for (int32_t col = x1; col <= x2; ++col) {
            const int32_t bit = col - x;
            *dest++ = (source[bit >> 3] & (0x80u >> (bit & 7))) ? 0xFFFF : 0x0000;
        }
    }
    markDirty(x1, y1, x2, y2);
    xSemaphoreGive(s_frameMutex);
}

bool vncHostTakeFullRepaintRequest() {
    return __atomic_exchange_n(&s_fullRepaintRequested, false, __ATOMIC_ACQ_REL);
}

bool vncHostReadPointer(int16_t *x, int16_t *y, bool *pressed) {
    if (!s_clientConnected) return false;
    portENTER_CRITICAL(&s_pointerMux);
    const bool known = s_pointerKnown;
    if (known) {
        if (x) *x = s_pointerX;
        if (y) *y = s_pointerY;
        if (pressed) *pressed = s_pointerPressed;
    }
    portEXIT_CRITICAL(&s_pointerMux);
    return known;
}

uint32_t vncHostLastKeyMs() { return s_lastKeyMs; }

uint8_t vncHostPendingKeys() {
    portENTER_CRITICAL(&s_keyMux);
    const uint16_t head = s_keyHead;
    const uint16_t tail = s_keyTail;
    portEXIT_CRITICAL(&s_keyMux);
    const uint16_t pending =
        (uint16_t)((head + kKeyRingSize - tail) % kKeyRingSize);
    return pending > 255 ? (uint8_t)255 : (uint8_t)pending;
}

bool vncHostPopKey(uint16_t *codepoint) {
    if (!s_enabled) return false;
    portENTER_CRITICAL(&s_keyMux);
    if (s_keyHead == s_keyTail) {
        portEXIT_CRITICAL(&s_keyMux);
        return false;
    }
    if (codepoint) *codepoint = s_keys[s_keyTail];
    s_keyTail = (uint16_t)((s_keyTail + 1) % kKeyRingSize);
    portEXIT_CRITICAL(&s_keyMux);
    return true;
}

#else

void vncHostInit(uint16_t, uint16_t) {}
bool vncHostSetEnabled(bool enabled) { return !enabled; }
bool vncHostEnabled() { return false; }
const char *vncHostPin() { return ""; }
uint32_t vncHostPinSerial() { return 0; }
uint32_t vncHostSession() { return 0; }
uint32_t vncHostUnlockRetryMs() { return 0; }
bool vncHostRedeemPin(const char *, char *tokenOut, size_t tokenLen) {
    if (tokenOut && tokenLen) tokenOut[0] = '\0';
    return false;
}
bool vncHostRunning() { return false; }
bool vncHostClientConnected() { return false; }
const char *vncHostIP() { return ""; }
uint16_t vncHostPort() { return 0; }
void vncHostCaptureFlush(int32_t, int32_t, int32_t, int32_t, const uint16_t *) {}
void vncHostCaptureFlushI1(int32_t, int32_t, int32_t, int32_t, const uint8_t *,
                           size_t) {}
bool vncHostTakeFullRepaintRequest() { return false; }
bool vncHostReadPointer(int16_t *, int16_t *, bool *) { return false; }
bool vncHostPopKey(uint16_t *) { return false; }
uint32_t vncHostLastKeyMs() { return 0; }
uint8_t vncHostPendingKeys() { return 0; }

#endif
