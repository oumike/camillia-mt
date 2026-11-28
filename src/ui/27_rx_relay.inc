// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: receive path, ACKs, relaying, chat server, community presets.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

// True when the given real channel has been muted via Channel Actions. Muted
// channels still buffer incoming messages, but raise no visual or audio alert.
static inline bool channelIsMuted(int chanIdx) {
    return chanIdx >= 0 && chanIdx < MESH_CHANNELS && CHANNEL_KEYS[chanIdx].muted;
}

// True when our position should actually go out on this channel. The global
// switch wins: with Share Location off in Settings no channel qualifies,
// whatever its own flag says. A disabled or unnamed slot never qualifies either
// — those are empty channel slots, not places to announce coordinates.
static bool channelSharesLocation(int chanIdx) {
    if (!s_cfg.shareLocation) return false;
    if (chanIdx < 0 || chanIdx >= MESH_CHANNELS) return false;
    const ChannelKey &ck = CHANNEL_KEYS[chanIdx];
    if (!ck.shareLocation) return false;
    if (ck.role == 2) return false;              // DISABLED
    const char *nm = ck.name_buf[0] ? ck.name_buf : ck.name;
    return nm && nm[0];
}

// ── How a message reached us ─────────────────────────────────────────────────
// The chat and DM stores keep what was said and by whom. How it got here -- over
// the air or through an MQTT broker, and which node handed it over on the last
// leg -- is only in the packet header, gone once the text is filed. So each text
// that arrives notes that here, keyed by sender and packet id, and Message Info
// (message actions) reads it back.
//
// A ring: the details are wanted for messages on screen, i.e. recent ones. A
// message older than the ring, or from before this boot, says so rather than
// guessing.
struct MsgRxInfo {
    uint32_t from;
    uint32_t id;
    float    snr;
    float    rssi;
    uint8_t  flags;      // MeshHdr.flags: hop_limit, via_mqtt, hop_start
    uint8_t  relayNode;  // MeshHdr.relay_node: low byte of the last transmitter
};
static constexpr int kMsgRxInfoCount = 64;
static MsgRxInfo s_msgRxInfo[kMsgRxInfoCount] = {};
static int s_msgRxInfoNext = 0;

static void noteMsgRxInfo(const MeshPacket &pkt) {
    MsgRxInfo &r = s_msgRxInfo[s_msgRxInfoNext];
    r.from = pkt.hdr.from;
    r.id = pkt.hdr.id;
    r.snr = pkt.snr;
    r.rssi = pkt.rssi;
    r.flags = pkt.hdr.flags;
    r.relayNode = pkt.hdr.relay_node;
    s_msgRxInfoNext = (s_msgRxInfoNext + 1) % kMsgRxInfoCount;
}

#if HAS_MESSAGE_ACTIONS
static const MsgRxInfo *findMsgRxInfo(uint32_t from, uint32_t packetId) {
    if (packetId == 0) return nullptr;
    for (int i = 0; i < kMsgRxInfoCount; i++) {
        const MsgRxInfo &r = s_msgRxInfo[i];
        if (r.id == packetId && r.from == from) return &r;
    }
    return nullptr;
}

#endif  // HAS_MESSAGE_ACTIONS

// Long name, else short name, else "!aabbccdd" -- the most human thing known.
static void nodeFriendlyName(uint32_t nodeId, char *out, size_t outLen) {
    if (!out || outLen == 0) return;
    if (nodeId == s_myNodeId && s_cfg.nodeLong[0]) {
        snprintf(out, outLen, TR("%s (this node)"), s_cfg.nodeLong);
        return;
    }
    const NodeEntry *e = Nodes.find(nodeId);
    if (e && e->longName[0]) {
        utf8util::copyTruncate(out, outLen, e->longName);
    } else if (e && e->shortName[0]) {
        utf8util::copyTruncate(out, outLen, e->shortName);
    } else {
        snprintf(out, outLen, "!%08lx", (unsigned long)nodeId);
    }
}

#if HAS_MESSAGE_ACTIONS
// The relayer, from the one byte the radio header carries: the low byte of the
// node that transmitted the copy we heard. Any known node ending in that byte
// could be it. One match is an answer; several are listed, directly heard ones
// first since the relayer is by definition a direct neighbour; none means a
// node this one has never heard from itself.
static void describeRelayByte(uint8_t relay, char *out, size_t outLen) {
    uint32_t direct[4] = {};
    uint32_t other[4] = {};
    int nDirect = 0, nOther = 0;
    const int total = Nodes.count();
    for (int i = 0; i < total; i++) {
        const NodeEntry *e = Nodes.at(i);
        if (!e || e->nodeId == 0 || e->nodeId == s_myNodeId) continue;
        if ((uint8_t)(e->nodeId & 0xFF) != relay) continue;
        const bool isDirect = e->hasHops && e->hops == 0;
        if (isDirect && nDirect < 4) direct[nDirect++] = e->nodeId;
        else if (!isDirect && nOther < 4) other[nOther++] = e->nodeId;
    }
    const int n = nDirect + nOther;
    if (n == 0) {
        snprintf(out, outLen, TR("unknown node (id ends ..%02x)"), (unsigned)relay);
        return;
    }
    size_t used = 0;
    if (n > 1) used = (size_t)snprintf(out, outLen, TR("one of: "));
    for (int i = 0; i < n && used < outLen; i++) {
        const uint32_t id = (i < nDirect) ? direct[i] : other[i - nDirect];
        char name[48];
        nodeFriendlyName(id, name, sizeof(name));
        used += (size_t)snprintf(out + used, outLen - used, "%s%s",
                                 (i == 0) ? "" : ", ", name);
    }
}

static void closeMsgSenderInfoModal() {
    lvObjDeleteSafe(s_msgInfoModal);
}

// Who sent a message and how it got here: over LoRa or MQTT -- marked with the
// same icon the chat view puts in front of it (msgTransportIcon()) -- and the
// node that handed it to us on the last leg: the relayer over the air, the
// uplinking gateway over MQTT.
static void openMsgSenderInfoModal(uint32_t from, uint32_t packetId) {
    if (!s_rootScreen) return;
    closeMsgSenderInfoModal();

    char lines[8][112] = {};
    int n = 0;
    char name[64];
    nodeFriendlyName(from, name, sizeof(name));
    snprintf(lines[n++], sizeof(lines[0]), TR("From: %s"), name);
    snprintf(lines[n++], sizeof(lines[0]), TR("Node id: !%08lx"), (unsigned long)from);

    const MsgRxInfo *rx = findMsgRxInfo(from, packetId);
    if (from == s_myNodeId) {
        snprintf(lines[n++], sizeof(lines[0]), TR("Sent by this node."));
    } else if (!rx) {
        snprintf(lines[n++], sizeof(lines[0]),
                 TR("Delivery details not recorded: received before this boot, "
                 "or too many messages ago."));
    } else {
        const bool viaMqtt = (rx->flags & 0x10) != 0;
        const int hopLimit = rx->flags & 0x07;
        const int hopStart = (rx->flags >> 5) & 0x07;
        const int hopsTaken = (hopStart >= hopLimit) ? (hopStart - hopLimit) : -1;
        char who[96];
        snprintf(lines[n++], sizeof(lines[0]), TR("Received over: %s %s"),
                 msgTransportIcon(viaMqtt), viaMqtt ? "MQTT" : "LoRa");
        if (viaMqtt) {
            uint32_t gw = 0;
            if (mqttBridgeGatewayFor(from, packetId, gw)) {
                if (gw == from) {
                    snprintf(lines[n++], sizeof(lines[0]),
                             TR("Uplinked by: the sender itself"));
                } else {
                    nodeFriendlyName(gw, who, sizeof(who));
                    snprintf(lines[n++], sizeof(lines[0]), TR("Uplinked by: %s"), who);
                }
            } else if (rx->relayNode != 0) {
                // Arrived over the air with via_mqtt already set: a node on this
                // mesh downlinked it from a broker and relayed it to us by radio.
                describeRelayByte(rx->relayNode, who, sizeof(who));
                snprintf(lines[n++], sizeof(lines[0]),
                         TR("Relayed to us by: %s (from MQTT)"), who);
            } else {
                snprintf(lines[n++], sizeof(lines[0]), TR("Gateway: not known"));
            }
        } else {
            if (hopStart > 0 && hopsTaken == 0) {
                snprintf(lines[n++], sizeof(lines[0]),
                         TR("Relayed to us by: nobody, heard directly"));
            } else if (rx->relayNode == 0) {
                // Firmware before Meshtastic 2.6 leaves relay_node unset.
                snprintf(lines[n++], sizeof(lines[0]),
                         TR("Relayed to us by: not reported by the relaying node"));
            } else if (rx->relayNode == (uint8_t)(from & 0xFF)) {
                snprintf(lines[n++], sizeof(lines[0]), TR("Relayed to us by: the sender itself"));
            } else {
                describeRelayByte(rx->relayNode, who, sizeof(who));
                snprintf(lines[n++], sizeof(lines[0]), TR("Relayed to us by: %s"), who);
            }
            if (hopsTaken >= 0 && hopStart > 0) {
                snprintf(lines[n++], sizeof(lines[0]), TR("Hops: %d of %d"), hopsTaken, hopStart);
            }
            snprintf(lines[n++], sizeof(lines[0]), TR("Signal: SNR %.1f dB, RSSI %.0f dBm"),
                     (double)rx->snr, (double)rx->rssi);
        }
    }

    int modalW = lv_disp_get_hor_res(NULL) - 24;
    if (modalW < 180) modalW = lv_disp_get_hor_res(NULL) - 8;
    if (modalW > 360) modalW = 360;
#if defined(DEVICE_TDECK) || defined(DEVICE_TDISPLAY_P4)
    const lv_font_t *bodyFont = &lv_font_montserrat_12;
#else
    const lv_font_t *bodyFont = &lv_font_montserrat_10;
#endif

    s_msgInfoModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_msgInfoModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_msgInfoModal, lv_disp_get_ver_res(NULL) - 16, 0);
    lv_obj_align(s_msgInfoModal, LV_ALIGN_CENTER, 0, 0);
    setupVScroll(s_msgInfoModal);
    lv_obj_set_scrollbar_mode(s_msgInfoModal, LV_SCROLLBAR_MODE_AUTO);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_msgInfoModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_msgInfoModal, lv_color_make(0, 0, 0), 0);
    const lv_color_t ink = lv_color_make(0, 0, 0);
    const lv_color_t dim = ink;
#else
    lv_obj_set_style_bg_color(s_msgInfoModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_border_color(s_msgInfoModal, lv_color_hex(0x5C86C6), 0);
    const lv_color_t ink = lv_color_hex(0xD9E8FF);
    const lv_color_t dim = lv_color_hex(0xA7C7FF);
#endif
    lv_obj_set_style_bg_opa(s_msgInfoModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_msgInfoModal, 1, 0);
    lv_obj_set_style_pad_all(s_msgInfoModal, 6, 0);
    lv_obj_set_style_pad_row(s_msgInfoModal, 3, 0);
    lv_obj_set_flex_flow(s_msgInfoModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_msgInfoModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(s_msgInfoModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, ink, 0);
    lv_label_set_text(title, TR("Message Info"));
#if UI_TOUCH_ONLY_PROFILE
    reserveHeltecCloseXRow(title);
    appendHeltecCloseX(s_msgInfoModal,
                       [](lv_event_t *ev) { LV_UNUSED(ev); closeMsgSenderInfoModal(); });
#endif

    for (int i = 0; i < n; i++) {
        lv_obj_t *row = lv_label_create(s_msgInfoModal);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_text_font(row, emojiFont(bodyFont), 0);
        lv_obj_set_style_text_color(row, ink, 0);
        // Wrapped, not ellipsised: a list of relay candidates is the point.
        lv_label_set_long_mode(row, LV_LABEL_LONG_WRAP);
        setLabelTextEmojiSafe(row, lines[i]);
    }

#if !UI_TOUCH_ONLY_PROFILE
    lv_obj_t *hint = lv_label_create(s_msgInfoModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, dim, 0);
    lv_obj_set_style_pad_top(hint, 3, 0);
    lv_label_set_text_fmt(hint, TR("%s = Close"), modalCloseKeyLabel());
#else
    LV_UNUSED(dim);
#endif
    lv_obj_move_foreground(s_msgInfoModal);
}
#endif  // HAS_MESSAGE_ACTIONS

static void appendRxText(int chanIdx, uint32_t fromNode, const char *text, uint32_t packetId, bool viaMqtt,
                         uint32_t replyId = 0, uint32_t emoji = 0) {
    char timePrefix[12];
    char sender[48];
    char prefix[80];

    liveBuildPrefix(timePrefix, sizeof(timePrefix));
#if UI_CARDPUTER_COMPACT
    LV_UNUSED(viaMqtt);
    chatSenderLabel(fromNode, sender, sizeof(sender));
    snprintf(prefix, sizeof(prefix), "%s[%s] ", timePrefix, sender);
#else
    chatSenderLabel(fromNode, sender, sizeof(sender));
    const char *transportIcon = msgTransportIcon(viaMqtt);
    // Keep a small visual buffer between transport icon and timestamp.
    snprintf(prefix, sizeof(prefix), "%s  %s[%s] ", transportIcon, timePrefix, sender);
#endif

    // A tapback is folded into a chip on the message it reacts to, so it has
    // no line of its own to mark new.
    const bool reaction = (emoji != 0 && replyId != 0);
    Channels.addMessage(chanIdx, prefix, text, TFT_WHITE, packetId, false, fromNode, !reaction,
                        false, replyId, reaction);
    // Being the active channel is not the same as being on screen. Behind the
    // glance overlay the chat view is not visible, so the channel the firmware
    // was last on has to notify like any other -- treating it as read is what
    // used to leave its messages off the lock screen entirely.
    const bool chanOnScreen = (chanIdx == s_activeChannel) && !glanceOverlayHidesUi();
    if (chanIdx >= 0 && chanIdx < MESH_CHANNELS && !chanOnScreen
        && !channelIsMuted(chanIdx)) {
        s_channelNeedsAttention[chanIdx] = true;
#if HAS_SLEEP_OVERLAY
        // Same condition, so the lock screen previews exactly the messages the
        // device is raising an alert for -- a muted channel stays silent there
        // too. The unwrapped body is recorded here, before addMessage() bakes a
        // prefix into it and splits it across rows.
        tdeckProNoteRecentMessage(chanIdx, fromNode, text, packetId);
#endif
    }
}

static const char *liveDestTag(uint32_t toNode) {
    if (toNode == 0xFFFFFFFF) return "B";
    if (s_myNodeId != 0 && toNode == s_myNodeId) return "M";
    return "U";
}

// Send our NodeInfo with want_response to a node we have heard but have no name
// for, so it stops showing as !xxxxxxxx. Only for a packet our own keys opened
// over RF from a node within kAutoNodeInfoMaxHops: an MQTT sighting may be a
// continent away, and a sender with no hop_start gives no bound on the flood.
static void maybeRequestUnknownNodeInfo(const MeshPacket &pkt) {
    static uint32_t sLastAutoReqMs = 0;
    // A base station hears far more nodes than a handheld, and asking each of
    // them is airtime spent on a node list nobody may look at. Upstream's
    // preferred rebroadcasters do not ask either.
    if (relay::isRouterRole(s_cfg.deviceRole)) return;
    if (!pkt.decrypted || (pkt.hdr.flags & 0x10)) return;
    // A NodeInfo carries the name itself; an S&F replay is not its author on air.
    if (pkt.portnum == NODEINFO_APP || pkt.portnum == STORE_FORWARD_APP) return;
    const uint32_t from = pkt.hdr.from;
    if (from == 0 || from == 0xFFFFFFFF || from == s_myNodeId || s_myNodeId == 0) return;

    NodeEntry *e = Nodes.find(from);
    // A name restored after an eviction still lacks the key and the rest of the
    // NodeInfo, so it is asked for like an unnamed node.
    if (!e || (e->hasName && !e->nameFromCache)) return;
    if (!e->hasHops || e->hops > kAutoNodeInfoMaxHops) return;

    const uint32_t now = millis();
    if (e->lastSentInfoMs != 0 && (uint32_t)(now - e->lastSentInfoMs) < kAutoNodeInfoPerNodeMs) return;
    if (sLastAutoReqMs != 0 && (uint32_t)(now - sLastAutoReqMs) < kAutoNodeInfoGapMs) return;
    if (!Radio.isReady() || announceHeldForOnboarding() || discoveryRadioParked()) return;
    if (Radio.channelUtilPercent() >= kAutoNodeInfoMaxChUtil) return;

    // Stamped before sending, success or not: a failing send must not be
    // retried on every packet the node sends.
    sLastAutoReqMs = now;
    e->lastSentInfoMs = now;
    const bool ok = Channels.sendNodeInfo(s_myNodeId, s_cfg.nodeLong, s_cfg.nodeShort,
                                          from, /*wantResponse=*/true, s_cfg.okToMqtt);
    debugLogMessages("[nodedb] !%08X unnamed (%u hops) - NodeInfo request %s\n",
                     (unsigned)from, (unsigned)e->hops, ok ? "sent" : "FAILED");
}

static void appendLiveRxSummary(const MeshPacket &pkt, int chanIdx, const char *portTag) {
    char timePrefix[12];
    char who[20];
    char line[96];

    liveBuildPrefix(timePrefix, sizeof(timePrefix));
    liveNodeLabel(pkt.hdr.from, who, sizeof(who), false);
    // Keep the compact, decodable form on every build so formatLiveLineText
    // can render the verbose live row (Cardputer previously emitted a shorter
    // unrecognised form that fell through to the raw fallback).
    snprintf(line, sizeof(line), "R %s>%s %s c%d",
             who,
             liveDestTag(pkt.hdr.to),
             (portTag && portTag[0]) ? portTag : "D",
             chanIdx);
    liveFeedAddPrefixed(timePrefix, line, TFT_DARKGREY, 0, false,
                        (pkt.hdr.flags & 0x10) != 0);
}

static void appendLiveRxEncrypted(const MeshPacket &pkt) {
    char timePrefix[12];
    char who[20];
    char line[96];

    liveBuildPrefix(timePrefix, sizeof(timePrefix));
    liveNodeLabel(pkt.hdr.from, who, sizeof(who), false);
    snprintf(line, sizeof(line), "R %s ENC h%02X", who, pkt.hdr.channel);
    liveFeedAddPrefixed(timePrefix, line, TFT_DARKGREY, 0, false,
                        (pkt.hdr.flags & 0x10) != 0);
}

#if HAS_ADMIN_TERMINAL
// ── Remote admin transport ───────────────────────────────────────────────────
// One session at a time, shared by the device terminal and the web terminal:
// opening the browser onto a node the device already has open continues the same
// conversation rather than starting a rival one with its own session key.
static AdminClient::Session s_adminSession;

// PKI only, on both transports. The target accepts a remote admin message when
// it is pki_encrypted and our public key is in its security.admin_key -- a
// channel-key path exists upstream but needs a deprecated setting that ships
// off, and would put admin commands under a key every channel member holds.
static uint32_t adminSendPacket(uint32_t nodeId, AdminClient::Transport via,
                                const uint8_t *payload, size_t len, void *) {
    // Every admin send funnels here; remote admin is PKI-only, and PKI is off
    // while licensed. The session and probe entry points refuse first.
    if (lic::active()) {
        Serial.printf("[admin] !%08lx refused - licensed mode (no PKI)\n",
                      (unsigned long)nodeId);
        return 0;
    }
    NodeEntry *node = Nodes.find(nodeId);
    if (!node || !node->hasPubKey) {
        Serial.printf("[admin] !%08lx has no public key - cannot send\n",
                      (unsigned long)nodeId);
        return 0;
    }

    // A read asks for a reply; a write is answered by a routing ACK only.
    const bool wantResponse = AdminProto::isReadRequest(payload, len);

    uint8_t proto[256];
    const size_t protoLen = encodeAdminData(payload, len, s_myNodeId, nodeId,
                                            wantResponse, proto, sizeof(proto));
    if (!protoLen) return 0;

    const uint32_t packetId = nextMeshPacketId();
    MeshHdr hdr = {};
    hdr.to    = nodeId;
    hdr.from  = s_myNodeId;
    hdr.id    = packetId;
    hdr.channel = 0;   // PKI marker: channel 0 is not a channel-key hash
    hdr.flags = meshOriginHopFlagsForChannel(node->chanIdx, 1 << 3);   // want_ack
    hdr.relay_node = (uint8_t)(s_myNodeId & 0xFF);

    uint8_t cipher[288];
    if (!encryptPki(packetId, s_myNodeId, node->pubKey, proto, protoLen, cipher)) {
        Serial.println("[admin] PKI encrypt failed");
        return 0;
    }
    const size_t payloadLen = protoLen + 12;   // ciphertext + tag(8) + extraNonce(4)

    if (via == AdminClient::TRANSPORT_MQTT) {
        // The broker path rides the PKI pseudo-channel. mqttBridgePublishRaw
        // already takes an arbitrary channel name and builds
        // <root>/2/e/PKI/<gateway id>, so the send half needs nothing new.
        if (!mqttBridgeConnected()) return 0;
        mqttBridgePublishRaw(hdr, cipher, payloadLen, "PKI");
        Serial.printf("[admin] sent id %08lx to !%08lx via mqtt\n",
                      (unsigned long)packetId, (unsigned long)nodeId);
        return packetId;
    }

    uint8_t frame[sizeof(MeshHdr) + 288];
    memcpy(frame, &hdr, sizeof(hdr));
    memcpy(frame + sizeof(hdr), cipher, payloadLen);
    if (!Radio.transmit(frame, sizeof(MeshHdr) + payloadLen)) {
        Serial.println("[admin] radio TX failed");
        return 0;
    }
    Serial.printf("[admin] sent id %08lx to !%08lx via rf\n",
                  (unsigned long)packetId, (unsigned long)nodeId);
    return packetId;
}

// "Heard on the radio recently" for the AUTO transport. Ten minutes is the same
// order as the node list's own idea of recent, and the question being asked is
// only "is RF plausible", not "is it guaranteed".
static bool adminRfRecent(uint32_t nodeId, void *) {
    const NodeEntry *n = Nodes.find(nodeId);
    if (!n || !n->lastHeardMs) return false;
    if (n->lastHeardViaMqtt) return false;
    return (uint32_t)(millis() - n->lastHeardMs) < 600000UL;
}

static bool adminMqttUp(void *) { return mqttBridgeConnected(); }

static uint32_t adminEpochNow(void *) {
    // Same floor the rest of this file uses to mean "the clock has actually been
    // set": pushing an unset clock to a remote is worse than refusing to.
    const time_t now = time(nullptr);
    return (now >= 1700000000) ? (uint32_t)now : 0;
}

static bool adminPeerAt(int index, uint32_t &nodeId, const char *&state, void *) {
    const AdminPeer *p = AdminPeerList.at(index);
    if (!p) return false;
    nodeId = p->nodeId;
    state = AdminPeers::stateName(p->state);
    return true;
}

static AdminClient::Hooks adminHooks() {
    AdminClient::Hooks h{};
    h.send = adminSendPacket;
    h.rfRecent = adminRfRecent;
    h.mqttUp = adminMqttUp;
    h.epochNow = adminEpochNow;
    h.peerAt = adminPeerAt;
    h.ctx = nullptr;
    return h;
}

// Opening and closing are the UI's business, but both UIs do it identically, so
// the rule about the gate lives here rather than twice over.
static bool adminOpenSession(uint32_t nodeId) {
    if (lic::active()) {
        // Opened only to carry the reason, so the operator reads why at once
        // instead of waiting out a timeout; every send is refused anyway.
        if (s_adminSession.isOpen() && s_adminSession.nodeId() == nodeId) return true;
        if (!s_adminSession.open(nodeId, adminHooks())) return false;
        // LINE_ERR: the device gutter prints the leading "! ".
        s_adminSession.notify(AdminClient::LINE_ERR,
                              TR("Remote admin unavailable in licensed mode (no PKI)"));
        return true;
    }
    if (!AdminPeerList.mayOpenTerminal(nodeId)) {
        Serial.printf("[admin] refusing !%08lx - not a listed peer\n",
                      (unsigned long)nodeId);
        return false;
    }
    return s_adminSession.open(nodeId, adminHooks());
}

// A 33/37 NAK closes the session from inside; the peer list is demoted here,
// because the session does not own it.
static void adminNoteUnauthorized(uint32_t nodeId) {
    AdminPeerList.deny(nodeId);
}
#endif  // HAS_ADMIN_TERMINAL

#if HAS_ADMIN_TERMINAL
// ── Terminal modal ───────────────────────────────────────────────────────────
// Transcript above, one input line below. The transcript is the session's ring
// buffer rendered; nothing is stored here that the session does not already own,
// so closing the window costs the screen and not the conversation -- the remote
// holds its session key for 300 s and reopening continues where this left off.
// Command history, walked with up/down. Small: this is a terminal for a handful
// of commands, not a shell.
static constexpr int kAdminHistory = 8;
static char s_adminHist[kAdminHistory][AdminClient::kLineLen] = {};
static int  s_adminHistCount = 0;
static int  s_adminHistPos = -1;    // -1 = editing a fresh line
static uint32_t s_adminRenderedRev = 0;

static lv_color_t adminLineColor(uint8_t kind) {
#if defined(DEVICE_TDECK_PRO)
    LV_UNUSED(kind);
    return lv_color_make(0, 0, 0);   // one-bit panel: everything is ink
#else
    switch (kind) {
        case AdminClient::LINE_ECHO: return lv_color_hex(0x8FB8FF);
        case AdminClient::LINE_INFO: return lv_color_hex(0xA7C7FF);
        case AdminClient::LINE_OK:   return lv_color_hex(0x8EF2B8);
        case AdminClient::LINE_ERR:  return lv_color_hex(0xFF9F9F);
        default:                     return lv_color_hex(0xFFFFFF);
    }
#endif
}

// The transcript's font and how many characters fit across it. Measured at
// open and held, because every line drawn needs the same answer and the panel
// does not change size while the modal is up.
static const lv_font_t *s_adminFont = &lv_font_montserrat_10;
static int              s_adminCols = 40;

// Average advance of the transcript's alphabet, rounded up.
//
// Rounded up on purpose: the count it feeds is a budget, and a budget that is a
// character too small costs one early break, while one that is a character too
// large overruns the panel -- which is the ragged edge this is here to stop.
static int adminAvgCharW(const lv_font_t *font) {
    static const char kSample[] = "abcdefghijklmnopqrstuvwxyz0123456789 .=_";
    lv_point_t sz = {0, 0};
    lv_text_get_size(&sz, kSample, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
    const int n = (int)(sizeof(kSample) - 1);
    if (n <= 0 || sz.x <= 0) return 6;
    return ((int)sz.x + n - 1) / n;
}

// The largest font that still leaves the terminal usably wide.
//
// Measured rather than chosen per board: this fleet runs from a 240 px panel to
// a 480 px one across eleven environments, and a table of device names is a
// table that goes stale the next time one is added. A terminal is judged on its
// column count -- below about 52 the session's `key = value` replies wrap on
// nearly every line -- so the rule is "biggest font that still clears 52", with
// montserrat_10 as the floor for the panels where nothing does.
static const lv_font_t *adminPickFont(int availW, int &colsOut) {
    static const lv_font_t *const kCandidates[] = {
        &lv_font_montserrat_14,
        &lv_font_montserrat_12,
        &lv_font_montserrat_10,
    };
    constexpr int kWantCols = 52;
    constexpr int kLast = (int)(sizeof(kCandidates) / sizeof(kCandidates[0])) - 1;

    for (int i = 0; i <= kLast; i++) {
        const int cw = adminAvgCharW(kCandidates[i]);
        const int cols = cw > 0 ? availW / cw : 0;
        if (cols >= kWantCols || i == kLast) {
            colsOut = cols > 8 ? cols : 8;
            return kCandidates[i];
        }
    }
    colsOut = 8;
    return &lv_font_montserrat_10;
}

// A two character gutter naming what kind of line this is.
//
// The colour panels answer this with adminLineColor and need nothing here. The
// Pro cannot: it is one bit, so echo, info, ok and error all resolve to the
// same black and the transcript arrives as an undifferentiated wall of text
// with no way to tell what you typed from what came back.
//
// Two characters because the echo already carries "> " from the session, so
// that is the width the rest have to match to line up under it. PLAIN gets
// blanks rather than a mark -- it is the body of whatever OK line came before
// it, and marking a continuation as its own thing is the confusion this is
// meant to remove.
static const char *adminKindGutter(uint8_t kind) {
#if defined(DEVICE_TDECK_PRO)
    switch (kind) {
        case AdminClient::LINE_ECHO: return "";     // already "> " in the text
        case AdminClient::LINE_INFO: return "- ";
        case AdminClient::LINE_OK:   return "+ ";
        case AdminClient::LINE_ERR:  return "! ";
        default:                     return "  ";
    }
#else
    LV_UNUSED(kind);
    return "";
#endif
}

// One transcript line, broken to fit and emitted as one label per visual row.
//
// Done here rather than left to LV_LABEL_LONG_WRAP because LVGL restarts a
// wrapped row flush left, which makes the tail of a long reply look like a new
// entry -- on a 40 column panel half the transcript reads as lines nobody sent.
// Continuations carry a two space hanging indent instead, so a wrapped line
// stays one line to the eye.
//
// Breaks at the last space in the window when there is one worth using, and
// mid-token when there is not: a 64 character public key has no spaces in it
// and still has to go somewhere. LONG_WRAP stays on each row as a backstop --
// the budget is an average-width estimate, so a row of unusually wide glyphs
// can still need LVGL to break it, and wrapping beats clipping when it does.
static void adminEmitLine(lv_obj_t *parent, const char *text, uint8_t kind) {
    const int cols = s_adminCols;
    constexpr int kIndent = 2;

    // Bound once so the arithmetic below never has to consider null.
    const char  *t   = text ? text : "";
    const size_t len = strlen(t);
    // Same width as the continuation indent by construction, so a wrapped line
    // lands exactly under the first character of the one it continues.
    const char  *gut = adminKindGutter(kind);
    const int    gutLen = (int)strlen(gut);
    size_t pos = 0;
    bool first = true;
    char buf[AdminClient::kLineLen + kIndent + 1];

    do {
        int budget = cols - (first ? gutLen : kIndent);
        if (budget < 4) budget = 4;

        size_t take = len - pos;
        if (take > (size_t)budget) {
            take = (size_t)budget;
            size_t brk = 0;
            for (size_t i = take; i > 0; i--) {
                if (t[pos + i - 1] == ' ') { brk = i; break; }
            }
            // Only if it leaves a reasonable amount on this row. A space in the
            // first few characters would break after one word and hand the rest
            // of the panel back empty.
            if (brk > (size_t)(budget / 3)) take = brk - 1;   // drop the space
        }

        int n = 0;
        if (first) { memcpy(buf, gut, (size_t)gutLen); n = gutLen; }
        else       { buf[0] = ' '; buf[1] = ' '; n = kIndent; }
        if (take > sizeof(buf) - 1 - (size_t)n) take = sizeof(buf) - 1 - (size_t)n;
        // Neither branch above can reach zero with budget >= 4, but the loop
        // only terminates because `take` advances `pos`, and that is too much
        // to rest on arithmetic nobody will re-check.
        if (take == 0 && pos < len) take = 1;
        memcpy(buf + n, t + pos, take);
        buf[n + take] = '\0';

        lv_obj_t *lbl = lv_label_create(parent);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_text_font(lbl, s_adminFont, 0);
        lv_obj_set_style_text_color(lbl, adminLineColor(kind), 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_label_set_text(lbl, buf);

        pos += take;
        while (pos < len && t[pos] == ' ') pos++;   // eat the break
        first = false;
    } while (pos < len);
}

#if defined(DEVICE_TDECK_PRO)
// Width of the scroll arrow column. Two thumb-sized targets on a 240 px panel:
// wide enough to hit without a stylus, narrow enough that the transcript keeps
// the columns the wrap budget was measured against.
static constexpr int kAdminArrowW = 26;

// Paged rather than nudged. E-paper spends most of a second on every refresh,
// so a scroll that moves a line at a time costs a full repaint per line; three
// quarters of a screen keeps a couple of lines of overlap for the eye to land
// on and asks the panel for one refresh to do it.
static void onAdminScrollArrow(lv_event_t *e) {
    if (!s_adminScroll || !lvObjValid(s_adminScroll)) return;
    const int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int page = lv_obj_get_height(s_adminScroll) * 3 / 4;
    if (page < 16) page = 16;
    // Through scrollListClamped, not lv_obj_scroll_by: the raw call is
    // unbounded and runs off the end of the transcript into blank space, which
    // on a panel that takes most of a second to repaint is a very slow way to
    // find out you were already at the bottom. Its sign convention is this
    // one -- positive dy reveals what is above, which is what Up means.
    scrollListClamped(s_adminScroll, (lv_coord_t)(dir * page));
    s_lastActivityMs = millis();
}
#endif

static void refreshAdminTranscript() {
    if (!s_adminScroll || !lvObjValid(s_adminScroll)) return;
    // Repainted whole rather than appended to: the session's buffer is a ring,
    // so once it wraps there is no "new lines only" that is still correct.
    // Forty labels is cheap next to getting the wrap wrong.
    if (s_adminSession.revision() == s_adminRenderedRev) return;
    s_adminRenderedRev = s_adminSession.revision();

    lv_obj_clean(s_adminScroll);
    for (int i = 0; i < s_adminSession.lineCount(); i++) {
        const AdminClient::Line *ln = s_adminSession.line(i);
        if (!ln) continue;
        adminEmitLine(s_adminScroll, ln->text, ln->kind);
    }
    lv_obj_scroll_to_y(s_adminScroll, LV_COORD_MAX, LV_ANIM_OFF);

    if (s_adminTitle && lvObjValid(s_adminTitle)) {
        const AdminClient::Transport t = s_adminSession.transport();
        // The name, falling back the way the rest of the UI does. A hex id is
        // nine characters of the title row spent on something the operator
        // already knows -- they picked the node to open it.
        const NodeEntry *n = Nodes.find(s_adminSession.nodeId());
        char who[24];
        if (n && n->longName[0])        snprintf(who, sizeof(who), "%s", n->longName);
        else if (n && n->shortName[0])  snprintf(who, sizeof(who), "%s", n->shortName);
        else snprintf(who, sizeof(who), "!%08lx", (unsigned long)s_adminSession.nodeId());

        lv_label_set_text_fmt(s_adminTitle, TR("%s  %s%s"), who,
                              t == AdminClient::TRANSPORT_RF ? "rf"
                                : t == AdminClient::TRANSPORT_MQTT ? "mqtt" : "auto",
                              s_adminSession.awaitingConfirm() ? TR("  CONFIRM?") : "");
    }
}

static void closeAdminTerminalModal() {
    if (lvObjValid(s_adminBackdrop)) {
        lv_obj_del(s_adminBackdrop);
    } else if (lvObjValid(s_adminModal)) {
        lv_obj_del(s_adminModal);
    }
    s_adminBackdrop = nullptr;
    s_adminModal = nullptr;
    s_adminScroll = nullptr;
    s_adminInput = nullptr;
    s_adminTitle = nullptr;
#if UI_TOUCH_ONLY_PROFILE
    s_adminKeyboard = nullptr;
#endif
    s_adminHistPos = -1;
    s_adminRenderedRev = 0;
    // The session is deliberately left open. The remote's key is good for five
    // minutes and reopening inside that window should not cost a round trip.
    s_lastActivityMs = millis();
}

static void adminSubmitCurrentLine() {
    if (!s_adminInput || !lvObjValid(s_adminInput)) return;
    const char *text = lv_textarea_get_text(s_adminInput);
    if (!text) return;

    if (text[0]) {
        // Newest first, de-duplicated against the immediately previous entry so
        // repeating a command does not fill the history with it.
        if (!s_adminHistCount || strcmp(s_adminHist[0], text) != 0) {
            for (int i = kAdminHistory - 1; i > 0; i--) {
                memcpy(s_adminHist[i], s_adminHist[i - 1], AdminClient::kLineLen);
            }
            snprintf(s_adminHist[0], AdminClient::kLineLen, "%s", text);
            if (s_adminHistCount < kAdminHistory) s_adminHistCount++;
        }
    }
    s_adminHistPos = -1;

    s_adminSession.submit(text, millis());
    lv_textarea_set_text(s_adminInput, "");
    refreshAdminTranscript();

    // `exit`, or an unauthorized NAK, closes the session from underneath us.
    if (!s_adminSession.isOpen()) closeAdminTerminalModal();
}

static void adminRecallHistory(int delta) {
    if (!s_adminInput || !lvObjValid(s_adminInput) || !s_adminHistCount) return;
    int pos = s_adminHistPos + delta;
    if (pos < -1) pos = -1;
    if (pos >= s_adminHistCount) pos = s_adminHistCount - 1;
    s_adminHistPos = pos;
    lv_textarea_set_text(s_adminInput, pos < 0 ? "" : s_adminHist[pos]);
}

#if UI_TOUCH_ONLY_PROFILE
static void onAdminKeyboardEvent(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY)  adminSubmitCurrentLine();
    if (code == LV_EVENT_CANCEL) closeAdminTerminalModal();
}
#endif

static void onAdminBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_adminBackdrop) return;
    closeAdminTerminalModal();
}

static void openAdminTerminalModal(uint32_t nodeId) {
    if (!s_rootScreen || s_adminModal) return;
    // The gate, applied here as well as at the row: the row is what a user sees,
    // this is what a stray call would have to get past.
    if (!adminOpenSession(nodeId)) return;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);

    s_adminBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_adminBackdrop, w, h);
    lv_obj_align(s_adminBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_adminBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_adminBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_adminBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_adminBackdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_adminBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_adminBackdrop, 0, 0);
    lv_obj_add_event_cb(s_adminBackdrop, onAdminBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_adminModal = lv_obj_create(s_adminBackdrop);
    lv_obj_set_size(s_adminModal, w - 8, h - 8);
    lv_obj_align(s_adminModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_adminModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_adminModal, LV_OBJ_FLAG_SCROLLABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_adminModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_adminModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_adminModal, lv_color_hex(0x0A0A18), 0);
    lv_obj_set_style_border_color(s_adminModal, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_set_style_bg_opa(s_adminModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_adminModal, 1, 0);
    lv_obj_set_style_pad_all(s_adminModal, 4, 0);
    lv_obj_set_style_pad_row(s_adminModal, 2, 0);
    lv_obj_set_flex_flow(s_adminModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_adminModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    s_adminTitle = lv_label_create(s_adminModal);
    lv_obj_set_width(s_adminTitle, lv_pct(100));
    // Always the small font, whatever the transcript ended up with: this is one
    // status row, and on the narrow panels every pixel it gives back is a line
    // of transcript.
    lv_obj_set_style_text_font(s_adminTitle, &lv_font_montserrat_10, 0);
    lv_label_set_long_mode(s_adminTitle, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_adminTitle, adminLineColor(AdminClient::LINE_INFO), 0);
    lv_label_set_text(s_adminTitle, "");

    // On the Pro the transcript shares its row with a pair of scroll arrows,
    // so it gets a row container to sit in; everywhere else it is the row.
    //
    // The arrows exist because this is the one board where the obvious keys are
    // already spoken for: j and k are letters the moment there is a text field
    // on screen, and the Pro's terminal always has one.
    lv_obj_t *scrollParent = s_adminModal;
#if defined(DEVICE_TDECK_PRO)
    lv_obj_t *body = lv_obj_create(s_adminModal);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_column(body, 2, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    scrollParent = body;
#endif

    s_adminScroll = lv_obj_create(scrollParent);
#if defined(DEVICE_TDECK_PRO)
    // In a row, flex_grow divides the width and the height is the row's.
    lv_obj_set_height(s_adminScroll, lv_pct(100));
#else
    lv_obj_set_width(s_adminScroll, lv_pct(100));
#endif
    lv_obj_set_flex_grow(s_adminScroll, 1);
    setupVScroll(s_adminScroll);
    lv_obj_set_scrollbar_mode(s_adminScroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(s_adminScroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_adminScroll, 0, 0);
    lv_obj_set_style_pad_all(s_adminScroll, 0, 0);
    lv_obj_set_style_pad_right(s_adminScroll, 2, 0);
    lv_obj_set_flex_flow(s_adminScroll, LV_FLEX_FLOW_COLUMN);
    // pad_all does not touch pad_row, and the default theme's row gap is set
    // for cards rather than for text -- left alone it put several blank pixels
    // between every line of the transcript, which on a 240 px panel is most of
    // a row given away per line. A terminal wants its lines touching.
    lv_obj_set_style_pad_row(s_adminScroll, 0, 0);
    lv_obj_set_style_text_line_space(s_adminScroll, 0, 0);

#if defined(DEVICE_TDECK_PRO)
    {
        lv_obj_t *arrows = lv_obj_create(body);
        lv_obj_set_width(arrows, kAdminArrowW);
        lv_obj_set_height(arrows, lv_pct(100));
        lv_obj_clear_flag(arrows, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(arrows, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(arrows, 0, 0);
        lv_obj_set_style_pad_all(arrows, 0, 0);
        lv_obj_set_style_pad_row(arrows, 2, 0);
        lv_obj_set_flex_flow(arrows, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(arrows, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        struct { const char *glyph; int dir; } kArrows[] = {
            { LV_SYMBOL_UP,   +1 },
            { LV_SYMBOL_DOWN, -1 },
        };
        for (size_t i = 0; i < sizeof(kArrows) / sizeof(kArrows[0]); i++) {
            lv_obj_t *b = lv_btn_create(arrows);
            lv_obj_remove_style_all(b);
            lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_width(b, lv_pct(100));
            lv_obj_set_flex_grow(b, 1);
            // Outlined, not filled: a filled button on a one-bit panel is a
            // black block, and the glyph inside it disappears.
            lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(b, 1, 0);
            lv_obj_set_style_border_color(b, lv_color_make(0, 0, 0), 0);
            lv_obj_set_style_radius(b, 0, 0);
            lv_obj_set_style_pad_all(b, 0, 0);
            lv_obj_add_event_cb(b, onAdminScrollArrow, LV_EVENT_CLICKED,
                                (void *)(intptr_t)kArrows[i].dir);

            lv_obj_t *g = lv_label_create(b);
            lv_obj_set_style_text_font(g, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(g, lv_color_make(0, 0, 0), 0);
            lv_label_set_text(g, kArrows[i].glyph);
            lv_obj_center(g);
        }
    }
#endif

    // Measured against the width the lines will actually be drawn in: the
    // modal's content box, less the scrollbar gutter reserved just above, less
    // the arrow column where there is one. Getting this wrong is not cosmetic
    // -- it is the budget every line is broken to.
    {
        int availW = (w - 8) - 2 * 4 - 2;
#if defined(DEVICE_TDECK_PRO)
        availW -= kAdminArrowW + 2;   // the column and the row gap before it
#endif
        s_adminFont = adminPickFont(availW, s_adminCols);
    }

#if defined(DEVICE_TDECK_PRO)
    // A seam between the transcript and the line being typed. Same rule idiom
    // as the sleep header's, turned on its side.
    //
    // Only on the Pro because only the Pro needs it: the colour panels tell the
    // two apart by ink -- the transcript is blue and green and red against a
    // dark card, the textarea is a lighter well -- and on a one-bit panel all
    // of that collapses to black on white, leaving the input as one more row of
    // text under the last reply.
    {
        lv_obj_t *seam = lv_obj_create(s_adminModal);
        lv_obj_set_width(seam, lv_pct(100));
        lv_obj_set_height(seam, 1);
        lv_obj_clear_flag(seam, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(seam, 0, 0);
        lv_obj_set_style_radius(seam, 0, 0);
        lv_obj_set_style_pad_all(seam, 0, 0);
        lv_obj_set_style_bg_color(seam, lv_color_make(0, 0, 0), 0);
        lv_obj_set_style_bg_opa(seam, LV_OPA_COVER, 0);
    }
#endif

    s_adminInput = lv_textarea_create(s_adminModal);
    lv_obj_set_width(s_adminInput, lv_pct(100));
    lv_textarea_set_one_line(s_adminInput, true);
    lv_textarea_set_max_length(s_adminInput, AdminClient::kLineLen - 1);
    lv_textarea_set_placeholder_text(s_adminInput, "help");
    // The same font the echo will be drawn in, so a typed line does not change
    // size when it lands in the transcript. The theme's textarea padding is
    // sized for a form field; here it was costing more height than the text.
    lv_obj_set_style_text_font(s_adminInput, s_adminFont, 0);
    lv_obj_set_style_pad_all(s_adminInput, 2, 0);

#if UI_TOUCH_ONLY_PROFILE
    // Not with a physical keyboard attached (the T-Display P4's expansion):
    // adminTerminalHandleKey() takes what is typed on it, and the transcript
    // gets the room.
    if (!keyboardAttached()) {
        s_adminKeyboard = lv_keyboard_create(s_adminModal);
        configureOnScreenKeyboard(s_adminKeyboard);
        lv_keyboard_set_textarea(s_adminKeyboard, s_adminInput);
        lv_obj_add_event_cb(s_adminKeyboard, onAdminKeyboardEvent, LV_EVENT_READY, nullptr);
        lv_obj_add_event_cb(s_adminKeyboard, onAdminKeyboardEvent, LV_EVENT_CANCEL, nullptr);
    }
#endif

    lv_obj_move_foreground(s_adminBackdrop);
    s_adminRenderedRev = 0;
    refreshAdminTranscript();
}

// Physical keys, for the boards that have them. Returns true when the key was
// consumed -- which is every key while this modal is up, because a terminal that
// let stray letters reach the chat screen underneath would be unusable.
static bool adminTerminalHandleKey(char k) {
    if (!s_adminModal || !s_adminInput || !lvObjValid(s_adminInput)) return false;

    if (k == KEY_ENTER) { adminSubmitCurrentLine(); return true; }
    if (isModalCloseKey(k) && !lv_textarea_get_text(s_adminInput)[0]) {
        // Close on an empty line only: with text in it the close key is a
        // backspace, which is what someone mid-command means by it.
        closeAdminTerminalModal();
        return true;
    }
    if (isBackspaceKey(k)) { lv_textarea_delete_char(s_adminInput); return true; }
    if (textFieldCaretKey(s_adminInput, k)) return true;
    if (k == KEY_SCROLL_UP)  { adminRecallHistory(+1); return true; }
    if (k == KEY_SCROLL_DN)  { adminRecallHistory(-1); return true; }
    if (k == KEY_PAGE_UP)    { scrollListClamped(s_adminScroll, 24); return true; }
    if (k == KEY_PAGE_DN)    { scrollListClamped(s_adminScroll, -24); return true; }
    if ((uint8_t)k >= 0x20 && (uint8_t)k < 0x7F) {
        lv_textarea_add_char(s_adminInput, (uint32_t)(uint8_t)k);
        return true;
    }
    return true;   // swallow everything else
}
#endif  // HAS_ADMIN_TERMINAL

#if HAS_ADMIN_TERMINAL
// ── Boot probe of favourites ─────────────────────────────────────────────────
// Once per boot, ask each favourite node whether it will take administration
// from us, so the terminal is offered on the ones that will without anyone
// having to go and find out by hand.
//
// Favourites rather than the whole node table, and that is the whole safety
// argument: a favourite is a short, deliberate, user-curated list. Probing
// everything we have ever heard would put an admin packet in front of every
// stranger on the mesh once per reboot, which is traffic they never asked for
// and a fair description of scanning.
//
// Sequential, never concurrent: one probe outstanding at a time, spaced, so a
// device with a dozen favourites does not key up twelve times in a row on a
// mesh that has other users. It runs on its own state rather than through
// Session, so it cannot clobber a transcript the user is reading -- and it
// stands aside entirely while a terminal is open.
namespace {

constexpr uint32_t kAdminProbeStartDelayMs = 45000;   // let the radio and node table settle
constexpr uint32_t kAdminProbeGapMs        = 8000;    // between one probe and the next
constexpr uint32_t kAdminProbeTimeoutMs    = 30000;   // matches the session's

struct AdminBootProbe {
    bool     done;          // the sweep has finished for this boot
    bool     started;
    int      rank;          // where the walk has reached in the node table
    uint32_t nextAtMs;
    uint32_t pendingId;     // packet id outstanding, 0 for none
    uint32_t pendingNode;
    uint32_t sentAtMs;
    uint8_t  probed;
    uint8_t  confirmed;
};

AdminBootProbe s_adminProbe{};

// The rescan button's cooldown. Separate from the sweep's own timers: those
// pace the packets inside one sweep, this paces how often a sweep may be
// started. `used` rather than a zero sentinel so the first press after boot is
// judged on its own merits and not on how close millis() still is to zero.
constexpr uint32_t kAdminRescanCooldownMs = 30000;
uint32_t s_adminRescanAtMs = 0;
bool     s_adminRescanUsed = false;

}  // namespace

// Tells an open session what a probe just found out about its own peer.
//
// The sweep and the terminal can be pointed at the same node at the same time,
// and the sweep is the one that gets the answer: the probe's reply is matched
// by packet id, so it never reaches the session's own handler. Without this an
// operator mid-session would learn that their grant had been revoked from the
// next write failing, several commands later.
//
// Silent for any other node -- a sweep across a dozen favourites has no
// business narrating itself into someone's terminal.
static void adminProbeTellSession(uint32_t nodeId, bool authorised) {
    if (!s_adminSession.isOpen() || s_adminSession.nodeId() != nodeId) return;
    if (authorised) {
        s_adminSession.notify(AdminClient::LINE_INFO,
                              "re-verified: this node still accepts us");
    } else {
        s_adminSession.notify(AdminClient::LINE_ERR,
                              "this node no longer accepts administration from us");
    }
    if (s_adminModal) refreshAdminTranscript();
}

// Called from the RX arms. Returns true when the packet belonged to the probe,
// so the session is not also handed a reply that was never its request.
static bool adminProbeOnReply(uint32_t fromNode, uint32_t requestId, const uint8_t *payload,
                              size_t len, AdminClient::Transport via) {
    if (!s_adminProbe.pendingId || s_adminProbe.pendingId != requestId) return false;
    if (fromNode != s_adminProbe.pendingNode) return false;

    AdminProto::Response r;
    const bool authorised = decodeResponse(payload, len, r) && r.hasPasskey;
    if (authorised) {
        // A session passkey is the proof: an unauthorized sender never gets one.
        AdminPeerList.add(s_adminProbe.pendingNode);   // no-op when already listed
        AdminPeerList.confirm(s_adminProbe.pendingNode,
                              via == AdminClient::TRANSPORT_MQTT ? ADMIN_VIA_MQTT : ADMIN_VIA_RF,
                              adminEpochNow(nullptr));
        s_adminProbe.confirmed++;
    }
    adminProbeTellSession(s_adminProbe.pendingNode, authorised);
    s_adminProbe.pendingId = 0;
    s_adminProbe.pendingNode = 0;
    return true;
}

static bool adminProbeOnRouting(uint32_t requestId, uint32_t errorReason) {
    if (!s_adminProbe.pendingId || s_adminProbe.pendingId != requestId) return false;
    if (AdminClient::errorMeansUnauthorized(errorReason)) {
        // Recorded rather than ignored: "this node refuses us" is worth knowing
        // and worth showing on the peers screen, and it stops the terminal being
        // offered on a node that will only ever say no.
        AdminPeerList.add(s_adminProbe.pendingNode);
        AdminPeerList.deny(s_adminProbe.pendingNode);
        adminProbeTellSession(s_adminProbe.pendingNode, false);
    }
    // Every other outcome -- an ack, a no-route, a duty-cycle refusal -- leaves
    // the peer exactly as it was. Only 33/37 mean anything about authorization.
    s_adminProbe.pendingId = 0;
    s_adminProbe.pendingNode = 0;
    return true;
}

static void serviceAdminBootProbe(uint32_t nowMs) {
    if (s_adminProbe.done || !s_radioReady) return;
    if (lic::active()) {
        // PKI-only, so nothing to ask while licensed.
        s_adminProbe.done = true;
        Serial.println("[admin-probe] skipped - licensed mode (no PKI)");
        return;
    }

    // Not while one of the operator's own commands is in flight: that is the
    // case where a probe would be a second admin exchange competing with the
    // first, and the operator's command is what the airtime is for.
    //
    // busy(), not isOpen(). A session deliberately outlives its window -- the
    // remote holds its key for 300 s and reopening inside that window should
    // not cost a round trip -- so isOpen() stays true long after the terminal
    // has been closed, and gating on it meant the first terminal of a boot
    // stopped the sweep for the rest of that boot. Between commands there is
    // nothing to collide with.
    if (s_adminSession.busy()) return;

    if (!s_adminProbe.started) {
        s_adminProbe.started = true;
        s_adminProbe.nextAtMs = nowMs + kAdminProbeStartDelayMs;
        return;
    }

    if (s_adminProbe.pendingId) {
        if ((uint32_t)(nowMs - s_adminProbe.sentAtMs) < kAdminProbeTimeoutMs) return;
        // Silence is not a refusal -- out of range, asleep, or a broker that is
        // down all look like this -- so the peer is left exactly as it was.
        Serial.printf("[admin-probe] !%08lx did not answer\n",
                      (unsigned long)s_adminProbe.pendingNode);
        s_adminProbe.pendingId = 0;
        s_adminProbe.pendingNode = 0;
        s_adminProbe.nextAtMs = nowMs + kAdminProbeGapMs;
        return;
    }

    if ((int32_t)(nowMs - s_adminProbe.nextAtMs) < 0) return;

    // Next favourite with a public key. No key means PKI is impossible, which
    // means the answer is already known without asking.
    while (s_adminProbe.rank < Nodes.count()) {
        NodeEntry *n = Nodes.getByRank(s_adminProbe.rank);
        s_adminProbe.rank++;
        if (!n || !n->favorite || !n->hasPubKey || !n->nodeId) continue;

        uint8_t msg[64];
        const size_t len = AdminProto::encodeGetRequest(
            msg, sizeof(msg), AdminProto::GET_DEVICE_METADATA_REQUEST, 1);
        const uint32_t id = adminSendPacket(n->nodeId,
                                            adminRfRecent(n->nodeId, nullptr)
                                                ? AdminClient::TRANSPORT_RF
                                                : AdminClient::TRANSPORT_MQTT,
                                            msg, len, nullptr);
        if (!id) {
            // Could not send -- no route today. Try the next one rather than
            // stalling the sweep on it.
            s_adminProbe.nextAtMs = nowMs + kAdminProbeGapMs;
            return;
        }
        s_adminProbe.pendingId = id;
        s_adminProbe.pendingNode = n->nodeId;
        s_adminProbe.sentAtMs = nowMs;
        s_adminProbe.probed++;
        Serial.printf("[admin-probe] asking !%08lx\n", (unsigned long)n->nodeId);
        return;
    }

    s_adminProbe.done = true;
    Serial.printf("[admin-probe] sweep done: %u favourite%s asked, %u confirmed\n",
                  (unsigned)s_adminProbe.probed,
                  s_adminProbe.probed == 1 ? "" : "s",
                  (unsigned)s_adminProbe.confirmed);
}

// ── Web terminal accessors ───────────────────────────────────────────────────
// Declared in web_config.h. The web terminal drives the same session the device
// does; nothing here keeps state of its own.
bool webCfgAdminSessionOpen() { return s_adminSession.isOpen(); }
uint32_t webCfgAdminNodeId()  { return s_adminSession.nodeId(); }
uint32_t webCfgAdminRevision(){ return s_adminSession.revision(); }
bool webCfgAdminBusy()        { return s_adminSession.busy(); }
bool webCfgAdminAwaitingConfirm() { return s_adminSession.awaitingConfirm(); }
int  webCfgAdminLineCount()   { return s_adminSession.lineCount(); }

const char *webCfgAdminLine(int i, uint8_t &kind) {
    const AdminClient::Line *l = s_adminSession.line(i);
    if (!l) { kind = 0; return nullptr; }
    kind = l->kind;
    return l->text;
}

bool webCfgAdminOpen(uint32_t nodeId) {
    // Already on this node: leave it alone. Reopening would clear the transcript
    // the operator is reading and, worse, look like a fresh session while the
    // remote still holds the old one.
    if (s_adminSession.isOpen() && s_adminSession.nodeId() == nodeId) return true;
    return adminOpenSession(nodeId);
}

void webCfgAdminSubmit(const char *line) {
    if (!line || !s_adminSession.isOpen()) return;
    s_adminSession.submit(line, millis());
    // The device modal, if it is up, is looking at the same buffer.
    if (s_adminModal) refreshAdminTranscript();
}

bool webCfgAdminVerify(uint32_t nodeId) {
    // Through the probe, not the session. The session refuses to open on a peer
    // that is not already CONFIRMED -- which is every peer worth verifying, so
    // routing Verify through it made the button useless on exactly the rows that
    // needed it. The probe has no such gate, because it is the thing that
    // decides whether the gate opens.
    if (s_adminProbe.pendingId) return false;   // one outstanding at a time
    NodeEntry *n = Nodes.find(nodeId);
    if (!n || !n->hasPubKey) return false;      // PKI is the only path in

    uint8_t msg[64];
    const size_t len = AdminProto::encodeGetRequest(
        msg, sizeof(msg), AdminProto::GET_DEVICE_METADATA_REQUEST, 1);
    const uint32_t id = adminSendPacket(nodeId,
                                        adminRfRecent(nodeId, nullptr)
                                            ? AdminClient::TRANSPORT_RF
                                            : AdminClient::TRANSPORT_MQTT,
                                        msg, len, nullptr);
    if (!id) return false;

    // Handed to the same state the boot sweep uses, so the reply and the NAK are
    // read by one piece of code rather than two that could disagree.
    s_adminProbe.pendingId = id;
    s_adminProbe.pendingNode = nodeId;
    s_adminProbe.sentAtMs = millis();
    Serial.printf("[admin-probe] verify asked !%08lx\n", (unsigned long)nodeId);
    return true;
}

int webCfgAdminRescanFavorites(uint32_t &waitSecs) {
    waitSecs = 0;

    // No session gate. A session outlives the window it was opened in, so
    // "is a session open" was answering a question nobody asked -- it refused
    // the button for five minutes after a terminal had been closed, and there
    // is no terminal on screen to be interrupted. The sweep itself stands aside
    // for a command actually in flight; that is where the conflict is.

    // Thirty seconds between sweeps. The limit is on the button, not on the
    // sweep itself -- a sweep is a packet every 8 s for as long as the
    // favourites last, and letting someone restart it from the top on every
    // click is how one browser tab turns into a node that will not stop
    // transmitting. The cooldown is measured from the last *accepted* press, so
    // holding the button down does not extend it.
    const uint32_t nowMs = millis();
    if (s_adminRescanUsed) {
        const uint32_t since = nowMs - s_adminRescanAtMs;
        if (since < kAdminRescanCooldownMs) {
            waitSecs = (kAdminRescanCooldownMs - since + 999) / 1000;
            return ADMIN_RESCAN_COOLING;
        }
    }

    // Is there anything to ask? A favourite with no public key can never answer
    // a PKI admin request, so a sweep over none of them would report "started"
    // and then do nothing visible for the rest of the minute.
    int candidates = 0;
    for (int i = 0; i < Nodes.count(); i++) {
        const NodeEntry *n = Nodes.getByRank(i);
        if (n && n->favorite && n->hasPubKey && n->nodeId) candidates++;
    }
    if (candidates == 0) return ADMIN_RESCAN_NOTHING;

    s_adminRescanUsed = true;
    s_adminRescanAtMs = nowMs;

    // Rewind the sweep. `started` stays true and nextAtMs is now, so this one
    // begins on the next loop pass rather than after the 45 s settling delay the
    // boot sweep wants -- the radio and the node table are long since settled by
    // the time anyone is clicking buttons in a browser.
    //
    // pendingId is deliberately left alone. A probe already in flight belongs to
    // a request that has not been answered yet; the service loop waits it out
    // and then walks from the top, which costs one extra spacing gap and keeps
    // the invariant that only one admin packet is ever outstanding.
    s_adminProbe.done      = false;
    s_adminProbe.started   = true;
    s_adminProbe.rank      = 0;
    s_adminProbe.nextAtMs  = nowMs;
    s_adminProbe.probed    = 0;
    s_adminProbe.confirmed = 0;

    Serial.printf("[admin-probe] rescan requested: %d favourite%s to ask\n",
                  candidates, candidates == 1 ? "" : "s");
    return candidates;
}
#endif  // HAS_ADMIN_TERMINAL

// Check the Routing.ack_proof on an ack or nak that answered one of our DMs, and
// log the verdict. The caller has matched it to a DM sent to pkt.hdr.from, which
// is what binds the proof to the node we addressed (upstream #11932).
static void noteAckProof(const MeshPacket &pkt) {
    uint8_t claimed[ACK_PROOF_BYTES];
    uint8_t routing[sizeof(pkt.payload)];
    size_t routingLen = 0;
    const AckProofFind found = ackProofExtract(pkt.payload, pkt.payloadLen, claimed,
                                               routing, &routingLen);
    if (found == ACK_PROOF_ABSENT) return;
    if (found == ACK_PROOF_MALFORMED) {
        Serial.printf("[ackproof] malformed proof from !%08lx for %08lx\n",
                      (unsigned long)pkt.hdr.from, (unsigned long)pkt.requestId);
        return;
    }
    const NodeEntry *peer = Nodes.find(pkt.hdr.from);
    uint8_t key[32];
    if (!peer || !peer->hasPubKey || !pkiSharedKey(peer->pubKey, key)) {
        Serial.printf("[ackproof] proof from !%08lx, but no key to check it\n",
                      (unsigned long)pkt.hdr.from);
        return;
    }
    uint8_t expected[ACK_PROOF_BYTES];
    ackProofCompute(key, pkt.hdr.from, pkt.hdr.to, pkt.requestId, routing, routingLen, expected);
    memset(key, 0, sizeof(key));
    const bool valid = memcmp(claimed, expected, ACK_PROOF_BYTES) == 0;
    Serial.printf("[ackproof] %s proof from !%08lx for %08lx\n", valid ? "valid" : "INVALID",
                  (unsigned long)pkt.hdr.from, (unsigned long)pkt.requestId);
}

static bool sendRoutingResult(uint32_t toNodeId, uint32_t requestId, uint32_t errorReason) {
    if (!Radio.isReady()) return false;
    if (toNodeId == 0 || toNodeId == 0xFFFFFFFF || requestId == 0) return false;
    if (s_myNodeId == 0) deriveNodeId();
    if (s_myNodeId == 0) return false;

    // Prove the receipt when we share a key with the sender (Meshtastic 2.8.1
    // Routing.ack_proof): their client can then show the message as delivered to
    // us rather than to whoever sent an ack with our number on it. Upstream
    // proves every ack to a keyed peer, PKI-encrypted original or not.
    uint8_t proofKey[32];
    bool haveProofKey = false;
    if (const NodeEntry *peer = Nodes.find(toNodeId)) {
        haveProofKey = peer->hasPubKey && pkiSharedKey(peer->pubKey, proofKey);
    }

    uint8_t proto[64];
    size_t protoLen = encodeRouting(requestId, s_myNodeId, errorReason, proto, sizeof(proto),
                                    s_cfg.okToMqtt ? 0x01 : 0,
                                    haveProofKey ? proofKey : nullptr, toNodeId);
    memset(proofKey, 0, sizeof(proofKey));
    if (protoLen == 0) return false;

    const ChannelKey &ck = CHANNEL_KEYS[0];  // ROUTING replies on primary channel.
    uint8_t cipher[96 + MESH_AEAD_OVERHEAD];
    uint32_t packetId = nextMeshPacketId();
    const size_t cipherLen = encryptChannelPayload(ck, packetId, s_myNodeId, toNodeId,
                                                   proto, protoLen, cipher, sizeof(cipher));
    if (cipherLen == 0) {
        return false;
    }

    uint8_t frame[sizeof(MeshHdr) + sizeof(cipher)];
    MeshHdr hdr = {};
    hdr.to = toNodeId;
    hdr.from = s_myNodeId;
    hdr.id = packetId;
    hdr.channel = ck.hash;
    hdr.flags   = meshOriginHopFlagsForChannel(0);
    hdr.relay_node = (uint8_t)(s_myNodeId & 0xFF);

    memcpy(frame, &hdr, sizeof(hdr));
    memcpy(frame + sizeof(hdr), cipher, cipherLen);
    return Radio.transmit(frame, sizeof(hdr) + cipherLen);
}

// Answer a traceroute aimed at us. Stock Meshtastic's destination node echoes
// the RouteDiscovery it received back to the requester with request_id set. We
// used to send only the routing ACK, so anyone tracing this node — including
// another board running this firmware — waited for a reply that never came.
static bool sendTracerouteReply(uint32_t toNodeId, uint32_t requestId,
                                const uint8_t *routePayload, size_t routePayloadLen,
                                int chanIdx, float rxSnr) {
    if (!Radio.isReady()) return false;
    if (toNodeId == 0 || toNodeId == 0xFFFFFFFF || requestId == 0) return false;
    if (s_myNodeId == 0) deriveNodeId();
    if (s_myNodeId == 0 || toNodeId == s_myNodeId) return false;
    if (chanIdx < 0 || chanIdx >= MESH_CHANNELS) chanIdx = 0;

    // Sized for a full 7-hop RouteDiscovery with SNR arrays and change to spare;
    // an echo that won't fit is dropped rather than truncated into nonsense.
    uint8_t proto[208];
    size_t protoLen = encodeTracerouteReply(proto, sizeof(proto),
                                            routePayload, routePayloadLen,
                                            requestId, s_myNodeId, rxSnr,
                                            s_cfg.okToMqtt ? 0x01 : 0);
    if (protoLen == 0) return false;

    // Reply on the channel the request arrived on: that is the one the sender is
    // listening to, and it need not be the primary.
    const ChannelKey &ck = CHANNEL_KEYS[chanIdx];
    uint8_t cipher[sizeof(proto) + MESH_AEAD_OVERHEAD];
    uint32_t packetId = nextMeshPacketId();
    const size_t cipherLen = encryptChannelPayload(ck, packetId, s_myNodeId, toNodeId,
                                                   proto, protoLen, cipher, sizeof(cipher));
    if (cipherLen == 0) {
        return false;
    }

    uint8_t frame[sizeof(MeshHdr) + sizeof(cipher)];
    MeshHdr hdr = {};
    hdr.to = toNodeId;
    hdr.from = s_myNodeId;
    hdr.id = packetId;
    hdr.channel = ck.hash;
    hdr.flags   = meshOriginHopFlagsForChannel(chanIdx);
    hdr.relay_node = (uint8_t)(s_myNodeId & 0xFF);

    memcpy(frame, &hdr, sizeof(hdr));
    memcpy(frame + sizeof(hdr), cipher, cipherLen);
    const bool ok = Radio.transmit(frame, sizeof(hdr) + cipherLen);

    // Logged like the outbound request, so the Live feed shows both halves of a
    // traceroute and answering one can be confirmed without a second device.
    char timePrefix[12];
    char dst[16];
    char line[72];
    liveBuildPrefix(timePrefix, sizeof(timePrefix));
    liveNodeLabel(toNodeId, dst, sizeof(dst), true);
    snprintf(line, sizeof(line), "T TRC U %s %08lX %s",
             dst, (unsigned long)requestId, ok ? "RPLY" : "ER");
    liveFeedAddPrefixed(timePrefix, line, ok ? TFT_DARKGREY : TFT_RED, 0, false);

    return ok;
}


// ── Managed flood rebroadcasting ──────────────────────────────────────────────
// Which roles relay, how long a relay waits, and what hearing another node's
// copy does to it are relay_policy.h's, host-tested. TRACKER needed nothing
// there: a tracker relays exactly like a CLIENT upstream, and reports its own
// position on top of that. Anyone wanting a node that only reports itself wants
// CLIENT_MUTE, not TRACKER.

// The packet is from, or (unicast) to, a node we favorited: the traffic a
// CLIENT_BASE relays like ROUTER_LATE. Upstream's isFromOrToFavoritedNode().
static bool relayFavoriteParty(const MeshHdr &hdr) {
    if (Nodes.isFavorite(hdr.from)) return true;
    return hdr.to != 0xFFFFFFFF && Nodes.isFavorite(hdr.to);
}

// One contention slot for the radio settings in use. Recomputed per relay
// rather than cached: it is a few float operations, and a cache would be one
// more thing a preset change or a preset scan's retune has to remember.
static uint32_t relaySlotMs() {
    const uint32_t slot = relay::slotTimeMsec(s_cfg.loraSf, s_cfg.loraBw,
                                              strcmp(s_cfg.region, "LORA_24") == 0);
    return slot ? slot : 1;
}

// A PKI direct message between two other nodes, at least one of whom we know.
// LOCAL_ONLY and KNOWN_ONLY relay these although they cannot read them (upstream
// #11898): DMs, remote administration and key verification all travel this way,
// and a relay that drops them black-holes them for the two nodes either side.
// An unreadable broadcast, or a unicast between two strangers, is still dropped
// -- ignoring foreign meshes is what those modes are for.
static bool isPkiUnicastWithKnownParty(const MeshPacket &pkt) {
    if (pkt.decrypted || pkt.hdr.channel != 0 || pkt.hdr.to == 0xFFFFFFFF) return false;
    return Nodes.find(pkt.hdr.from) != nullptr || Nodes.find(pkt.hdr.to) != nullptr;
}

// Apply the configured rebroadcastMode filter to a received packet.
static bool rebroadcastModeAllows(const MeshPacket &pkt) {
    switch (s_cfg.rebroadcastMode) {
        case 2: // LOCAL_ONLY — only packets we decrypted on a known channel
            return (pkt.decrypted && pkt.chanIdx >= 0) || isPkiUnicastWithKnownParty(pkt);
        case 3: // KNOWN_ONLY — only from nodes already in our DB
            return Nodes.find(pkt.hdr.from) != nullptr || isPkiUnicastWithKnownParty(pkt);
        case 4: // CORE_PORTNUMS_ONLY — only decoded core portnums
            // A packet we cannot read is relayed as it is (upstream #11844): the
            // portnum filter cannot apply to a payload nobody here can see, and
            // dropping it broke remote admin through ROUTER-role nodes, whose
            // default this mode is.
            if (!pkt.decrypted) return true;
            // Upstream Router.cpp's list, exactly (unchanged from 2.8.0 to
            // 2.8.1). This is about what we relay for the rest of the mesh, not
            // what we handle ourselves: a node in this mode that drops key
            // verification or admin cuts off whoever it is the only path for,
            // and one that relays neighbor info or beacons spends airtime that
            // every stock node in the same mode saves.
            switch (pkt.portnum) {
                case TEXT_MESSAGE_APP: case TEXT_MESSAGE_COMPRESSED_APP:
                case POSITION_APP: case NODEINFO_APP: case ROUTING_APP:
                case TELEMETRY_APP: case ADMIN_APP: case ALERT_APP:
                case KEY_VERIFICATION_APP: case WAYPOINT_APP:
                case STORE_FORWARD_APP: case TRACEROUTE_APP:
                case STORE_FORWARD_PLUSPLUS_APP:
                    return true;
                default: return false;
            }
        default: // ALL / ALL_SKIP_DECODING — relay raw regardless of decode
            return true;
    }
}

// Deferred rebroadcast queue. A relay waits out an SNR-weighted contention
// window (relay::relayDelayMsec(), upstream's) before re-transmitting, and is
// cancelled or moved to the late window if another node's copy is heard first
// (relayHeardDuplicate()). This wait MUST NOT be a blocking delay() on the main
// loop — that starves lv_timer_handler() and makes the whole UI sluggish.
// Instead we stash the framed packet with a send-after deadline and transmit it
// from servicePendingRebroadcast() once the window elapses.
//
// The windows are long -- on LongFast, 0.7 s to 12 s -- so the queue holds more
// at once than the 20-150 ms jitter it replaced did. Sixteen is upstream's TX
// queue depth; the Cardputer, with no PSRAM and the DRAM already tight, keeps
// eight. A full queue drops the new relay: relaying is discretionary.
struct PendingRebroadcast {
    uint8_t  frame[sizeof(MeshHdr) + sizeof(((MeshPacket *)nullptr)->rawCipher)];
    size_t   len;
    uint32_t sendAtMs;
    uint32_t from, id;   // the packet's identity, to match a copy heard later
    float    snr;        // as we heard it: sizes the late window
    bool     late;       // already moved to the late window once
    bool     active;
};
#if defined(DEVICE_CARDPUTER_LORA_HAT)
static constexpr uint8_t kMaxPendingRebroadcast = 8;
#else
static constexpr uint8_t kMaxPendingRebroadcast = 16;
#endif
static PendingRebroadcast s_pendingRebroadcast[kMaxPendingRebroadcast];

static PendingRebroadcast *allocPendingRebroadcast() {
    for (uint8_t i = 0; i < kMaxPendingRebroadcast; i++)
        if (!s_pendingRebroadcast[i].active) return &s_pendingRebroadcast[i];
    return nullptr;
}

// Empty the queue. Called on both edges of every temporary retune (a preset
// scan, a MeshBeacon send), because a
// queued frame is a packet already framed for the preset it was heard on: sent
// after the radio moved, it lands on a mesh that never carried it, under a
// channel hash computed for somewhere else. Up to 150 ms of jitter is exactly
// the window a retune fits inside. Dropping is the honest outcome — a relay is
// discretionary, and the mesh it belonged to has other nodes in it.
static void dropPendingRebroadcasts() {
    for (uint8_t i = 0; i < kMaxPendingRebroadcast; i++) s_pendingRebroadcast[i].active = false;
}

// Licensed mode: peers whose NodeInfo mismatched but that have no node entry
// (none is created for them). RAM only, round-robin; filled only while
// lic::active(), so licPeerStatus() equals Nodes.licenseStatus() otherwise.
static constexpr uint8_t kLicUnlicensedMax = 32;
static uint32_t s_licUnlicensedIds[kLicUnlicensedMax] = {};
static uint8_t s_licUnlicensedNext = 0;

static int licUnlicensedIndex(uint32_t nodeId) {
    if (nodeId == 0 || nodeId == 0xFFFFFFFF) return -1;
    for (uint8_t i = 0; i < kLicUnlicensedMax; i++)
        if (s_licUnlicensedIds[i] == nodeId) return i;
    return -1;
}

static lic::PeerStatus licPeerStatus(uint32_t nodeId) {
    const lic::PeerStatus s = Nodes.licenseStatus(nodeId);
    if (s == lic::PEER_UNKNOWN && licUnlicensedIndex(nodeId) >= 0) return lic::PEER_UNLICENSED;
    return s;
}

// Record a peer's is_licensed from a NodeInfo: on its entry when it has one,
// otherwise (unlicensed only) in the RAM set. Never creates an entry.
static void licRecordPeerLicensed(uint32_t nodeId, bool isLicensed) {
    if (nodeId == 0 || nodeId == 0xFFFFFFFF) return;
    const int idx = licUnlicensedIndex(nodeId);
    if (idx >= 0) s_licUnlicensedIds[idx] = 0;
    if (NodeEntry *e = Nodes.find(nodeId)) {
        e->isLicensed = isLicensed;
        e->licensedKnown = true;
    } else if (!isLicensed) {
        s_licUnlicensedIds[s_licUnlicensedNext] = nodeId;
        s_licUnlicensedNext = (uint8_t)((s_licUnlicensedNext + 1) % kLicUnlicensedMax);
    }
}

// Licensed: a NodeInfo whose is_licensed differs from ours. The peer is marked
// unlicensed; nothing is stored, replied to, acked or relayed.
static void licHandleNodeInfoMismatch(const MeshPacket &pkt, bool peerLicensed) {
    s_licNodeInfoMismatch++;
    licRecordPeerLicensed(pkt.hdr.from, peerLicensed);
    debugLogMessages("[lic] NodeInfo mismatch from=%08lx (is_licensed=%d)\n",
                     (unsigned long)pkt.hdr.from, (int)peerLicensed);
    const int chanIdx = (pkt.chanIdx >= 0 && pkt.chanIdx < MESH_CHANNELS) ? pkt.chanIdx : 0;
    appendLiveRxSummary(pkt, chanIdx, "N");
}

// Re-transmit a freshly received packet verbatim (hop limit decremented) so it
// propagates across the mesh. Caller guarantees the packet is new (passed
// isDuplicate) and not from us. The original on-air payload is relayed as-is
// from pkt.rawCipher — no re-encryption.
static void maybeRebroadcastPacket(const MeshPacket &pkt) {
    if (!Radio.isReady() || s_myNodeId == 0) return;
    if (!relay::roleRebroadcasts(s_cfg.deviceRole)) return;

    uint8_t hopLimit = pkt.hdr.flags & 0x07;
    if (hopLimit == 0) return;               // out of hops
    if (pkt.hdr.to == s_myNodeId) return;    // we're the endpoint; broadcasts/others relay
    if (pkt.rawLen == 0) return;             // no stored on-air payload to relay
    if (!rebroadcastModeAllows(pkt)) return;
    // Licensed: never relay what we could not decode, nor traffic to or from a
    // node known to be unlicensed. Always true when not licensed.
    if (!lic::allowRelay(lic::active(), pkt.decrypted,
                         licPeerStatus(pkt.hdr.from),
                         licPeerStatus(pkt.hdr.to))) return;

    // Meshtastic 2.8 classifies hop_start < hop_limit as INVALID and drops the
    // packet before it is even decrypted, so relaying one spends airtime on a
    // frame every 2.8 node in range refuses. This also catches hop_start == 0,
    // i.e. pre-2.3.0 firmware that never populated the field — those frames are
    // dropped by 2.8 too, so carrying them only helps inside a mesh with no 2.8
    // node in it. Matching upstream is the better trade.
    const uint8_t hopStart = (uint8_t)((pkt.hdr.flags >> 5) & 0x07);
    if (hopStart < hopLimit) {
        debugLogMessages("[fwd] drop (hop_start %u < hop_limit %u) from=%08lx id=%08lx\n",
                         hopStart, hopLimit,
                         (unsigned long)pkt.hdr.from, (unsigned long)pkt.hdr.id);
        return;
    }

    // A unicast routed through a chosen relay names it in next_hop (the low byte
    // of its node number). As upstream's NextHopRouter, relay only one that
    // names nobody or names us: the sender picked another path, and a node that
    // floods it anyway spends airtime and makes duplicates for nothing.
    const uint8_t ourRelayByte = (uint8_t)(s_myNodeId & 0xFF);
    if (pkt.hdr.next_hop != 0 && pkt.hdr.next_hop != ourRelayByte) {
        debugLogMessages("[fwd] skip (next_hop %02x is not us) from=%08lx id=%08lx\n",
                         pkt.hdr.next_hop,
                         (unsigned long)pkt.hdr.from, (unsigned long)pkt.hdr.id);
        return;
    }

    // A router-type node relaying for a favorited router-type node does not
    // spend a hop (upstream's shouldDecrementHopLimit()). Only CLIENT_BASE can
    // be that here, and only when the relay byte names one node unambiguously.
    relay::PrevRelay prev = {};
    if (relay::isRouterRole(s_cfg.deviceRole)) {
        const NodeEntry *via = Nodes.resolveRelayByte(pkt.hdr.relay_node, s_myNodeId);
        if (via) {
            prev.unique   = true;
            prev.favorite = via->favorite;
            prev.hasRole  = via->hasRole;
            prev.role     = via->role;
        }
    }
    const bool spendHop = relay::shouldDecrementHopLimit(s_cfg.deviceRole, hopStart, hopLimit, prev);
    const uint8_t outHopLimit = spendHop ? (uint8_t)(hopLimit - 1) : hopLimit;

    MeshHdr hdr = pkt.hdr;
    hdr.flags = (uint8_t)((pkt.hdr.flags & 0xF8) | (outHopLimit & 0x07));
    hdr.relay_node = ourRelayByte;
    // Upstream rewrites next_hop from its route table as it relays. We keep no
    // routes, which upstream treats as no preference: left as our own byte, the
    // nodes after us would see a packet routed to someone else and drop it.
    hdr.next_hop = 0;

    PendingRebroadcast *slot = allocPendingRebroadcast();
    if (!slot) {                             // queue full: drop rather than block
        debugLogMessages("[fwd] drop (queue full) id=%08lx\n",
                         (unsigned long)pkt.hdr.id);
        return;
    }
    const uint32_t delayMs = relay::relayDelayMsec(pkt.snr, relaySlotMs(), esp_random());
    memcpy(slot->frame, &hdr, sizeof(hdr));
    memcpy(slot->frame + sizeof(hdr), pkt.rawCipher, pkt.rawLen);
    slot->len      = sizeof(hdr) + pkt.rawLen;
    slot->sendAtMs = millis() + delayMs;
    slot->from     = pkt.hdr.from;
    slot->id       = pkt.hdr.id;
    slot->snr      = pkt.snr;
    slot->late     = false;
    slot->active   = true;

    debugLogMessages("[fwd] queued from=%08lx id=%08lx hop %u->%u in %lu ms %s\n",
                     (unsigned long)pkt.hdr.from, (unsigned long)pkt.hdr.id,
                     hopLimit, outHopLimit, (unsigned long)delayMs, portnumName(pkt.portnum));
}

// Another node's copy of a packet we may have queued: heard over the radio, so
// someone has just carried it. An ordinary relay stands down (that node covered
// the ground ours would have); a CLIENT_BASE relaying for a favorite moves to
// the late window instead, so it still goes out once, after everyone else.
// MQTT-downlinked copies never get here: a broker delivering a packet says
// nothing about who heard it on the air around us.
static void relayHeardDuplicate(const MeshPacket &pkt) {
    for (uint8_t i = 0; i < kMaxPendingRebroadcast; i++) {
        PendingRebroadcast &p = s_pendingRebroadcast[i];
        if (!p.active || p.from != pkt.hdr.from || p.id != pkt.hdr.id) continue;

        const uint8_t hopLimit = pkt.hdr.flags & 0x07;
        const uint8_t hopStart = (uint8_t)((pkt.hdr.flags >> 5) & 0x07);
        const bool repeatedByOrigin = hopStart > 0 && hopStart == hopLimit;
        switch (relay::onDuplicateHeard(s_cfg.deviceRole, relayFavoriteParty(pkt.hdr), repeatedByOrigin)) {
            case relay::DupeAction::Cancel:
                p.active = false;
                s_relayStoodDownCount++;
                debugLogMessages("[fwd] stood down id=%08lx (heard relayed)\n",
                                 (unsigned long)pkt.hdr.id);
                break;
            case relay::DupeAction::MoveLate:
                if (!p.late) {
                    p.sendAtMs = millis() + relay::lateWindowMsec(p.snr, relaySlotMs());
                    p.late = true;
                    debugLogMessages("[fwd] late window id=%08lx\n", (unsigned long)pkt.hdr.id);
                }
                break;
            case relay::DupeAction::Keep:
                break;
        }
        return;
    }
}

// Milliseconds until the earliest queued relay is due (0 when one already is),
// or false when nothing is queued. Light-sleep naps are cut to this, so a relay
// leaves when its window says rather than up to a nap later.
static bool relayNextDueInMs(uint32_t now, uint32_t &outMs) {
    bool any = false;
    uint32_t best = 0;
    for (uint8_t i = 0; i < kMaxPendingRebroadcast; i++) {
        const PendingRebroadcast &p = s_pendingRebroadcast[i];
        if (!p.active) continue;
        const int32_t left = (int32_t)(p.sendAtMs - now);
        const uint32_t ms = left > 0 ? (uint32_t)left : 0;
        if (!any || ms < best) best = ms;
        any = true;
    }
    outMs = best;
    return any;
}

// Transmit at most one queued rebroadcast whose contention-jitter deadline has
// passed. One TX per loop bounds how long a burst of relays can block the loop
// (Radio.transmit() blocks for the packet airtime); remaining slots go out on
// subsequent iterations a few ms later, which also spreads them out on-air.
static void servicePendingRebroadcast(uint32_t now) {
    if (!Radio.isReady()) return;
    for (uint8_t i = 0; i < kMaxPendingRebroadcast; i++) {
        PendingRebroadcast &p = s_pendingRebroadcast[i];
        if (!p.active || (int32_t)(now - p.sendAtMs) < 0) continue;
        p.active = false;
        if (Radio.transmit(p.frame, p.len)) s_rebroadcastCount++;
        return;
    }
}

// ── camillia chat server client ──────────────────────────────────────────────
// Catches up on channel messages missed while this node was off or away, from a
// camillia chat server (camillia-chat-server repo; protocol in cs_proto.h). The
// decisions live in cs_client.cpp, host-tested; this is the radio, storage and
// UI around it. Port 256 (Meshtastic PRIVATE_APP).
static constexpr uint32_t kCsPortnum = 256;
static constexpr int      kCsDiscoveryChanIdx = -3;   // pkt.chanIdx for the camillia-cs channel
static csc::Client s_csClient;
static bool        s_csStarted = false;
static uint32_t    s_csChanIds[MESH_CHANNELS] = {};

static uint8_t csDiscoveryHash() {
    static int16_t hash = -1;
    if (hash < 0) hash = computeChannelHash(csp::DISCOVERY_CHANNEL_NAME, csp::DISCOVERY_KEY, 16);
    return (uint8_t)hash;
}

static void csComputeChanIds(uint32_t out[MESH_CHANNELS]) {
    for (int i = 0; i < MESH_CHANNELS; i++) {
        const ChannelKey &ck = CHANNEL_KEYS[i];
        const char *name = ck.name_buf[0] ? ck.name_buf : ck.name;
        out[i] = (ck.role != 2 && name && name[0]) ? csp::channelId(name, ck.key, ck.keyLen) : 0;
    }
}

// Saved catch-up positions: which server they belong to and, per channel, which
// channel (by id) they were for, so a reconfigured channel never reuses one.
struct CsSavedStates {
    uint32_t serverId;
    uint32_t chanId[MESH_CHANNELS];
    csc::ChannelState st[MESH_CHANNELS];
};

static void csLoadStates(csc::ChannelState out[MESH_CHANNELS]) {
    memset(out, 0, sizeof(csc::ChannelState) * MESH_CHANNELS);
    CsSavedStates saved = {};
    Preferences p;
    if (!p.begin("cschan", true)) return;
    bool ok = p.getBytesLength("st") == sizeof(saved) && p.getBytes("st", &saved, sizeof(saved)) == sizeof(saved);
    p.end();
    if (!ok || saved.serverId != s_cfg.chatServerNodeId) return;
    for (int i = 0; i < MESH_CHANNELS; i++)
        if (saved.chanId[i] && saved.chanId[i] == s_csChanIds[i]) out[i] = saved.st[i];
}

static void csSaveStates() {
    CsSavedStates saved = {};
    saved.serverId = s_csClient.serverId();
    for (int i = 0; i < MESH_CHANNELS; i++) {
        saved.chanId[i] = s_csChanIds[i];
        saved.st[i] = s_csClient.state(i);
    }
    Preferences p;
    if (!p.begin("cschan", false)) return;
    p.putBytes("st", &saved, sizeof(saved));
    p.end();
}

static void csBegin(uint32_t nowMs) {
    csComputeChanIds(s_csChanIds);
    csc::ChannelState saved[MESH_CHANNELS];
    csLoadStates(saved);
    s_csClient.begin((csc::Mode)s_cfg.chatServerMode, s_cfg.chatServerNodeId,
                     (s_cfg.chatServerFlags & CHAT_SERVER_FLAG_MANUAL) != 0,
                     s_csChanIds, MESH_CHANNELS, saved, nowMs);
    s_csStarted = true;
}

static void csServerLabel(uint32_t nodeId, char *out, size_t outLen) {
    liveNodeLabel(nodeId, out, outLen, false);
}

static void csLive(const char *text, uint16_t color) {
    Serial.printf("[cs] %s\n", text);
    char timePrefix[12];
    liveBuildPrefix(timePrefix, sizeof(timePrefix));
    liveFeedAddPrefixed(timePrefix, text, color, 0, false);
}

// Mirror the client's server choice into the persisted config.
static void csSyncConfig() {
    const uint32_t id = s_csClient.serverId();
    const uint8_t flags = (uint8_t)(CHAT_SERVER_FLAGS_MAGIC | (s_csClient.manual() ? CHAT_SERVER_FLAG_MANUAL : 0));
    if (id != s_cfg.chatServerNodeId || flags != s_cfg.chatServerFlags) {
        s_cfg.chatServerNodeId = id;
        s_cfg.chatServerFlags = flags;
        markConfigDirty();
    }
}

static bool csTransmit(const csc::Send &out) {
    // Off while licensed: every chat-server frame is encrypted.
    if (lic::active()) return false;
    if (!Radio.isReady() || s_myNodeId == 0) return false;
    uint8_t data[csp::MAX_PAYLOAD + 16];
    size_t n = 0;
    data[n++] = (1 << 3) | 0;                 // Data.portnum = 256 (varint)
    data[n++] = 0x80; data[n++] = 0x02;
    data[n++] = (2 << 3) | 2;                 // Data.payload
    if (out.len < 128) {
        data[n++] = (uint8_t)out.len;
    } else {
        data[n++] = (uint8_t)(out.len | 0x80);
        data[n++] = (uint8_t)(out.len >> 7);
    }
    memcpy(data + n, out.payload, out.len);
    n += out.len;
    data[n++] = (9 << 3) | 0;                 // Data.bitfield = 0 (2.8 drops Data without it)
    data[n++] = 0;

    uint8_t frame[sizeof(MeshHdr) + sizeof(data) + MESH_AEAD_OVERHEAD];
    MeshHdr hdr = {};
    hdr.to = out.to;
    hdr.from = s_myNodeId;
    hdr.id = nextMeshPacketId();
    const uint8_t hop = out.hopLimit & 0x07;
    hdr.flags = (uint8_t)(hop | (hop << 5));
    hdr.relay_node = (uint8_t)(s_myNodeId & 0xFF);
    size_t cipherLen = 0;
    if (out.chanIdx < 0) {
        // The discovery channel is ours alone and stays AES-CTR.
        hdr.channel = csDiscoveryHash();
        if (encryptPayload(hdr.id, s_myNodeId, csp::DISCOVERY_KEY, 16, data, frame + sizeof(MeshHdr), n))
            cipherLen = n;
    } else {
        const ChannelKey &ck = CHANNEL_KEYS[out.chanIdx];
        hdr.channel = ck.hash;
        cipherLen = encryptChannelPayload(ck, hdr.id, s_myNodeId, hdr.to, data, n,
                                          frame + sizeof(MeshHdr), sizeof(frame) - sizeof(MeshHdr));
    }
    if (cipherLen == 0) return false;
    memcpy(frame, &hdr, sizeof(hdr));
    const bool ok = Radio.transmit(frame, sizeof(hdr) + cipherLen);

    csp::Type t = csp::DISCOVER;
    csp::peekType(out.payload, out.len, t);
    char dst[16], line[72];
    liveNodeLabel(out.to, dst, sizeof(dst), true);
    snprintf(line, sizeof(line), "T CS U %s %s %s", dst, t == csp::REQUEST ? "REQ" : "DISC", ok ? "TX" : "ER");
    csLive(line, ok ? TFT_DARKGREY : TFT_RED);
    return ok;
}

// A packet on the camillia-cs discovery channel is not in CHANNEL_KEYS, so the
// radio path leaves it encrypted. AES-CTR is symmetric: encryptPayload decrypts.
static void chatServerTryDiscovery(MeshPacket &pkt) {
    if (pkt.decrypted || pkt.rawLen == 0 || pkt.hdr.channel != csDiscoveryHash()) return;
    uint8_t plain[256];
    if (pkt.rawLen > sizeof(plain)) return;
    if (!encryptPayload(pkt.hdr.id, pkt.hdr.from, csp::DISCOVERY_KEY, 16, pkt.rawCipher, plain, pkt.rawLen))
        return;
    uint32_t portnum = 0, requestId = 0;
    bool wantResponse = false;
    const uint8_t *pay = nullptr;
    size_t payLen = 0;
    if (!decodeData(plain, pkt.rawLen, portnum, pay, payLen, requestId, wantResponse)) return;
    if (portnum != kCsPortnum || !pay || payLen > sizeof(pkt.payload)) return;
    memcpy(pkt.payload, pay, payLen);
    pkt.payloadLen = payLen;
    pkt.portnum = portnum;
    pkt.decrypted = true;
    pkt.chanIdx = kCsDiscoveryChanIdx;
}

static uint8_t csHopsTravelled(const MeshHdr &hdr) {
    const uint8_t start = (hdr.flags >> 5) & 0x07, limit = hdr.flags & 0x07;
    return (start == 0 || limit > start) ? 0 : (uint8_t)(start - limit);
}

// Posts one batch's messages into the channel, each at its place in time.
static int csPostItems(int chanIdx, const csp::BatchHeader &h, const csp::Item *items, uint8_t n) {
    const time_t nowT = time(nullptr);
    const bool clockSet = nowT >= 1700000000;
    int posted = 0;
    for (uint8_t k = 0; k < n; k++) {
        const csp::Item &it = items[k];
        if (Channels.hasMessage(chanIdx, it.from, it.packetId) || Ignored.contains(it.from)) continue;

        char text[MESH_TEXT_MAX_LEN + 1];
        utf8util::copyTruncateBytes(text, sizeof(text), (const uint8_t *)it.text, it.textLen);
        for (char *c = text; *c; c++) if (*c == '\r' || *c == '\n') *c = ' ';

        const uint32_t epoch = csc::displayEpoch((uint32_t)nowT, clockSet, it.ageSec);
        char clock[LIVE_CLOCK_BUF] = "--:--";
        if (epoch) {
            time_t e = (time_t)epoch;
            struct tm tmv;
            localtime_r(&e, &tmv);
            liveFormatClock(tmv, clock, sizeof(clock));
        }
        char sender[48], prefix[96];
        chatSenderLabel(it.from, sender, sizeof(sender));
#if UI_CARDPUTER_COMPACT
        snprintf(prefix, sizeof(prefix), "CS %s [%s] ", clock, sender);
#else
        // The refresh mark where LoRa/MQTT messages show their transport icon:
        // "this arrived from the chat server" (v1 marker; to be refined).
        snprintf(prefix, sizeof(prefix), "%s  %s [%s] ", LV_SYMBOL_REFRESH, clock, sender);
#endif
        Channels.insertMessageByEpoch(chanIdx, prefix, text, TFT_WHITE, it.packetId, it.from, epoch, true);
        posted++;
    }
    (void)h;
    if (posted && !channelIsMuted(chanIdx)) {
        const bool chanOnScreen = (chanIdx == s_activeChannel) && !glanceOverlayHidesUi();
        if (!chanOnScreen && chanIdx < MESH_CHANNELS) s_channelNeedsAttention[chanIdx] = true;
        triggerMessageAlert(MSG_ALERT_VISUAL_CHANNEL);
    }
    return posted;
}

// Port-256 packet on a channel (or the discovery channel). True = the visible
// chat changed.
static bool chatServerHandleRx(const MeshPacket &pkt) {
    if (!s_csStarted) return false;
    csp::Type t;
    if (!csp::peekType(pkt.payload, pkt.payloadLen, t)) return false;
    const uint32_t now = millis();
    const uint8_t hops = csHopsTravelled(pkt.hdr);

    if (t == csp::ANNOUNCE && pkt.chanIdx == kCsDiscoveryChanIdx &&
        (pkt.hdr.to == s_myNodeId || pkt.hdr.to == 0xFFFFFFFF)) {
        csp::Announce a;
        if (csp::decodeAnnounce(pkt.payload, pkt.payloadLen, a)) {
            Serial.printf("[cs] rx ANNOUNCE from !%08lx \"%s\" hops %u, %u channel(s)\n",
                          (unsigned long)pkt.hdr.from, a.shortName, hops, a.count);
            s_csClient.onAnnounce(pkt.hdr.from, hops, a, now);
        }
        return false;
    }
    if (t == csp::BATCH && pkt.hdr.to == s_myNodeId && pkt.chanIdx >= 0 && pkt.chanIdx < MESH_CHANNELS &&
        pkt.hdr.channel == CHANNEL_KEYS[pkt.chanIdx].hash) {
        csp::BatchHeader h;
        static csp::Item items[csp::MAX_PAYLOAD / csp::ITEM_OVERHEAD];
        uint8_t n = 0;
        if (!csp::decodeBatch(pkt.payload, pkt.payloadLen, h, items,
                              (uint8_t)(sizeof(items) / sizeof(items[0])), n))
            return false;
        const int accepted = s_csClient.onBatch(pkt.hdr.from, pkt.chanIdx, h, items, n, now);
        Serial.printf("[cs] rx BATCH ch%d %u item(s)%s%s%s seq %lu..%lu -> cursor %lu (epoch %08lx)\n",
                      pkt.chanIdx, n, (h.flags & csp::FLAG_FIRST) ? " FIRST" : "",
                      (h.flags & csp::FLAG_LAST) ? " LAST" : "", (h.flags & csp::FLAG_MORE) ? " MORE" : "",
                      n ? (unsigned long)items[0].seq : 0UL, n ? (unsigned long)items[n - 1].seq : 0UL,
                      (unsigned long)s_csClient.state(pkt.chanIdx).cursor,
                      (unsigned long)s_csClient.state(pkt.chanIdx).epoch);
        if ((h.flags & csp::FLAG_TIME_VALID) && !clockIsSet() && clockIsOursToSet()) {
            struct timeval tv = {(time_t)h.serverTime, 0};
            settimeofday(&tv, nullptr);
            csLive("[cs] clock set from chat server", TFT_DARKGREY);
        }
        if (accepted <= 0) return false;
        return csPostItems(pkt.chanIdx, h, items, (uint8_t)accepted) > 0 && pkt.chanIdx == s_activeChannel;
    }
    return false;
}

static void chatServerService(uint32_t now) {
    if (s_myNodeId == 0) return;
    if (!s_csStarted) csBegin(now);

    // Requests from the web config (it cannot reach the client directly).
    if (s_cfg.chatServerMode != (uint8_t)s_csClient.mode()) chatServerModeChanged();
    uint32_t webNode = 0;
    if (webCfgTakeChatServerSet(webNode)) {
        if (webNode) chatServerSetManual(webNode);
        else chatServerClear();
    }
    if (webCfgTakeChatServerCheck()) {
        const char *why = nullptr;
        webCfgSetChatServerResult(chatServerCheckNow(&why) ? (why ? TR("searching") : TR("checking"))
                                                            : (why ? why : TR("unavailable")));
    }
    // The client is off while licensed: no discovery, no requests.
    if (lic::active()) return;

    uint32_t ids[MESH_CHANNELS];
    csComputeChanIds(ids);
    if (memcmp(ids, s_csChanIds, sizeof(ids)) != 0) csBegin(now);   // channels reconfigured

    csc::Send out;
    if (s_csClient.poll(now, [](int i) {
            csc::Anchor a{0, 0};
            Channels.newestReceived(i, s_myNodeId, a.from, a.packetId);
            return a;
        }, out)) {
        csTransmit(out);
    }

    for (csc::Notice n = s_csClient.takeNotice(); n != csc::NOTICE_NONE; n = s_csClient.takeNotice()) {
        char who[16], line[72];
        csServerLabel(s_csClient.serverId(), who, sizeof(who));
        if (n == csc::NOTICE_FOUND) {
            snprintf(line, sizeof(line), TR("Chat server %s found"), who);
            csLive(line, TFT_GREEN);
        } else if (n == csc::NOTICE_UNREACHABLE) {
            snprintf(line, sizeof(line), TR("Can't reach chat server %s"), who);
            csLive(line, TFT_RED);
        } else if (n == csc::NOTICE_REACHABLE) {
            snprintf(line, sizeof(line), TR("Chat server %s reachable again"), who);
            csLive(line, TFT_GREEN);
        }
    }
    csSyncConfig();
    if (s_csClient.takeStatesDirty()) csSaveStates();
}

static bool chatServerCheckNow(const char **why) {
    if (lic::active()) {
        if (why) *why = TR("Chat server is off while licensed");
        return false;
    }
    const char *reason = nullptr;
    if (!s_csStarted) {
        reason = "busy";
    } else if (s_csClient.checkNow(millis(), &reason)) {
        if (why) *why = reason;   // "searching" when there is no server yet, else null
        return true;
    }
    if (why) {
        *why = !strcmp(reason, "off")       ? TR("Chat server is off")
             : !strcmp(reason, "cooldown")  ? TR("Checked recently, try again in a few minutes")
                                            : TR("Already checking");
    }
    return false;
}

static void chatServerSetManual(uint32_t nodeId) {
    if (!s_csStarted) csBegin(millis());
    s_csClient.setServer(nodeId, true, millis());
    csSyncConfig();
    csSaveStates();
}

static void chatServerClear() {
    if (!s_csStarted) csBegin(millis());
    s_csClient.clearServer(millis());
    csSyncConfig();
    csSaveStates();
}

static void chatServerModeChanged() {
    if (s_csStarted) s_csClient.setMode((csc::Mode)s_cfg.chatServerMode, millis());
}

// "RiCs (manual)" / "RiCs" / "none".
static void chatServerLabel(char *out, size_t outLen) {
    const uint32_t id = s_csStarted ? s_csClient.serverId() : s_cfg.chatServerNodeId;
    if (!id) { snprintf(out, outLen, "%s", TR("none")); return; }
    char who[16];
    csServerLabel(id, who, sizeof(who));
    const bool manual = s_csStarted ? s_csClient.manual() : (s_cfg.chatServerFlags & CHAT_SERVER_FLAG_MANUAL);
    if (manual) snprintf(out, outLen, TR("%s (manual)"), who);
    else snprintf(out, outLen, "%s", who);
}

static bool processMeshPacket(const MeshPacket &rxPkt) {
    MeshPacket pkt = rxPkt;

    if (isDuplicate(pkt.hdr.from, pkt.hdr.id)) {
        if (s_rxFromRadio) relayHeardDuplicate(pkt);
        return false;
    }

    // Match v1 behavior: ignore reflected copies of our own transmitted packets.
    if (s_myNodeId != 0 && pkt.hdr.from == s_myNodeId) return false;

    // User preference: drop packets that arrived from MQTT before any handling
    // (display, rebroadcast, or node-state updates).
    if (s_cfg.ignoreMqtt && (pkt.hdr.flags & 0x10)) return false;

    // Licensed: PKI-encrypted packets are not decrypted, as upstream.
    if (!lic::active() && !pkt.decrypted && pkt.hdr.channel == 0 && pkt.rawLen > 12) {
        NodeEntry *sender = Nodes.find(pkt.hdr.from);
        if (sender && sender->hasPubKey) {
            uint8_t plain[256];
            size_t plainLen = sizeof(plain);
            if (decryptPki(pkt.hdr, pkt.rawCipher, pkt.rawLen, sender->pubKey, plain, plainLen)) {
                pkt.decrypted = true;
                pkt.chanIdx = -2;
                const uint8_t *payPtr = nullptr;
                size_t payLen = 0;
                decodeData(plain, plainLen, pkt.portnum, payPtr, payLen,
                           pkt.requestId, pkt.wantResponse,
                           &pkt.dataDest, &pkt.hasDataDest,
                           &pkt.dataSource, &pkt.hasDataSource,
                           &pkt.env);
                if (payPtr && payLen <= sizeof(pkt.payload)) {
                    memcpy(pkt.payload, payPtr, payLen);
                    pkt.payloadLen = payLen;
                } else if (payPtr) {
                    Serial.printf("[rx] PKI payload too long: port=%lu len=%u cap=%u (dropped)\n",
                                  (unsigned long)pkt.portnum,
                                  (unsigned)payLen,
                                  (unsigned)sizeof(pkt.payload));
                }
            }
        }
    }

    // Licensed: a decoded NodeInfo is read before anything else sees the
    // packet, so a mismatched one makes no node entry and is not relayed. It
    // also passes the unlicensed-sender check below, so a peer that has since
    // become licensed can say so. 0 = not one, 1 = matches, 2 = mismatch.
    uint8_t licNodeInfo = 0;
    bool licPeerIsLicensed = false;
    if (lic::active() && pkt.decrypted && pkt.portnum == NODEINFO_APP) {
        UserInfo lu = {};
        if (decodeUser(pkt.payload, pkt.payloadLen, lu)) {
            licPeerIsLicensed = lu.isLicensed;
            licNodeInfo = lic::acceptNodeInfo(true, lu.isLicensed) ? 1 : 2;
        }
    }

    // Licensed: traffic to or from a node known to be unlicensed is neither
    // processed nor relayed; unknown nodes pass. Always true when not licensed.
    if (!lic::allowPacket(lic::active(),
                          licNodeInfo ? lic::PEER_UNKNOWN : licPeerStatus(pkt.hdr.from),
                          licPeerStatus(pkt.hdr.to))) {
        s_licDropUnlicensed++;
        debugLogMessages("[lic] drop (unlicensed peer) from=%08lx to=%08lx id=%08lx\n",
                         (unsigned long)pkt.hdr.from, (unsigned long)pkt.hdr.to,
                         (unsigned long)pkt.hdr.id);
        return false;
    }

    // Before the node list hears of the packet: one the policy drops does not
    // mark its sender as heard, any more than it is relayed or shown.
    const SigPolicyResult sig = checkPacketSignature(pkt);
    if (!sig.accept) return false;

    // Licensed NodeInfo, once the signature policy has accepted it.
    if (licNodeInfo == 2) {
        licHandleNodeInfoMismatch(pkt, licPeerIsLicensed);
        return false;
    }
    if (licNodeInfo == 1) licRecordPeerLicensed(pkt.hdr.from, licPeerIsLicensed);

    // Store-and-Forward is the one port where hdr.from is not who we just heard.
    // On a replay it is the *original author* of a message the router stored
    // hours ago, while the SNR, hop count and arrival time all describe the
    // router's transmission right now — so feeding it here marks a node that has
    // been silent all afternoon as freshly heard, at someone else's signal
    // strength, and reshuffles the node list every time a burst lands. The S&F
    // case below calls this itself for the frames a router really did originate
    // (heartbeats, pings, stats), where hdr.from means what it usually means.
    if (!(pkt.decrypted && pkt.portnum == STORE_FORWARD_APP)
        // Nothing vouches for the sender of a packet no key opened, so it may
        // refresh a node already known but never create (and persist) one.
        && (pkt.decrypted || Nodes.find(pkt.hdr.from))) {
        Nodes.updateFromPacket(pkt);
    }
    // After the update above, which creates the entry for a first contact.
    if (sig.markSigner) Nodes.markSigner(pkt.hdr.from);
    maybeRequestUnknownNodeInfo(pkt);

    // Live SNR/RSSI sparkline samples (one per received packet).
    chartPushSample(s_snrHist, pkt.snr);
    chartPushSample(s_rssiHist, pkt.rssi);

    // Managed flood: relay new traffic onward before handling it locally. Held
    // while a preset scan has the radio parked: relaying there makes this node a
    // router on a mesh it is visiting for five minutes, and a downlinked MQTT
    // packet relayed from that perch puts one mesh's traffic on another's air.
    chatServerTryDiscovery(pkt);

    if (!discoveryRadioParked()) maybeRebroadcastPacket(pkt);

    // Native MQTT uplink: mirror packets heard on a known, named, uplink-enabled
    // channel up to the broker. Never re-publish packets that arrived via MQTT
    // (via_mqtt flag) — that would form an MQTT→RF→MQTT loop with downlink.
    // Nor packets heard while a preset scan has the radio parked on a foreign
    // mesh: they are not this node's mesh to mirror, and channel 0 is wearing
    // the scanned preset's name for the duration, so they would be published
    // under a channel_id this node does not normally speak on.
    if (pkt.chanIdx >= 0 && pkt.chanIdx < MESH_CHANNELS &&
        !(pkt.hdr.flags & 0x10) &&
        !discoveryRadioParked() &&
        CHANNEL_KEYS[pkt.chanIdx].name[0] &&
        CHANNEL_KEYS[pkt.chanIdx].uplinkEnabled) {
        mqttBridgePublish(pkt, CHANNEL_KEYS[pkt.chanIdx].name);
    }
    // A PKI DM between two other nodes, relayed here but not ours to read: a
    // gateway publishes it to the PKI topic still end-to-end encrypted, as
    // upstream does (meshtastic/firmware#12110), so DMs between nodes this
    // gateway bridges reach the broker. Same gates as above, plus the channel
    // hash 0 that marks PKI, a unicast neither to nor from us, and room for the
    // tag and nonce. Only with encrypted uplink: there is no plaintext to send
    // a JSON/decoded consumer. isDuplicate() above already let only the first
    // copy through.
    else if (pkt.hdr.channel == 0 && !pkt.decrypted
             && pkt.hdr.to != 0xFFFFFFFF && pkt.hdr.to != s_myNodeId
             && pkt.hdr.from != s_myNodeId && s_myNodeId != 0
             && !(pkt.hdr.flags & 0x10)
             && !discoveryRadioParked()
             && s_cfg.mqttEnabled && s_cfg.mqttEncryption
             && pkt.rawLen > 12) {
        mqttBridgePublish(pkt, "PKI");
    }

    bool wantsAck = ((pkt.hdr.flags & (1 << 3)) != 0);
    bool addressedToMe = (pkt.hdr.to == s_myNodeId)
                      || (pkt.hasDataDest && pkt.dataDest == s_myNodeId);

    int chanIdx = (pkt.chanIdx >= 0 && pkt.chanIdx < MESH_CHANNELS) ? pkt.chanIdx : 0;
    if (!pkt.decrypted) {
        appendLiveRxEncrypted(pkt);

        if (wantsAck && addressedToMe) {
            uint32_t err = (pkt.hdr.channel == 0) ? 35u : 6u;  // PKI_UNKNOWN_PUBKEY / NO_CHANNEL
            (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, err);

            // Same hold as the announces: this path answers with our NodeInfo,
            // which on a fresh device is still the compiled-in default identity.
            if (err == 35 && !announceHeldForOnboarding()) {
                static uint32_t sLastNodeInfoReqNode = 0;
                static uint32_t sLastNodeInfoReqMs = 0;
                uint32_t now = millis();
                if (pkt.hdr.from != sLastNodeInfoReqNode || (now - sLastNodeInfoReqMs) > 5000) {
                    (void)Channels.sendNodeInfo(s_myNodeId,
                                                s_cfg.nodeLong,
                                                s_cfg.nodeShort,
                                                pkt.hdr.from,
                                                true,
                                                s_cfg.okToMqtt);
                    sLastNodeInfoReqNode = pkt.hdr.from;
                    sLastNodeInfoReqMs = now;
                }
            }
        }
        return false;
    }

    switch (pkt.portnum) {
        case TEXT_MESSAGE_APP: {
            char textBuf[MESH_TEXT_MAX_LEN + 1];
            size_t copy = utf8util::copyTruncateBytes(
                textBuf,
                sizeof(textBuf),
                pkt.payload,
                pkt.payloadLen);
            for (size_t i = 0; i < copy; i++) {
                if (textBuf[i] == '\r' || textBuf[i] == '\n') textBuf[i] = ' ';
            }

            if (textBuf[0]) {
                const bool viaMqtt = (pkt.hdr.flags & 0x10) != 0;
                noteMsgRxInfo(pkt);   // for Message Info (message actions)
                bool isDirectToMe = (pkt.hdr.to == s_myNodeId)
                                 || (pkt.hasDataDest && pkt.dataDest == s_myNodeId);

                // A broadcast may be the text half of a legacy-split beacon.
                // Checked before the ignore test and before any display work,
                // because this only annotates the stored offer -- the message
                // itself carries on to the chat exactly as it would have.
                // Direct messages are excluded: a beacon is always a broadcast.
                if (!isDirectToMe && s_cfg.meshBeaconListen) {
                    beaconOfferPairSplitText(pkt.hdr.from, chanIdx, textBuf);
                }

                // Suppress chat/DM display for user-ignored senders. We still
                // ACK direct-to-me messages so the sender's radio stops
                // retrying, and NodeDB/Live traffic summaries still get the
                // update via the callers above and appendLiveRxSummary below.
                if (Ignored.contains(pkt.hdr.from)) {
                    if (wantsAck && isDirectToMe) {
                        (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, 0);
                    }
                    appendLiveRxSummary(pkt, chanIdx, "T");
                    return false;
                }

                if (isDirectToMe) {
                    NodeEntry *sender = Nodes.find(pkt.hdr.from);
                    char senderShort[5] = {};
                    if (sender && sender->shortName[0]) {
                        utf8util::copyTruncate(senderShort, sizeof(senderShort), sender->shortName);
                    }

                    char timePrefix[12];
                    char prefix[32];
                    liveBuildPrefix(timePrefix, sizeof(timePrefix));
                    if (senderShort[0]) {
                        snprintf(prefix, sizeof(prefix), "%s[%s] ", timePrefix, senderShort);
                    } else {
                        char who[16];
                        liveNodeLabel(pkt.hdr.from, who, sizeof(who), false);
                        snprintf(prefix, sizeof(prefix), "%s[%s] ", timePrefix, who);
                    }

                    // Open in the DM modal *and* actually visible: with the
                    // glance overlay up the conversation the user was last in
                    // is as unseen as any other, so it must not auto-read.
                    const bool dmOnScreen = dmConversationOnScreen(pkt.hdr.from);

                    DMs.addMessage(pkt.hdr.from,
                                   senderShort[0] ? senderShort : nullptr,
                                   prefix,
                                   textBuf,
                                   TFT_WHITE,
                                   !dmOnScreen,
                                   chanIdx,
                                   pkt.hdr.id,   // retain sender pid for web reply/tapback targeting
                                   true);        // blinks until the conversation is left
                    if (dmOnScreen) {
                        DMs.markRead(pkt.hdr.from);
                    }
#if HAS_SLEEP_OVERLAY
                    else {
                        // -1, not chanIdx: the row should read "DM", not the
                        // channel the DM happened to arrive on.
                        tdeckProNoteRecentMessage(-1, pkt.hdr.from, textBuf, pkt.hdr.id);
                    }
#endif
                } else {
                    appendRxText(chanIdx, pkt.hdr.from, textBuf, pkt.hdr.id, viaMqtt,
                                 pkt.env.replyId, pkt.env.emoji);
                }

                if (isDirectToMe || !channelIsMuted(chanIdx)) {
                    triggerMessageAlert(isDirectToMe ? MSG_ALERT_VISUAL_DM
                                                     : MSG_ALERT_VISUAL_CHANNEL);
                }
                if (wantsAck && isDirectToMe) {
                    (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, 0);
                }

                appendLiveRxSummary(pkt, chanIdx, "T");
                return isDirectToMe ? (s_dmModal != nullptr) : (chanIdx == s_activeChannel);
            }
            return false;
        }

        case kCsPortnum: {
            const bool changed = chatServerHandleRx(pkt);
            appendLiveRxSummary(pkt, chanIdx, "C");
            return changed;
        }

        case STORE_FORWARD_APP:
            // Meshtastic Store & Forward is no longer a client here (replaced by
            // the camillia chat server); its traffic stays visible in Live.
            appendLiveRxSummary(pkt, chanIdx, "F");
            return false;

        case ROUTING_APP: {
            if (!pkt.requestId) return false;

            uint32_t errorReason = 0;
            const uint8_t *routeReplyPayload = nullptr;
            size_t routeReplyLen = 0;
            size_t i = 0;
            while (i < pkt.payloadLen) {
                uint64_t tag = 0;
                i = pbReadVarint(pkt.payload, pkt.payloadLen, i, tag);
                if (!i) break;

                uint32_t field = (uint32_t)(tag >> 3);
                uint32_t wt = (uint32_t)(tag & 7);
                if (wt == 0) {
                    uint64_t v = 0;
                    i = pbReadVarint(pkt.payload, pkt.payloadLen, i, v);
                    if (!i) break;
                    if (field == 3) {
                        errorReason = (uint32_t)v;
                    }
                } else if (wt == 2) {
                    uint64_t sz = 0;
                    size_t j = pbReadVarint(pkt.payload, pkt.payloadLen, i, sz);
                    if (!j) break;
                    if (sz > pkt.payloadLen - j) break;
                    if (field == 2) {
                        routeReplyPayload = pkt.payload + j;
                        routeReplyLen = (size_t)sz;
                    }
                    i = j + (size_t)sz;
                } else if (wt == 5) {
                    if (i + 4 > pkt.payloadLen) break;
                    i += 4;
                } else if (wt == 1) {
                    if (i + 8 > pkt.payloadLen) break;
                    i += 8;
                } else {
                    break;
                }
            }

            bool isAck = (errorReason == 0);
#if HAS_ADMIN_TERMINAL
            // The boot sweep first: its packets are not the session's, and a
            // NAK for one must not close a terminal open on a different node.
            if (!adminProbeOnRouting(pkt.requestId, errorReason)
                // A write is answered by this and nothing else, so the session's
                // notion of "done" lives here rather than in the ADMIN_APP arm.
                && s_adminSession.isOpen() && s_adminSession.nodeId() == pkt.hdr.from) {
                const uint32_t peer = s_adminSession.nodeId();
                s_adminSession.onRouting(pkt.requestId, errorReason,
                                         (pkt.hdr.flags & 0x10) ? AdminClient::TRANSPORT_MQTT
                                                                : AdminClient::TRANSPORT_RF,
                                         millis());
                // The session closes itself on 33/37; demoting the peer is this
                // side's job, because the session does not own the list.
                if (AdminClient::errorMeansUnauthorized(errorReason)) {
                    adminNoteUnauthorized(peer);
                }
            }
#endif
            bool dmRoutingMatched = DMs.handleRoutingResult(pkt.hdr.from, pkt.requestId, errorReason);
            // Routing.ack_proof (Meshtastic 2.8.1). Only for an answer to one of
            // our DMs, from the node it went to -- a nak from a relay is not a
            // receipt -- and advisory, as upstream's is: the verdict is logged,
            // and the ack counts the same either way.
            if (dmRoutingMatched) noteAckProof(pkt);
            tracerouteProgressOnRouting(pkt.hdr.from,
                                        pkt.requestId,
                                        errorReason,
                                        routeReplyPayload,
                                        routeReplyLen,
                                        (pkt.hdr.flags & 0x10) != 0);

            if (isAck) {
                const bool chanMatched = Channels.setAckStateFrom(pkt.requestId, pkt.hdr.from);
#if HAS_HAPTIC
                // A tap on the first ACK for one of our messages, channel or
                // DM. Both managers retire the pending record on that match,
                // so further ACKs for the same message (other nodes, a relay
                // and then the addressee) stay quiet. Off with the rest of
                // vibration.
                if ((chanMatched || dmRoutingMatched)
                    && cfgHapticStyle(s_cfg) != HAPTIC_CFG_OFF) {
                    tdisplayP4HapticTick();
                }
#else
                (void)chanMatched;
#endif
            } else {
                Channels.setAckState(pkt.requestId, DisplayLine::NAKED);
                if (dmRoutingMatched) {
                    DmConv *conv = DMs.find(pkt.hdr.from);
                    if (conv) {
                        const char *errName = routingErrorName(errorReason);
                        char errMsg[44];
                        if (errName) {
                            snprintf(errMsg, sizeof(errMsg), TR("! NAK %s(%lu)"),
                                     errName,
                                     (unsigned long)errorReason);
                        } else {
                            snprintf(errMsg, sizeof(errMsg), TR("! NAK err=%lu"),
                                     (unsigned long)errorReason);
                        }
                        DMs.addMessage(pkt.hdr.from, nullptr, "", errMsg, TFT_RED,
                                       false, -1, 0);
                    }
                }
            }

            appendLiveRxSummary(pkt, chanIdx, isAck ? "A" : "K");
            return true;
        }

        case NODEINFO_APP: {
            UserInfo u = {};
            if (decodeUser(pkt.payload, pkt.payloadLen, u)) {
                if (!lic::acceptNodeInfo(lic::active(), u.isLicensed)) {
                    if (lic::active()) {
                        // Normally caught before the node update and relay;
                        // kept so no path stores, replies to or acks one.
                        licHandleNodeInfoMismatch(pkt, u.isLicensed);
                        return false;
                    }
                    // Not licensed: a licensed peer's NodeInfo is not stored.
                    // Its status alone is kept on a record the packet made.
                    s_licNodeInfoMismatch++;
                    if (NodeEntry *e = Nodes.find(pkt.hdr.from)) {
                        e->isLicensed = u.isLicensed;
                        e->licensedKnown = true;
                    }
                    debugLogMessages("[lic] NodeInfo mismatch from=%08lx (is_licensed=%d)\n",
                                     (unsigned long)pkt.hdr.from, (int)u.isLicensed);
                } else {
                    // Balanced/Strict: an unsigned NodeInfo may not rename or
                    // re-role a node that has signed before. The rest of it is
                    // still taken; the name and role stay as they were.
                    NodeEntry *known = Nodes.find(pkt.hdr.from);
                    if (known && sigRefusesUnsignedIdentity(s_cfg.packetSigPolicy,
                                                            pkt.env.hasSignature,
                                                            known->hasPubKey && known->xeddsaVerified,
                                                            packetOnAeadChannel(pkt))) {
                        const bool renames =
                            (u.longName[0] && known->longName[0]
                             && strncmp(u.longName, known->longName, sizeof(known->longName) - 1) != 0)
                            || (u.shortName[0] && known->shortName[0]
                                && strncmp(u.shortName, known->shortName, sizeof(known->shortName) - 1) != 0);
                        const bool reroles = known->hasRole && u.role != known->role;
                        if (renames || reroles) {
                            noteRefusedUnsignedIdentity(pkt.hdr.from, u);
                            utf8util::copyTruncate(u.longName, sizeof(u.longName), known->longName);
                            utf8util::copyTruncate(u.shortName, sizeof(u.shortName), known->shortName);
                            if (known->hasRole) u.role = known->role;
                        }
                    }
                    if (Nodes.updateUser(pkt.hdr.from, u)) {
                        // Key pinned on first use; a different one only raises a
                        // conflict for the user (NodeDB::updateUser).
                        noteKeyConflictRaised(pkt.hdr.from);
                    }
                }
            }
            // Answer a discovery ping. Discovery is symmetric: we sweep the mesh
            // with want_response, so we owe other nodes the same answer.
            //
            // Throttled per requester off lastSentInfoMs. Meshtastic suppresses
            // for 12 h (NodeInfoModule.cpp:15-21); we use 1 h deliberately —
            // our node table is much smaller and evicts faster, so a peer that
            // fell out of it and came back should not wait half a day to be
            // seen again. The unicast reply also skips sendNodeInfo()'s 15 s
            // broadcast window, so this throttle is the only thing bounding it.
            if (pkt.wantResponse && Radio.isReady() && s_myNodeId != 0
                && !announceHeldForOnboarding()
                && pkt.hdr.from != 0 && pkt.hdr.from != s_myNodeId) {
                NodeEntry *requester = Nodes.find(pkt.hdr.from);
                const uint32_t now = millis();
                const bool throttled = requester && requester->lastSentInfoMs != 0
                    && (uint32_t)(now - requester->lastSentInfoMs) < kNodeInfoReplyThrottleMs;
                if (!throttled) {
                    // want_response false on the reply: theirs was the question,
                    // and answering a question with one is how a ping-pong starts.
                    const bool ok = Channels.sendNodeInfo(s_myNodeId,
                                                          s_cfg.nodeLong,
                                                          s_cfg.nodeShort,
                                                          pkt.hdr.from,
                                                          /*wantResponse=*/false,
                                                          s_cfg.okToMqtt);
                    if (ok && requester) requester->lastSentInfoMs = now;
                }
            }
            if (wantsAck && addressedToMe) {
                (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, 0);
            }
            appendLiveRxSummary(pkt, chanIdx, "N");
            return false;
        }

#if HAS_ADMIN_TERMINAL
        case ADMIN_APP: {
            // Client only: we administer other nodes and do not serve the other
            // half, so the only admin traffic we act on is a reply to something
            // we sent. Anything else is somebody else's conversation passing
            // through -- relayed by the rebroadcast path, ignored here.
            //
            // RF needs nothing extra: handleRx() already PKI-decrypts a
            // hdr.channel == 0 packet from a known key and fills in requestId.
            // via_mqtt is flags bit 4 (see MeshHdr), not a member.
            const AdminClient::Transport via = (pkt.hdr.flags & 0x10)
                ? AdminClient::TRANSPORT_MQTT : AdminClient::TRANSPORT_RF;
            // Only a PKI reply from the node asked: the request id and `from`
            // are cleartext, and a channel-key packet can claim any sender.
            if (pkt.requestId && addressedToMe && pkt.chanIdx == -2
                && !adminProbeOnReply(pkt.hdr.from, pkt.requestId, pkt.payload,
                                      pkt.payloadLen, via)
                && s_adminSession.isOpen() && s_adminSession.nodeId() == pkt.hdr.from) {
                s_adminSession.onAdminReply(pkt.requestId, pkt.payload, pkt.payloadLen,
                                            via, millis());
            }
            if (wantsAck && addressedToMe) {
                (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, 0);
            }
            appendLiveRxSummary(pkt, chanIdx, "A");
            return false;
        }
#endif

        case POSITION_APP: {
            PositionInfo p = {};
            if (decodePosition(pkt.payload, pkt.payloadLen, p)) {
                Nodes.updatePosition(pkt.hdr.from, p);
            }
            if (wantsAck && addressedToMe) {
                (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, 0);
            }
            appendLiveRxSummary(pkt, chanIdx, "P");
            return false;
        }

        case TELEMETRY_APP: {
            TelemetryInfo t = {};
            if (decodeTelemetry(pkt.payload, pkt.payloadLen, t)) {
                Nodes.updateTelemetry(pkt.hdr.from, t);
                if (t.hasDeviceMetrics) {
                    chartPushSample(s_chUtilHist, t.chUtil);
                    chartPushSample(s_airUtilHist, t.airUtil);
                }
            }
            if (wantsAck && addressedToMe) {
                (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, 0);
            }
            appendLiveRxSummary(pkt, chanIdx, "E");
            return false;
        }

        case NEIGHBORINFO_APP: {
            NeighborInfoPayload n = {};
            if (decodeNeighborInfo(pkt.payload, pkt.payloadLen, n)) {
#if FEATURE_DISCOVERY
                // The only view we get of links that do not touch us, and so of
                // nodes we have heard about but never heard from. Discovery is
                // built on it; nothing else consumes it, which is why storing it
                // goes away with Discovery rather than costing RAM for nothing.
                Nodes.updateNeighbors(pkt.hdr.from, n, (pkt.hdr.flags & 0x10) != 0);
#endif
                debugLogMessages("[neighborinfo] from=!%08lx node=!%08lx neighbors=%u interval=%lus\n",
                                 (unsigned long)pkt.hdr.from,
                                 (unsigned long)n.nodeId,
                                 (unsigned)n.neighborCount,
                                 (unsigned long)n.nodeBroadcastIntervalS);
            }
            if (wantsAck && addressedToMe) {
                (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, 0);
            }
            appendLiveRxSummary(pkt, chanIdx, "G");
            return false;
        }

        case MESH_BEACON_APP: {
            // Off by default and gated here rather than at the display: with the
            // setting off nothing is decoded, cached or shown. The live-feed tag
            // below still fires — Live is a traffic monitor, and a packet that
            // arrived did arrive.
            MeshBeaconPayload b = {};
            if (s_cfg.meshBeaconListen && decodeMeshBeacon(pkt.payload, pkt.payloadLen, b)) {
                beaconOfferStore(pkt, b, chanIdx);
                debugLogMessages("[beacon] from=!%08lx msg=\"%.40s\" chan=%s preset=%u region=%u\n",
                                 (unsigned long)pkt.hdr.from, b.message,
                                 b.hasOfferChannel ? b.offerChannelName : "-",
                                 (unsigned)b.offerPreset, (unsigned)b.offerRegion);
            }
            if (wantsAck && addressedToMe) {
                (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, 0);
            }
            appendLiveRxSummary(pkt, chanIdx, "B");
            return false;
        }

        case TRACEROUTE_APP: {
            // want_response marks the outbound half of a traceroute; anything
            // else on this port is somebody's reply, including the one our own
            // progress modal is waiting for.
            const bool isRequestToUs = addressedToMe && pkt.wantResponse;
            if (isRequestToUs) {
                (void)sendTracerouteReply(pkt.hdr.from, pkt.hdr.id,
                                          pkt.payload, pkt.payloadLen, chanIdx,
                                          pkt.snr);
            } else {
                tracerouteProgressOnResponse(pkt);
            }
            if (wantsAck && addressedToMe) {
                (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, 0);
            }
            appendLiveRxSummary(pkt, chanIdx, "R");
            return false;
        }

        default:
            return false;
    }
}

// Drain a clock set queued by the web config form. Same reason as the chat send
// below: the form handler runs on the web server task, and the system clock is
// set from here.
static void serviceWebCommunityPreset() {
    int idx = -1;
    if (!webCfgTakeCommunityPreset(idx)) return;
    const char *err = communityPresetApply(idx);   // reboots on success
    Serial.printf("[web] community preset not applied: %s\n", err ? err : "nothing changed");
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Web preset not applied: %s"),
             err ? err : TR("nothing changed"));
}

static void serviceWebManualTime() {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    if (!webCfgTakeManualTime(y, mo, d, h, mi)) return;
    if (applyManualClock(y, mo, d, h, mi)) {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Clock set from web config"));
    } else {
        Serial.printf("[time] web manual clock rejected: %04d-%02d-%02d %02d:%02d\n",
                      y, mo, d, h, mi);
    }
}

#if HAS_VNC_HOST
static void serviceVncPinNotice() {
    if (!vncHostEnabled() || !vncHostPin()[0] || s_onboardingModal) return;
    const uint32_t serial = vncHostPinSerial();
    if (serial == s_vncPinNoticeSerial) return;
    const bool redrawn = vncHostSession() == s_vncPinNoticeSession;
    s_vncPinNoticeSerial = serial;
    s_vncPinNoticeSession = vncHostSession();
    if (redrawn) {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                 TR("Too many wrong VNC PINs\nNew PIN: %s"), vncHostPin());
    } else {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                 TR("VNC Host on - PIN %s\nEnter it in the web config's Remote tab"),
                 vncHostPin());
    }
    openCfgActionMessageModal(s_cfgStatus);
}

static void serviceWebVncToggle() {
    bool enabled = false;
    if (webCfgTakeVncToggle(enabled)) {
        if (applyVncEnabled(enabled)) {
            Serial.printf("[vnc] web checkbox: %s\n", enabled ? "enabled" : "disabled");
        } else {
            Serial.println("[vnc] web checkbox: enable rejected");
        }
    }
    serviceVncPinNotice();
}
#endif

// Drain a pending Chat-tab send (queued by the web server) on the main loop,
// where we own the node id and the LoRa TX path.
static void serviceWebChatSend() {
    bool isDm = false;
    uint32_t target = 0, replyId = 0, emoji = 0;
    char text[MESH_TEXT_MAX_LEN + 1];
    if (!webCfgTakeChatSend(isDm, target, text, sizeof(text), replyId, emoji)) return;
    if (!text[0]) return;
    if (s_myNodeId == 0) deriveNodeId();
    if (s_myNodeId == 0) return;

    if (isDm) {
        DMs.sendDm(s_myNodeId, target, text, replyId, emoji);
        refreshDmModal(true);
    } else {
        int ch = (int)target;
        if (ch < 0 || ch >= MESH_CHANNELS) ch = s_activeChannel;
        Channels.sendText(s_myNodeId, text, s_cfg.okToMqtt, ch, replyId, emoji);
        refreshChatView(true);
    }
}

static bool pollMeshRx() {
    bool changed = false;
    MeshPacket pkt;
    while (Radio.pollRx(pkt)) {
        s_rxFromRadio = true;
        changed = processMeshPacket(pkt) || changed;
        s_rxFromRadio = false;
    }

    if (Channels.expireAcks()) {
        changed = true;
    }
    // DMs are transmitted straight from DmMgr and never enter ChannelMgr's
    // pending table, so they need their own sweep or they never time out at all.
    if (DMs.expireAcks()) {
        changed = true;
    }

    return changed;
}

// Set by the MQTT downlink inject when it produces a UI-visible change; consumed
// in loop() so the same refresh path as RX packets runs this iteration.
static bool s_mqttDownlinkUiDirty = false;

// Downlink sink: a ServiceEnvelope arrived from the broker. Reconstruct a
// received-style packet and run it through the normal RX pipeline so it displays
// and (for router roles) rebroadcasts onto LoRa — gated by the channel's
// downlink flag. Called from mqttBridgeLoop() on the main loop.
//
// plaintext: the envelope's packet carried `decoded` Data rather than
// ciphertext, as stock nodes publish with MQTT encryption off. Such a packet is
// encrypted here with the named channel's key and stamped with its hash, so from
// there on it is the encrypted form and takes the same path — decrypt, dedup
// against the RF and encrypted-MQTT copies, and a relay that has on-air bytes
// to send. Upstream does the same, re-encrypting on its way back to LoRa.
static void mqttDownlinkInject(const MeshHdr &hdrIn, const uint8_t *cipherIn,
                               size_t cipherLen, bool plaintext, const char *chanName) {
    MeshHdr hdr = hdrIn;
    const uint8_t *cipher = cipherIn;
    // Nothing from the broker enters the RX pipeline while a preset scan has the
    // radio parked on another preset. The bridge keeps its session — dropping a
    // downlink costs one packet, where dropping the session costs a reconnect —
    // but an injected packet updates the node table, feeds Discovery's neighbor
    // graph and is a candidate for rebroadcast, all of it attributed to a window
    // that is supposed to describe five minutes of one foreign preset's air.
    if (discoveryRadioParked()) return;

    // Licensed: third-party traffic from the internet is not this station's
    // transmission, so nothing from the broker enters the RX (and relay) path.
    if (lic::active()) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            Serial.println("[mqtt] downlink drop: licensed mode refuses MQTT downlink");
        }
        return;
    }

    // Loop guard: ignore our own packets echoed back by the broker. The dedup
    // check (against RF-heard copies and duplicate MQTT copies) is left to
    // processMeshPacket() below — isDuplicate() records as a side effect, so
    // calling it here would make that check drop the packet.
    if (s_myNodeId != 0 && hdr.from == s_myNodeId) {
        Serial.println("[mqtt] downlink drop: own packet echoed back");
        return;
    }

    // PKI traffic rides a pseudo-channel named "PKI" at <root>/2/e/PKI/<gateway>,
    // with ServiceEnvelope.channel_id = "PKI". It resolves against no local
    // channel and its hdr.channel is 0 -- which is the PKI marker itself -- so
    // the two rules below would drop every one of these. Narrowed to packets
    // addressed to us, which is upstream's own isToUs rule for this topic.
    //
    // Deliberate side effect: PKI direct messages over MQTT start working. They
    // were silently discarded by these same two lines, not by anything that
    // meant to reject them.
    const bool isPkiEnvelope = chanName && !strcmp(chanName, "PKI");
    if (isPkiEnvelope) {
        // A PKI packet is only ever ciphertext; there is no key to check a
        // "decoded" one against, so it would be an unauthenticated DM.
        if (plaintext) {
            Serial.println("[mqtt] downlink drop: decoded packet on PKI topic");
            return;
        }
        if (s_myNodeId == 0 || hdr.to != s_myNodeId) {
            Serial.println("[mqtt] downlink drop: PKI packet not addressed to us");
            return;
        }
        Serial.printf("[mqtt] downlink inject: PKI from=%08lx\n",
                      (unsigned long)hdr.from);
        MeshPacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.hdr = hdr;
        pkt.rxMs = millis();
        pkt.chanIdx = -1;
        pkt.decrypted = false;
        if (cipherLen > 0 && cipherLen <= sizeof(pkt.rawCipher)) {
            memcpy(pkt.rawCipher, cipher, cipherLen);
            pkt.rawLen = cipherLen;
        }
        // Decryption is handleRx()'s job: it already does the PKI path for a
        // hdr.channel == 0 packet from a node whose key we hold, which is
        // exactly this. Handing it the ciphertext keeps one implementation.
        processMeshPacket(pkt);
        return;
    }

    // Resolve the channel by name; require it to exist locally with downlink on.
    int idx = -1;
    for (int i = 0; i < MESH_CHANNELS; i++) {
        if (CHANNEL_KEYS[i].name[0] && chanName &&
            strcmp(CHANNEL_KEYS[i].name, chanName) == 0) { idx = i; break; }
    }
    if (idx < 0) {
        Serial.printf("[mqtt] downlink drop: no local channel named '%s'\n",
                      chanName ? chanName : "(null)");
        return;
    }
    if (!CHANNEL_KEYS[idx].downlinkEnabled) {
        Serial.printf("[mqtt] downlink drop: channel '%s' has downlink disabled\n",
                      CHANNEL_KEYS[idx].name);
        return;
    }

    uint8_t sealed[sizeof(((MeshPacket *)nullptr)->rawCipher)];
    if (plaintext) {
        uint32_t port = 0, reqId = 0; bool wantResp = false;
        const uint8_t *pay = nullptr; size_t payLen = 0;
        if (cipherLen == 0 || cipherLen > sizeof(sealed) ||
            !decodeData(cipher, cipherLen, port, pay, payLen, reqId, wantResp)) {
            Serial.printf("[mqtt] downlink drop: malformed decoded packet on '%s'\n",
                          CHANNEL_KEYS[idx].name);
            return;
        }
        // Upstream's rule: anyone on the broker can write a decoded packet, and
        // nothing in it proves who sent it, so it never reaches admin handling.
        if (port == ADMIN_APP) {
            Serial.printf("[mqtt] downlink drop: decoded ADMIN packet from=%08lx\n",
                          (unsigned long)hdr.from);
            return;
        }
        // Sealed the way the channel is: AES-CCM with its tag on an AEAD
        // channel, so decryptPacket() and every node we relay it to can open it
        // under the hash set below. Never signed here: our own packets were
        // dropped above, and the signer only signs as us.
        const size_t sealedLen = encryptChannelPayload(CHANNEL_KEYS[idx], hdr.id, hdr.from, hdr.to,
                                                       cipher, cipherLen, sealed, sizeof(sealed));
        if (sealedLen == 0) {
            Serial.println("[mqtt] downlink drop: could not encrypt decoded packet");
            return;
        }
        cipher = sealed;
        cipherLen = sealedLen;
        // A decoded packet's channel field is the sender's channel index, not
        // the on-air hash decryptPacket() and the relay need.
        hdr.channel = CHANNEL_KEYS[idx].hash;
    }

    Serial.printf("[mqtt] downlink inject: channel='%s' from=%08lx%s\n",
                  CHANNEL_KEYS[idx].name, (unsigned long)hdr.from,
                  plaintext ? " (decoded)" : "");

    // Reconstruct the decoded packet the same way MeshRadio::pollRx() does, then
    // hand it to the shared RX handler (decrypt/display + rebroadcast).
    MeshPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.hdr      = hdr;               // via_mqtt already forced on by the decoder
    pkt.rxMs     = millis();
    pkt.chanIdx  = -1;
    pkt.decrypted = false;
    if (cipherLen > 0 && cipherLen <= sizeof(pkt.rawCipher) && hdr.channel != 0) {
        memcpy(pkt.rawCipher, cipher, cipherLen);
        pkt.rawLen = cipherLen;
        uint8_t plain[256];
        size_t plainLen = 0;
        pkt.chanIdx = decryptPacket(pkt.hdr, cipher, plain, cipherLen, plainLen);
        pkt.decrypted = (pkt.chanIdx >= 0);
        if (pkt.decrypted) {
            const uint8_t *payPtr; size_t payLen;
            decodeData(plain, plainLen, pkt.portnum, payPtr, payLen,
                       pkt.requestId, pkt.wantResponse,
                       &pkt.dataDest, &pkt.hasDataDest,
                       &pkt.dataSource, &pkt.hasDataSource,
                       &pkt.env);
            if (payPtr && payLen <= sizeof(pkt.payload)) {
                memcpy(pkt.payload, payPtr, payLen);
                pkt.payloadLen = payLen;
            }
        }
    }

    if (processMeshPacket(pkt)) s_mqttDownlinkUiDirty = true;
}

