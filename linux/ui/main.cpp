// SPDX-License-Identifier: GPL-3.0-or-later
// camillia-czero-ui: the CardputerZero screen for the Camillia-MT daemon.
//
// Draws on the LCD framebuffer with LVGL and reads the keypad directly; all
// radio work is the daemon's (camillia-czero), reached over its Unix socket
// (common/ipc.h). Started from the M5 launcher, which hands over the screen
// and keyboard and sends SIGINT to take them back.
//
// Keys: Up/Down select or scroll, Enter opens or sends, Esc goes back (and
// quits from the conversation list), Tab switches conversations <-> nodes.
#include "common/ipc.h"
#include "daemon/store.h"
#include "ui/keyboard.h"

#include <lvgl.h>
#include <src/drivers/display/fb/lv_linux_fbdev.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

LV_FONT_DECLARE(lv_font_montserrat_10)
LV_FONT_DECLARE(lv_font_montserrat_12)
LV_FONT_DECLARE(lv_font_montserrat_14)

namespace {

// Camillia Dark, the firmware's default theme (main_lvgl.cpp kUiThemePresets).
lv_color_t c565(uint16_t v) {
    return lv_color_make((uint8_t)(((v >> 11) & 0x1F) * 255 / 31), (uint8_t)(((v >> 5) & 0x3F) * 255 / 63),
                         (uint8_t)((v & 0x1F) * 255 / 31));
}
const lv_color_t kBg = c565(0x0843), kPanel = c565(0x1065), kAlt = c565(0x18A7), kAccent = c565(0xDA8E);
const lv_color_t kText = lv_color_hex(0xE8E6EE), kDim = lv_color_hex(0x9A96A8);

struct Node {
    uint32_t id = 0;
    std::string shortName, longName;
    bool hasKey = false;
    uint32_t lastHeard = 0;
    float rssi = 0, snr = 0;
    int hops = -1;
};

enum class View { Convs, Chat, Nodes };

volatile sig_atomic_t gQuit = 0;
std::string gSocketPath, gReadPath;
int gSock = -1;
std::string gIn;
bool gSynced = false;
uint32_t gNextConnectMs = 0;

uint32_t gMe = 0;
std::string gMyShort, gRegion, gPreset;
std::map<int, std::string> gChannels;
std::map<uint32_t, Node> gNodes;
std::vector<Msg> gMsgs;
bool gGpsFix = false;
std::map<std::string, uint32_t> gReadUpTo;   // conv -> epoch read up to

View gView = View::Convs;
std::string gConv;            // open conversation
int gSel = 0;                 // selection in Convs / Nodes
bool gDirty = true;
std::string gCompose;

lv_obj_t *gTitle, *gStatus, *gBody, *gComposeTa;

void onSignal(int) { gQuit = 1; }

uint32_t nowMs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// ── Model ───────────────────────────────────────────────────────────────────

std::string nodeName(uint32_t id, bool longForm = false) {
    auto it = gNodes.find(id);
    if (it != gNodes.end()) {
        if (longForm && !it->second.longName.empty()) return it->second.longName;
        if (!it->second.shortName.empty()) return it->second.shortName;
    }
    return "!" + ipc::hex32(id).substr(4);
}

std::string convTitle(const std::string &conv) {
    if (conv.size() > 1 && conv[0] == 'c') {
        auto it = gChannels.find(atoi(conv.c_str() + 1));
        return "#" + (it != gChannels.end() ? it->second : conv.substr(1));
    }
    if (conv.size() > 1 && conv[0] == 'd') return nodeName((uint32_t)strtoul(conv.c_str() + 1, nullptr, 16), true);
    return conv;
}

struct ConvInfo {
    std::string conv;
    const Msg *last = nullptr;
    int unread = 0;
};

// Channels first in their order, then DMs, most recent first.
std::vector<ConvInfo> conversations() {
    std::map<std::string, ConvInfo> by;
    for (auto &kv : gChannels) {
        std::string c = "c" + std::to_string(kv.first);
        by[c].conv = c;
    }
    for (auto &m : gMsgs) {
        ConvInfo &ci = by[m.conv];
        ci.conv = m.conv;
        ci.last = &m;
        if (m.state == 'r' && m.epoch > gReadUpTo[m.conv]) ci.unread++;
    }
    if (gView == View::Chat && !gConv.empty()) by[gConv].conv = gConv;
    std::vector<ConvInfo> chans, dms;
    for (auto &kv : by) (kv.first[0] == 'c' ? chans : dms).push_back(kv.second);
    std::sort(chans.begin(), chans.end(),
              [](const ConvInfo &a, const ConvInfo &b) { return atoi(a.conv.c_str() + 1) < atoi(b.conv.c_str() + 1); });
    std::sort(dms.begin(), dms.end(), [](const ConvInfo &a, const ConvInfo &b) {
        return (a.last ? a.last->epoch : 0) > (b.last ? b.last->epoch : 0);
    });
    chans.insert(chans.end(), dms.begin(), dms.end());
    return chans;
}

std::vector<const Node *> nodesByRecency() {
    std::vector<const Node *> v;
    for (auto &kv : gNodes)
        if (kv.first != gMe) v.push_back(&kv.second);
    std::sort(v.begin(), v.end(), [](const Node *a, const Node *b) { return a->lastHeard > b->lastHeard; });
    return v;
}

void loadReadMarks() {
    if (FILE *f = fopen(gReadPath.c_str(), "r")) {
        char conv[32];
        unsigned long e;
        while (fscanf(f, "%31s %lu", conv, &e) == 2) gReadUpTo[conv] = (uint32_t)e;
        fclose(f);
    }
}

void markRead(const std::string &conv) {
    uint32_t newest = gReadUpTo[conv];
    for (auto &m : gMsgs)
        if (m.conv == conv && m.epoch > newest) newest = m.epoch;
    if (newest == gReadUpTo[conv]) return;
    gReadUpTo[conv] = newest;
    std::string tmp = gReadPath + ".tmp";
    if (FILE *f = fopen(tmp.c_str(), "w")) {
        for (auto &kv : gReadUpTo) fprintf(f, "%s %u\n", kv.first.c_str(), kv.second);
        fclose(f);
        rename(tmp.c_str(), gReadPath.c_str());
    }
}

std::string hhmm(uint32_t epoch) {
    if (!epoch) return "";
    time_t t = epoch, now = time(nullptr);
    tm lt, ln;
    localtime_r(&t, &lt);
    localtime_r(&now, &ln);
    char b[16];
    if (lt.tm_yday == ln.tm_yday && lt.tm_year == ln.tm_year) strftime(b, sizeof b, "%H:%M", &lt);
    else strftime(b, sizeof b, "%b %d", &lt);
    return b;
}

std::string ago(uint32_t epoch) {
    time_t now = time(nullptr);
    if (!epoch || now < (time_t)epoch) return "-";
    long s = (long)(now - epoch);
    char b[24];
    if (s < 60) snprintf(b, sizeof b, "%lds", s);
    else if (s < 3600) snprintf(b, sizeof b, "%ldm", s / 60);
    else if (s < 86400) snprintf(b, sizeof b, "%ldh", s / 3600);
    else snprintf(b, sizeof b, "%ldd", s / 86400);
    return b;
}

const char *stateText(char s) {
    switch (s) {
    case 'p': return "sending";
    case 's': return "sent";
    case 'R': return "relayed";
    case 'A': return LV_SYMBOL_OK " delivered";
    case 'N': return "refused";
    case 'F': return LV_SYMBOL_WARNING " failed";
    default: return "";
    }
}

// ── Daemon connection ──────────────────────────────────────────────────────

void disconnect() {
    if (gSock >= 0) close(gSock);
    gSock = -1;
    gSynced = false;
    gIn.clear();
    gDirty = true;
}

void tryConnect() {
    if (gSock >= 0 || nowMs() - gNextConnectMs > 0x80000000u) return;
    gNextConnectMs = nowMs() + 2000;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un a = {};
    a.sun_family = AF_UNIX;
    strncpy(a.sun_path, gSocketPath.c_str(), sizeof a.sun_path - 1);
    if (fd < 0 || connect(fd, (sockaddr *)&a, sizeof a) != 0) {
        if (fd >= 0) close(fd);
        return;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    gSock = fd;
    // The daemon replays everything on connect.
    gChannels.clear();
    gNodes.clear();
    gMsgs.clear();
    gDirty = true;
}

void sendLine(const std::string &line) {
    if (gSock < 0) return;
    std::string out = line + "\n";
    if (send(gSock, out.data(), out.size(), MSG_NOSIGNAL) != (ssize_t)out.size()) disconnect();
}

void handleRecord(const std::string &line) {
    auto f = ipc::split(line);
    if (f.empty()) return;
    const std::string &t = f[0];
    if (t == "I" && f.size() >= 6) {
        gMe = (uint32_t)strtoul(f[1].c_str(), nullptr, 16);
        gMyShort = f[3];
        gRegion = f[4];
        gPreset = f[5];
    } else if (t == "C" && f.size() >= 3) {
        gChannels[atoi(f[1].c_str())] = f[2];
    } else if (t == "N" && f.size() >= 9) {
        Node n;
        n.id = (uint32_t)strtoul(f[1].c_str(), nullptr, 16);
        n.shortName = f[2];
        n.longName = f[3];
        n.hasKey = f[4] == "1";
        n.lastHeard = (uint32_t)strtoul(f[5].c_str(), nullptr, 10);
        n.rssi = strtof(f[6].c_str(), nullptr);
        n.snr = strtof(f[7].c_str(), nullptr);
        n.hops = atoi(f[8].c_str());
        gNodes[n.id] = n;
        if (gView == View::Chat) return;   // names in an open chat rarely change
    } else if (t == "M") {
        Msg m;
        if (!parseMsgRecord(f, m)) return;
        gMsgs.push_back(m);
        if (gMsgs.size() > MessageStore::kKeep) gMsgs.erase(gMsgs.begin());
        if (gSynced && gView == View::Chat && m.conv == gConv) markRead(gConv);
    } else if (t == "S" && f.size() >= 3 && f[2].size() == 1) {
        uint32_t id = (uint32_t)strtoul(f[1].c_str(), nullptr, 16);
        for (auto it = gMsgs.rbegin(); it != gMsgs.rend(); ++it)
            if (it->id == id && it->from == gMe) { it->state = f[2][0]; break; }
    } else if (t == "G" && f.size() >= 2) {
        gGpsFix = f[1] == "1";
    } else if (t == "E") {
        gSynced = true;
    }
    gDirty = true;
}

void readSocket() {
    char buf[4096];
    for (;;) {
        ssize_t n = recv(gSock, buf, sizeof buf, MSG_DONTWAIT);
        if (n > 0) {
            gIn.append(buf, (size_t)n);
            size_t nl;
            while ((nl = gIn.find('\n')) != std::string::npos) {
                std::string line = gIn.substr(0, nl);
                gIn.erase(0, nl + 1);
                handleRecord(line);
            }
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
        disconnect();
        return;
    }
}

// ── Drawing ────────────────────────────────────────────────────────────────

lv_obj_t *label(lv_obj_t *parent, const std::string &text, const lv_font_t *font, lv_color_t color) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text.c_str());
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

lv_obj_t *plain(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    return o;
}

lv_obj_t *listBox() {
    lv_obj_t *list = plain(gBody);
    lv_obj_set_size(list, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 1, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    return list;
}

lv_obj_t *row(lv_obj_t *list, bool selected) {
    lv_obj_t *r = plain(list);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, selected ? kAlt : kPanel, 0);
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(r, selected ? 3 : 0, 0);
    lv_obj_set_style_border_color(r, kAccent, 0);
    lv_obj_set_style_pad_hor(r, 6, 0);
    lv_obj_set_style_pad_ver(r, 3, 0);
    return r;
}

// One line: left text (clipped) and a right-hand note.
void rowLine(lv_obj_t *r, const std::string &left, const std::string &right, const lv_font_t *font,
             lv_color_t color, int y) {
    lv_obj_t *l = label(r, left, font, color);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(l, right.empty() ? lv_pct(100) : lv_pct(72));
    lv_obj_set_pos(l, 0, y);
    if (!right.empty()) {
        lv_obj_t *n = label(r, right, &lv_font_montserrat_10, kDim);
        lv_obj_align(n, LV_ALIGN_TOP_RIGHT, 0, y + 2);
    }
}

void drawConvs() {
    auto convs = conversations();
    if (gSel >= (int)convs.size()) gSel = (int)convs.size() - 1;
    if (gSel < 0) gSel = 0;
    lv_obj_t *list = listBox(), *selRow = nullptr;
    for (int i = 0; i < (int)convs.size(); i++) {
        const ConvInfo &c = convs[i];
        lv_obj_t *r = row(list, i == gSel);
        if (i == gSel) selRow = r;
        std::string note = c.unread ? std::to_string(c.unread) + " new" : c.last ? hhmm(c.last->epoch) : "";
        rowLine(r, convTitle(c.conv), note, &lv_font_montserrat_14, c.unread ? kAccent : kText, 0);
        std::string preview = "No messages";
        if (c.last) preview = (c.last->from == gMe ? std::string("You") : nodeName(c.last->from)) + ": " + c.last->text;
        std::replace(preview.begin(), preview.end(), '\n', ' ');
        rowLine(r, preview, "", &lv_font_montserrat_12, kDim, 17);
    }
    if (selRow) lv_obj_scroll_to_view(selRow, LV_ANIM_OFF);
    lv_label_set_text(gTitle, ("Camillia  " + gRegion + " " + gPreset).c_str());
}

void drawNodes() {
    auto nodes = nodesByRecency();
    if (gSel >= (int)nodes.size()) gSel = (int)nodes.size() - 1;
    if (gSel < 0) gSel = 0;
    lv_obj_t *list = listBox(), *selRow = nullptr;
    for (int i = 0; i < (int)nodes.size(); i++) {
        const Node &n = *nodes[i];
        lv_obj_t *r = row(list, i == gSel);
        if (i == gSel) selRow = r;
        std::string name = (n.shortName.empty() ? "?" : n.shortName) + "  " +
                           (n.longName.empty() ? "!" + ipc::hex32(n.id) : n.longName);
        rowLine(r, name, ago(n.lastHeard), &lv_font_montserrat_14, kText, 0);
        char detail[96];
        if (n.hops > 0) snprintf(detail, sizeof detail, "!%08x  %d hop%s%s", n.id, n.hops, n.hops == 1 ? "" : "s",
                                 n.hasKey ? "  " LV_SYMBOL_EYE_CLOSE : "");
        else snprintf(detail, sizeof detail, "!%08x  %.0f dBm  %.1f dB%s", n.id, n.rssi, n.snr,
                      n.hasKey ? "  " LV_SYMBOL_EYE_CLOSE : "");
        rowLine(r, detail, "", &lv_font_montserrat_12, kDim, 17);
    }
    if (selRow) lv_obj_scroll_to_view(selRow, LV_ANIM_OFF);
    lv_label_set_text(gTitle, ("Nodes (" + std::to_string(nodes.size()) + ")").c_str());
}

lv_obj_t *gChatList = nullptr;

void drawChat() {
    lv_obj_t *col = plain(gBody);
    lv_obj_set_size(col, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);

    gChatList = plain(col);
    lv_obj_set_width(gChatList, lv_pct(100));
    lv_obj_set_flex_grow(gChatList, 1);
    lv_obj_set_flex_flow(gChatList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(gChatList, 3, 0);
    lv_obj_set_style_pad_row(gChatList, 3, 0);
    lv_obj_set_scrollbar_mode(gChatList, LV_SCROLLBAR_MODE_OFF);

    const bool isChannel = gConv[0] == 'c';
    int shown = 0;
    for (auto &m : gMsgs) {
        if (m.conv != gConv) continue;
        shown++;
        const bool mine = m.from == gMe;
        lv_obj_t *line = plain(gChatList);
        lv_obj_set_size(line, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(line, mine ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);

        lv_obj_t *bubble = plain(line);
        lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_max_width(bubble, lv_pct(85), 0);
        lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bubble, mine ? lv_color_mix(kAccent, kPanel, 90) : kPanel, 0);
        lv_obj_set_style_radius(bubble, 6, 0);
        lv_obj_set_style_pad_hor(bubble, 6, 0);
        lv_obj_set_style_pad_ver(bubble, 3, 0);

        std::string meta = mine ? std::string(stateText(m.state)) : (isChannel ? nodeName(m.from) : std::string());
        std::string when = hhmm(m.epoch);
        if (!when.empty()) meta = meta.empty() ? when : meta + "  " + when;
        if (!mine && m.hops > 0) meta += "  " + std::to_string(m.hops) + "h";
        if (!meta.empty()) label(bubble, meta, &lv_font_montserrat_10, mine ? kText : kAccent);
        lv_obj_t *t = label(bubble, m.text, &lv_font_montserrat_14, kText);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_max_width(t, 260, 0);
        lv_obj_set_width(t, LV_SIZE_CONTENT);
    }
    if (!shown) {
        lv_obj_t *l = label(gChatList, isChannel ? "No messages on this channel yet." : "No messages yet. Say hello.",
                            &lv_font_montserrat_12, kDim);
        lv_obj_set_style_pad_top(l, 6, 0);
    }

    gComposeTa = lv_textarea_create(col);
    lv_textarea_set_one_line(gComposeTa, true);
    lv_textarea_set_placeholder_text(gComposeTa, "Message, Enter to send");
    lv_textarea_set_text(gComposeTa, gCompose.c_str());
    lv_obj_set_width(gComposeTa, lv_pct(100));
    lv_obj_set_style_text_font(gComposeTa, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(gComposeTa, kAlt, 0);
    lv_obj_set_style_text_color(gComposeTa, kText, 0);
    lv_obj_set_style_border_width(gComposeTa, 0, 0);
    lv_obj_set_style_radius(gComposeTa, 0, 0);
    lv_obj_set_style_pad_all(gComposeTa, 3, 0);
    lv_obj_add_state(gComposeTa, LV_STATE_FOCUSED);

    lv_obj_update_layout(col);
    lv_obj_scroll_to_y(gChatList, LV_COORD_MAX, LV_ANIM_OFF);
    lv_label_set_text(gTitle, convTitle(gConv).c_str());
}

void redraw() {
    gDirty = false;
    lv_obj_clean(gBody);
    gComposeTa = nullptr;
    gChatList = nullptr;
    if (gSock < 0 || !gSynced) {
        lv_label_set_text(gTitle, "Camillia");
        lv_obj_t *l = label(gBody, gSock < 0 ? "Waiting for the radio service...\n\n"
                                               "sudo systemctl start camillia-czero"
                                             : "Loading...",
                            &lv_font_montserrat_14, kDim);
        lv_obj_center(l);
        lv_label_set_text(gStatus, "");
        return;
    }
    if (gView == View::Convs) drawConvs();
    else if (gView == View::Nodes) drawNodes();
    else drawChat();

    int heard = 0;
    time_t now = time(nullptr);
    for (auto &kv : gNodes)
        if (kv.second.lastHeard && now - (time_t)kv.second.lastHeard < 2 * 3600) heard++;
    std::string status = std::to_string(heard) + " " LV_SYMBOL_WIFI;
    if (gGpsFix) status += "  " LV_SYMBOL_GPS;
    std::string t = hhmm((uint32_t)now);
    if (!t.empty()) status += "  " + t;
    lv_label_set_text(gStatus, status.c_str());
}

void buildScreen() {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, kBg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *bar = plain(scr);
    lv_obj_set_size(bar, lv_pct(100), 18);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, kPanel, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, kAccent, 0);
    gTitle = label(bar, "Camillia", &lv_font_montserrat_12, kText);
    lv_label_set_long_mode(gTitle, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(gTitle, 190);
    lv_obj_align(gTitle, LV_ALIGN_LEFT_MID, 4, 0);
    gStatus = label(bar, "", &lv_font_montserrat_12, kDim);
    lv_obj_align(gStatus, LV_ALIGN_RIGHT_MID, -4, 0);

    gBody = plain(scr);
    lv_obj_set_size(gBody, lv_pct(100), lv_display_get_vertical_resolution(nullptr) - 18);
    lv_obj_set_pos(gBody, 0, 18);
}

// ── Keys ───────────────────────────────────────────────────────────────────

void openChat(const std::string &conv) {
    gConv = conv;
    gCompose.clear();
    gView = View::Chat;
    markRead(conv);
    gDirty = true;
}

void onKey(const KeyEvent &k) {
    if (gSock < 0 || !gSynced) {
        if (k.key == Key::Esc) gQuit = 1;
        return;
    }
    switch (gView) {
    case View::Convs:
    case View::Nodes: {
        int count = gView == View::Convs ? (int)conversations().size() : (int)nodesByRecency().size();
        if (k.key == Key::Up && gSel > 0) gSel--;
        else if (k.key == Key::Down && gSel + 1 < count) gSel++;
        else if (k.key == Key::PageUp) gSel = std::max(0, gSel - 5);
        else if (k.key == Key::PageDown) gSel = std::min(count - 1, gSel + 5);
        else if (k.key == Key::Tab) { gView = gView == View::Convs ? View::Nodes : View::Convs; gSel = 0; }
        else if (k.key == Key::Esc) {
            if (gView == View::Nodes) { gView = View::Convs; gSel = 0; }
            else gQuit = 1;
        } else if (k.key == Key::Enter && count > 0) {
            if (gView == View::Convs) openChat(conversations()[gSel].conv);
            else openChat("d" + ipc::hex32(nodesByRecency()[gSel]->id));
        } else return;
        gDirty = true;
        return;
    }
    case View::Chat:
        if (k.key == Key::Esc) {
            gView = View::Convs;
            gDirty = true;
        } else if (k.key == Key::Enter) {
            if (!gCompose.empty()) {
                sendLine(ipc::join(std::string("T"), gConv, ipc::esc(gCompose)));
                gCompose.clear();
                if (gComposeTa) lv_textarea_set_text(gComposeTa, "");
            }
        } else if (k.key == Key::Backspace) {
            if (!gCompose.empty()) {
                // Drop one UTF-8 character, not one byte.
                size_t i = gCompose.size() - 1;
                while (i > 0 && ((unsigned char)gCompose[i] & 0xC0) == 0x80) i--;
                gCompose.erase(i);
                if (gComposeTa) lv_textarea_delete_char(gComposeTa);
            }
        } else if (k.key == Key::Char) {
            if (gCompose.size() + k.text.size() <= 200) {
                gCompose += k.text;
                if (gComposeTa) lv_textarea_add_text(gComposeTa, k.text.c_str());
            }
        } else if (gChatList && (k.key == Key::Up || k.key == Key::Down || k.key == Key::PageUp || k.key == Key::PageDown)) {
            int step = (k.key == Key::Up || k.key == Key::Down) ? 30 : 120;
            lv_obj_scroll_by_bounded(gChatList, 0, (k.key == Key::Up || k.key == Key::PageUp) ? step : -step,
                                     LV_ANIM_OFF);
        }
        return;
    }
}

uint32_t tickCb() { return nowMs(); }

}  // namespace

int main(int argc, char **argv) {
    gSocketPath = ipc::defaultSocketPath();
    std::string fb = access("/dev/fb_lcd", F_OK) == 0 ? "/dev/fb_lcd" : "/dev/fb0";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--socket") && i + 1 < argc) gSocketPath = argv[++i];
        else if (!strcmp(argv[i], "--fb") && i + 1 < argc) fb = argv[++i];
        else {
            fprintf(stderr, "usage: %s [--socket PATH] [--fb /dev/fbN]\n", argv[0]);
            return 2;
        }
    }
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    signal(SIGPIPE, SIG_IGN);

    const char *home = getenv("HOME");
    const char *xdg = getenv("XDG_CONFIG_HOME");
    std::string dir = (xdg && *xdg) ? std::string(xdg) + "/camillia" : std::string(home ? home : ".") + "/.config/camillia";
    mkdir(dir.c_str(), 0700);
    gReadPath = dir + "/ui-read";
    loadReadMarks();

    Keyboard kbd;
    if (!kbd.open()) fprintf(stderr, "[ui] no keypad found; set CAMILLIA_KBD=/dev/input/eventN\n");

    lv_init();
    lv_tick_set_cb(tickCb);
    lv_display_t *disp = lv_linux_fbdev_create();
    if (!disp) {
        fprintf(stderr, "[ui] cannot create the display\n");
        return 1;
    }
    lv_linux_fbdev_set_file(disp, fb.c_str());
    buildScreen();

    uint32_t lastClock = 0;
    while (!gQuit) {
        tryConnect();
        std::vector<pollfd> pfds;
        if (kbd.fd() >= 0) pfds.push_back({kbd.fd(), POLLIN, 0});
        if (gSock >= 0) pfds.push_back({gSock, POLLIN, 0});
        uint32_t wait = lv_timer_handler();
        if (wait > 50) wait = 50;
        poll(pfds.data(), pfds.size(), (int)wait);
        for (auto &p : pfds) {
            if (!p.revents) continue;
            if (p.fd == kbd.fd()) {
                for (KeyEvent k = kbd.read(); k.key != Key::None; k = kbd.read()) onKey(k);
            } else if (p.fd == gSock) {
                readSocket();
            }
        }
        // Refresh the clock and "heard" times once a minute.
        uint32_t minute = (uint32_t)(time(nullptr) / 60);
        if (minute != lastClock && gView != View::Chat) { lastClock = minute; gDirty = true; }
        if (gDirty) redraw();
    }
    if (gSock >= 0) close(gSock);
    lv_obj_clean(lv_screen_active());
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    lv_refr_now(nullptr);
    return 0;
}
