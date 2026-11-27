// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: beacon offers, temporary retune, channel Config and edit modals.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

static void beaconOfferPairSplitText(uint32_t from, int chanIdx, const char *text) {
    if (!text || !text[0]) return;
    for (int i = 0; i < s_beaconOfferCount; i++) {
        BeaconOffer &o = s_beaconOffers[i];
        if (o.sender != from) continue;
        if (o.chanIdx != (int8_t)chanIdx) return;   // same node, other channel
        if (o.message[0]) return;                   // already has words
        const uint32_t nowMs = millis();
        if ((uint32_t)(nowMs - o.rxMs) > kBeaconSplitPairMs) return;
        utf8util::copyTruncate(o.message, sizeof(o.message), text);
        o.textPaired = true;
        s_beaconOfferSeq++;                          // repaint the Beacons list
        debugLogMessages("[beacon] paired split text from=!%08lx chan=%d \"%.40s\"\n",
                         (unsigned long)from, chanIdx, o.message);
        return;
    }
}


// Newest first, one entry per sender: a beacon repeats on an interval and the
// latest one is the only one worth keeping. The repeat itself is not thrown
// away though — the count and the first-heard stamp survive the overwrite, and
// together they are what lets the screen say how often this sender beacons.
static void beaconOfferStore(const MeshPacket &pkt, const MeshBeaconPayload &b, int chanIdx) {
    int slot = -1;
    for (int i = 0; i < s_beaconOfferCount; i++) {
        if (s_beaconOffers[i].sender == pkt.hdr.from) { slot = i; break; }
    }
    const bool repeat = (slot >= 0);
    uint32_t firstMs = repeat ? s_beaconOffers[slot].firstMs : pkt.rxMs;
    uint16_t hits    = repeat ? s_beaconOffers[slot].hits : 0;
    // Carried across the move and memset below. A sender running Meshtastic's
    // FLAG_LEGACY_SPLIT emits the offer and the text as two packets, so a repeat
    // beacon from that node has no message in it -- clearing the slot outright
    // would drop text we had already paired to this offer, and the advert would
    // go blank on the second beacon.
    char prevMessage[sizeof(s_beaconOffers[0].message)] = {};
    if (repeat) {
        utf8util::copyTruncate(prevMessage, sizeof(prevMessage), s_beaconOffers[slot].message);
    }
    const bool prevPaired = repeat && s_beaconOffers[slot].textPaired;
    if (slot < 0) {
        if (s_beaconOfferCount < kBeaconOfferMax) slot = s_beaconOfferCount++;
        else slot = kBeaconOfferMax - 1;          // evict the oldest
    }
    // Move to the front so index 0 is always the most recent.
    for (int i = slot; i > 0; i--) s_beaconOffers[i] = s_beaconOffers[i - 1];
    BeaconOffer &o = s_beaconOffers[0];

    memset(&o, 0, sizeof(o));
    o.sender  = pkt.hdr.from;
    o.rxMs    = pkt.rxMs;
    o.firstMs = firstMs;
    o.chanIdx = (int8_t)chanIdx;
    o.hits    = (hits < 0xFFFFu) ? (uint16_t)(hits + 1) : hits;
    o.snr     = pkt.snr;
    o.rssi    = pkt.rssi;
    // Same derivation the node table uses: without hop_start, "0 hops" is an
    // absent field rather than a direct neighbor.
    {
        const uint8_t hopStart = (uint8_t)((pkt.hdr.flags >> 5) & 0x07);
        const uint8_t hopLimit = (uint8_t)(pkt.hdr.flags & 0x07);
        if (hopStart > 0) {
            o.hops = (hopStart > hopLimit) ? (uint8_t)(hopStart - hopLimit) : 0;
            o.hasHops = true;
        }
    }
    if (b.message[0]) {
        // A combined beacon carries its own text and always wins.
        utf8util::copyTruncate(o.message, sizeof(o.message), b.message);
        o.textPaired = false;
    } else if (prevPaired && prevMessage[0]) {
        // Offer-only repeat from a legacy-split sender: keep the text we paired
        // to the previous one rather than blanking the advert.
        utf8util::copyTruncate(o.message, sizeof(o.message), prevMessage);
        o.textPaired = true;
    }
    o.hasChannel = b.hasOfferChannel;
    if (b.hasOfferChannel) {
        utf8util::copyTruncate(o.channelName, sizeof(o.channelName), b.offerChannelName);
        o.channelHasPsk = (b.offerPskLen > 0);
        o.channelUsesAead = b.offerUsesAead;
    }
    o.preset = b.hasOfferPreset ? presetFromMeshtastic(b.offerPreset) : -1;
    const char *rc = regionCodeFromMeshtastic(b.offerRegion);
    if (rc) utf8util::copyTruncate(o.region, sizeof(o.region), rc);
    o.hasSlot = b.hasOfferSlot;
    o.slot    = b.offerSlot;
    s_beaconOfferSeq++;
}

// The Beacons tool's own surface. Beacons used to be a group on Discovery,
// which was the wrong home for them: Discovery answers "who is out there right
// now" and is built around a sweep whose window is 60 s, where a beacon repeats
// on the sender's schedule — minutes or hours apart. A group that is empty
// nearly every time you look at it teaches you to stop looking, so they get a
// screen that is only ever about them, and that keeps what it has heard until
// you clear it.
static lv_obj_t *s_beaconsModal = nullptr;
static lv_obj_t *s_beaconsList = nullptr;
static lv_obj_t *s_beaconsStatusLabel = nullptr;
static lv_obj_t *s_beaconsTxLabel = nullptr;   // "Your beacon: ..."
// One per card, holding the only text that moves on its own. Kept so a ticking
// age does not cost a full teardown of a list the user may be reading.
static lv_obj_t *s_beaconAgeLabels[kBeaconOfferMax] = {};
static uint32_t  s_beaconsRenderedSig = 0;

#if UI_TOUCH_ONLY_PROFILE
static const lv_font_t *kBeaconsBodyFont  = &lv_font_montserrat_12;
static const lv_font_t *kBeaconsTitleFont = &lv_font_montserrat_14;
#else
static const lv_font_t *kBeaconsBodyFont  = &lv_font_montserrat_10;
static const lv_font_t *kBeaconsTitleFont = &lv_font_montserrat_12;
#endif

// Same amber-on-pale-blue split Discovery uses for headings and rows.
static constexpr uint32_t kBeaconsTitleColor = 0xFFC98A;
static constexpr uint32_t kBeaconsBodyColor  = 0xD9E8FF;
static constexpr uint32_t kBeaconsDimColor   = 0xA7C7FF;

// ── Discovery ────────────────────────────────────────────────────────────────
// Compiled out on the Cardputer, where the ~3 KB it holds costs more than the
// feature is worth on a 240x135 panel — see FEATURE_DISCOVERY in config.h. The
// NodeInfo reply below it is not part of that: it costs nothing and is how
// other people's sweeps see us.
//
// Two questions the rest of the UI cannot answer: who is out there, and how are
// they attached to us. The Nodes screen lists who we have heard *from*; this
// adds the shape — direct neighbors, nodes by hop count, and nodes that only
// appear in somebody else's neighbor list, which we have heard *about* and
// never from.
//
// The passive half costs nothing: NEIGHBORINFO_APP reports were already
// arriving and being thrown away. The active half is one broadcast, on demand.

// Per-requester throttle on answering a NodeInfo want_response. Meshtastic
// suppresses for 12 h (NodeInfoModule.cpp:15-21); 1 h is a deliberate local
// divergence — our node table is far smaller and evicts sooner, so a peer that
// aged out and came back should not be invisible to us for half a day.
static constexpr uint32_t kNodeInfoReplyThrottleMs = 3600000UL;

// Asking an unnamed node for its NodeInfo (maybeRequestUnknownNodeInfo()), as
// Meshtastic does on hearing a node it has no user for (MeshService.cpp, "Heard
// new node"). Unlike upstream it is bounded three ways, because every request is
// a unicast flood and a busy mesh has many unnamed senders: per node, across all
// nodes, and by distance and channel load.
static constexpr uint32_t kAutoNodeInfoPerNodeMs = 1800000UL;   // 30 min
static constexpr uint32_t kAutoNodeInfoGapMs     = 30000UL;     // any node
static constexpr uint8_t  kAutoNodeInfoMaxHops   = 3;
static constexpr float    kAutoNodeInfoMaxChUtil = 25.0f;       // Meshtastic's polite threshold

#if FEATURE_DISCOVERY
// One sweep per minute. Meshtastic's floor for an interactive, user-triggered
// NodeInfo request (NodeInfoModule.cpp:168-172, the shorterTimeout path).
static constexpr uint32_t kDiscoverySweepMinGapMs = 60000UL;

// How long replies are counted against a sweep before it is called done. A flat
// minute: comfortably past a 3-hop round trip with relay backoff, short enough
// that a dead sweep stops looking live, and a round figure someone waiting on
// one can hold in their head next to the preset scan's five.
static constexpr uint32_t kDiscoverySweepWindowMs = 60000UL;

// Quiet time after a sweep closes before another is accepted. Deliberately
// separate from kDiscoverySweepMinGapMs above, which measures from a sweep's
// *start* and cites an upstream rule: folding this into that number would make
// its citation false, and would silently vanish on the preset scan below, whose
// window outlasts the gap several times over. Two rules, each measuring what it
// says it measures.
static constexpr uint32_t kDiscoverySweepCooldownMs = 15000UL;

// A preset scan listens far longer than a sweep waits for replies, and for a
// different reason: the one broadcast collects whoever answers, and the rest of
// the window is what catches the foreign mesh's own periodic NodeInfo, Position
// and Telemetry broadcasts. That is where most of the picture comes from.
static constexpr uint32_t kDiscoveryPresetScanWindowMs = 300000UL;   // 5 minutes

// Both windows are now a question rather than a constant: the two above are the
// defaults the picker opens on, and what actually times a run is the pair of
// runtime values below.
//
// The reason to ask is that neither number is right for every job. A sweep in a
// room with three nodes is over in fifteen seconds; a scan of a foreign preset
// on a quiet band is mostly waiting for a periodic broadcast that may be minutes
// away. The old fixed minute and five minutes were reasonable middles, and a
// reasonable middle is exactly what someone with a specific mesh in front of
// them does not need.
struct DiscoveryDurationOption {
    uint32_t    ms;
    const char *label;
};
static const DiscoveryDurationOption kDiscoveryDurations[] = {
    {    30000UL, TR_NOOP("30 sec") },
    {    60000UL, TR_NOOP("1 min")  },
    {   120000UL, TR_NOOP("2 min")  },
    {   300000UL, TR_NOOP("5 min")  },
    {   600000UL, TR_NOOP("10 min") },
    {   900000UL, TR_NOOP("15 min") },
    // The long end, for leaving a board somewhere and coming back to it. A
    // sweep at these lengths is cheap -- the one broadcast has already gone out
    // and the rest is listening -- so the cost is only the screen staying on.
    //
    // A PRESET SCAN AT THESE LENGTHS IS NOT CHEAP, and the difference is worth
    // being blunt about: a scan parks the radio on a foreign preset for the
    // whole window, so the node is deaf to its own mesh for as long as it runs.
    // Six hours of that is six hours of missed messages on your own channels,
    // and nothing on the device will tell you afterwards what you did not hear.
    // The picker offers it because surveying a quiet band genuinely needs it;
    // the row is not a mistake, but it is not the one to reach for by default.
    //
    // 6 h is 21,600,000 ms, comfortably inside the uint32_t these are held in
    // and inside the ~49-day wrap of the millis() subtraction that times them.
    {  1800000UL, TR_NOOP("30 min") },
    {  3600000UL, TR_NOOP("1 hour") },
    {  7200000UL, TR_NOOP("2 hours")},
    { 21600000UL, TR_NOOP("6 hours")},
};
static constexpr int kDiscoveryDurationCount =
    (int)(sizeof(kDiscoveryDurations) / sizeof(kDiscoveryDurations[0]));

// What the run in flight is timed against. Seeded from the constants above, so
// a device that never opens the picker behaves exactly as it did.
static uint32_t s_discoverySweepWindowMs     = kDiscoverySweepWindowMs;
static uint32_t s_discoveryPresetScanWindowMs = kDiscoveryPresetScanWindowMs;

// Which run the picker is standing in front of: a preset index, or this
// sentinel for a plain sweep. The slider's apply() takes only an index, so the
// thing it is about has to be parked here between opening and committing.
static constexpr uint8_t kDiscoveryNoPresetPending = 0xFF;
static uint8_t s_discoveryPendingPreset = kDiscoveryNoPresetPending;

// "Save while discovering", from Sweep/Scan Settings. When set, the run writes
// its results to storage as they arrive instead of waiting for the Save button.
// Off at boot and remembered for the session, like the durations beside it.
static bool s_discoverySaveWhileRunning = false;

// Meshtastic's *polite* channel-utilization threshold (airtime.h:70-72); its
// hard refusal is 40%. A sweep is discretionary and asks others to transmit, so
// it is held to the polite one. Measured locally by Radio.channelUtilPercent()
// over a rolling hour — slower to react than Meshtastic's shorter periods, so
// it errs toward refusing after a busy spell rather than during one.
static constexpr float kDiscoverySweepMaxChUtil = 25.0f;

// The single most dangerous number in the feature: every node inside this many
// hops answers a sweep. See ChannelMgr::sendDiscoverySweep().
static constexpr uint8_t kDiscoverySweepHopLimit = 3;

// Which column each group renders into. Wide panels have the room to put the
// groups side by side instead of making the user scroll past one to reach the
// next; narrow ones stack them in a single column as before. The column count
// falls out of the highest index used, so a board is described by these three
// numbers alone.
//
//   Pager (480 wide): DIRECT | distance | HEARD ABOUT
//   T-Deck, Mesh Deck, M9 (320): DIRECT + distance | HEARD ABOUT
//   Cardputer, Heltec: one column, everything stacked
//   T-Display P4: the Pager's three in landscape (616 wide), one in portrait
static constexpr int kDiscoveryColDirect = 0;
#if defined(DEVICE_TDISPLAY_P4)
// The one board whose shape is not known at compile time: the orientation is
// chosen at boot (HAS_RUNTIME_ORIENTATION), so these are read when the screen
// is built rather than fixed here. Landscape has the Pager's width and more;
// portrait is 284 wide and stacks like the Heltec. uiPortrait() does not
// change while the firmware runs, so every reader sees the same layout.
#define kDiscoveryColDistance (uiPortrait() ? 0 : 1)
#define kDiscoveryColHeard    (uiPortrait() ? 0 : 2)
#define kDiscoveryCols        (kDiscoveryColHeard + 1)
static constexpr int kDiscoveryColsMax = 3;
#elif defined(DEVICE_TLORA_PAGER_TFT)
static constexpr int kDiscoveryColDistance = 1;
static constexpr int kDiscoveryColHeard    = 2;
#elif defined(DEVICE_TDECK) || defined(DEVICE_MESH_DECK) || defined(DEVICE_M9)
static constexpr int kDiscoveryColDistance = 0;
static constexpr int kDiscoveryColHeard    = 1;
#else
static constexpr int kDiscoveryColDistance = 0;
static constexpr int kDiscoveryColHeard    = 0;
#endif
#if !defined(DEVICE_TDISPLAY_P4)
static constexpr int kDiscoveryCols = kDiscoveryColHeard + 1;
static constexpr int kDiscoveryColsMax = kDiscoveryCols;
#endif

#if UI_TOUCH_ONLY_PROFILE
// Touch build with no keyboard and one column of results: it has the width to
// spend on legibility, and nothing competing for it.
static const lv_font_t *kDiscoveryBodyFont   = &lv_font_montserrat_12;
static const lv_font_t *kDiscoveryHeaderFont = &lv_font_montserrat_14;
#else
static const lv_font_t *kDiscoveryBodyFont   = &lv_font_montserrat_10;
static const lv_font_t *kDiscoveryHeaderFont = &lv_font_montserrat_12;
#endif

// Group headings are amber against the body's pale blue. Deliberately not one
// of the status colours used elsewhere in this modal — green and red mean
// "worked" and "failed" here, and a heading means neither.
static constexpr uint32_t kDiscoveryHeaderColor = 0xFFC98A;
static constexpr uint32_t kDiscoveryBodyColor   = 0xD9E8FF;

// Longest node name a row will print, including the terminator. Long names run
// to 40 characters, which would wrap every row of a three-column Pager; one
// column has the width to show far more of one.
#if defined(DEVICE_TDISPLAY_P4)
// Sized for one column; discoveryNodeLabel() cuts to the three-column length
// when the orientation gives it three.
static constexpr size_t kDiscoveryNameMax = 29;
#else
static constexpr size_t kDiscoveryNameMax = (kDiscoveryCols == 1) ? 29 : 19;
#endif

static lv_obj_t *s_discoveryModal = nullptr;
static lv_obj_t *s_discoveryStatusLabel = nullptr;
// Wardriving line under the summary: GPS state and what the wardrive log has
// taken. Only exists while the log is on, and is updated in place by
// refreshDiscoveryModal() rather than by a rebuild, because the satellite count
// and line count change far more often than anything else on the screen.
static lv_obj_t *s_discoveryWardriveLabel = nullptr;
static char      s_discoveryWardriveText[96];
static lv_obj_t *s_discoveryList = nullptr;
// The label on the Clear/Cancel button. Held because its text is the button's
// whole state: it is one control doing whichever of the two the screen is
// currently able to mean, exactly as the C key is.
static lv_obj_t *s_discoveryClearLabel = nullptr;
// The key-hint line, held for the same reason: the word it gives C has to follow
// what C currently does. Declared on every build so the teardown paths can null
// it without a guard; only the keyboard builds ever create one.
static lv_obj_t *s_discoveryHintLabel = nullptr;
// What the button and the hint were last told to say. Compared rather than the
// label text, so the two cannot disagree and neither is rewritten on a tick
// where nothing changed -- lv_label_set_text() invalidates.
static bool      s_discoveryActionsShowCancel = false;
static lv_obj_t *s_discoveryColBoxes[kDiscoveryColsMax] = {};
// 0 = no sweep in flight. Survives the modal closing so a sweep cannot be
// restarted by closing and reopening, and so its result is there on return.
static uint32_t s_discoverySweepStartedMs = 0;
static uint32_t s_discoveryLastSweepMs = 0;
static int      s_discoverySweepBaseNodes = 0;
static char     s_discoveryStatus[72] = {};
static uint32_t s_discoveryRenderedSig = 0;
// When the last sweep's window closed, for kDiscoverySweepCooldownMs. 0 = none
// has closed this boot.
static uint32_t s_discoverySweepEndedMs = 0;

// ── Preset scan ──────────────────────────────────────────────────────────────
// A sweep answers "who is out there on the preset this node runs". A preset
// scan asks the same question of a preset the node is *not* running: it parks
// the radio there, sweeps, and puts the radio back.
//
// The manoeuvre is Meshtastic 2.7's beacon trick in reverse — their nodes
// retune briefly to *advertise* on a foreign preset (mesh_proto.h:148) and we
// have only ever received those. This retunes to listen.
//
// Nothing about it is a configuration change, and that distinction is the whole
// design. s_cfg is never written, so no save path can make the scanned preset
// permanent — see discoveryStartPresetScan() for why that matters more than it
// sounds. The only global state it moves is the radio tuning and channel 0's
// name, both restored by discoveryEndPresetScan().
static bool     s_presetScanActive = false;
static uint8_t  s_presetScanPreset = 0;        // PRESET_* being scanned
// While non-zero, the screen is reporting one preset scan rather than the node
// table at large, and "heard" narrows to "heard over the radio at or after this
// millis()". Kept set after the scan ends so the result is still on screen when
// the user reads it; cleared by a normal sweep or by Clear.
//
// This is the fix for a scan reporting nodes that were never on the scanned
// preset. The window alone is not enough: the MQTT bridge keeps running while
// the radio is parked, so nodes arriving from the broker land inside it and
// looked exactly like finds. They are excluded by transport, not by timing.
static uint32_t s_presetScanResultFromMs = 0;
static uint8_t  s_presetScanResultPreset = 0;
// Where that window *closes*: millis() at the moment the radio left the scanned
// preset, or 0 while it is still parked there.
//
// The second half of the same fix. s_presetScanResultFromMs is deliberately
// left set after a scan ends so the finding survives being read — but on its
// own it is an open-ended "at or after", so once the radio came home every node
// heard on this node's own preset walked straight into a list headed with
// somebody else's. The scan is a question about five minutes of air, and it has
// two ends.
static uint32_t s_presetScanResultToMs = 0;
// What the radio has to go back to. Snapshotted rather than re-derived on the
// way out: re-deriving would depend on applyPresetParams() reaching the same
// answer it reached at boot, and a restore has no business being that clever.
// It also has to be right for a node running custom modem settings, where there
// is no preset to derive anything from. Channel 0's name is in there too, both
// halves: name may point at a compiled-in literal rather than into name_buf (see
// ChannelKey, mesh_proto.h). tempTuneEnter()/tempTuneLeave() own the contents.
static TempTuneSnapshot s_presetScanSnap = {};

// Preset picker. Rows are the presets worth scanning, so a row index is not a
// preset index — s_presetPickChoices maps one to the other.
static lv_obj_t *s_presetPickBackdrop = nullptr;
static lv_obj_t *s_presetPickModal = nullptr;
static lv_obj_t *s_presetPickRows[PRESET_COUNT] = {};
static uint8_t   s_presetPickChoices[PRESET_COUNT] = {};
static int       s_presetPickCount = 0;
static int       s_presetPickSelection = 0;
#endif  // FEATURE_DISCOVERY

// True while a preset scan has the radio parked on a mesh this node does not
// live on. Everything that would otherwise carry traffic between the two reads
// it: MQTT downlink injection, MQTT uplink, flood relaying and our own periodic
// announces.
//
// The scan retunes to *listen*, and that has to be the whole of it. A relay or
// an uplink sends the foreign mesh's packets somewhere it never agreed to go;
// an injected downlink or an announce puts our side's traffic on their air, and
// then Discovery reports the mixture back as a finding on their preset.
#if FEATURE_DISCOVERY
static inline bool discoveryRadioParked() { return s_presetScanActive; }
#else
static inline bool discoveryRadioParked() { return false; }
#endif

static_assert(sizeof(((TempTuneSnapshot *)nullptr)->prevBuf) == sizeof(CHANNEL_KEYS[0].name_buf),
              "TempTuneSnapshot::prevBuf must mirror ChannelKey::name_buf");

// Parks the radio on `preset`, having snapshotted what it has to go back to.
// Shared by the Discovery preset scan and the MeshBeacon broadcast; every
// caller must pair it with tempTuneLeave(), whatever happened in between.
//
// Deliberately NOT applyPresetParams() and NOT the onWebCfgSaved() retune
// sequence, both of which write s_cfg. That would be the whole feature's
// failure mode: markConfigDirty() is called from three dozen unrelated places
// and serviceConfigFlush() writes the *entire* s_cfg to NVS 1.5 s later, so a
// brightness nudge while parked would make the temporary preset permanent.
// Same argument s_powerWifiHold settled for the low-battery Wi-Fi shed: a
// runtime hold is not a configuration change, and must not be able to become
// one. Everything below is derived locally and handed straight to the radio.
//
// renameCh0: Discovery's sweep is an ordinary channel-0 send, and the hash
// stamped on every packet is computeChannelHash(name, key, keyLen) — a sweep
// sent while channel 0 is still called LongFast carries the LongFast hash and
// the mesh we just retuned to hear will drop it. Gated on the same test every
// other path uses, so a primary channel the user deliberately renamed is left
// alone. The beacon passes false: it never goes out on channel 0 (which may
// hold a private key) and stamps the target's public hash itself.
static void tempTuneEnter(uint8_t preset, TempTuneSnapshot &snap, bool renameCh0) {
    const PresetParams &p = kPresets[preset < PRESET_COUNT ? preset : PRESET_LONG_FAST];

    snap.freq = s_cfg.loraFreq;
    snap.bw   = s_cfg.loraBw;
    snap.sf   = s_cfg.loraSf;
    snap.cr   = s_cfg.loraCr;

    snap.renamed = renameCh0 && primaryChannelNameIsPresetDefault();
    if (snap.renamed) {
        snap.prevName = CHANNEL_KEYS[0].name;
        memcpy(snap.prevBuf, CHANNEL_KEYS[0].name_buf, sizeof(snap.prevBuf));
        strncpy(CHANNEL_KEYS[0].name_buf, p.channelName,
                sizeof(CHANNEL_KEYS[0].name_buf) - 1);
        CHANNEL_KEYS[0].name_buf[sizeof(CHANNEL_KEYS[0].name_buf) - 1] = '\0';
        CHANNEL_KEYS[0].name = CHANNEL_KEYS[0].name_buf;
        recomputeChannelHashes();
    }

    // Same slot arithmetic applyPresetParams() runs, read straight out of the
    // preset table instead of through the config struct.
    const float freq = regionSlotFreq(s_cfg.region, p.bw, p.channelName);
    dropPendingRebroadcasts();   // framed for the preset we are leaving
    Radio.reconfigure(freq, p.bw, p.sf, p.cr, s_cfg.loraPower);
}

// Undoes tempTuneEnter() in reverse: channel 0's name and hashes, the relay
// queue, then the radio. Safe to call whatever the parked period did.
static void tempTuneLeave(TempTuneSnapshot &snap) {
    if (snap.renamed) {
        memcpy(CHANNEL_KEYS[0].name_buf, snap.prevBuf, sizeof(snap.prevBuf));
        CHANNEL_KEYS[0].name = snap.prevName;
        recomputeChannelHashes();
        snap.renamed = false;
    }
    dropPendingRebroadcasts();   // and the ones framed for the preset we are leaving
    if (s_radioReady) {
        Radio.reconfigure(snap.freq, snap.bw, snap.sf, snap.cr, s_cfg.loraPower);
    }
}


static lv_obj_t *s_chanCfgBackdrop = nullptr;
static lv_obj_t *s_chanCfgModal    = nullptr;
static lv_obj_t *s_chanCfgRows[MESH_CHANNELS]  = {};
static lv_obj_t *s_chanCfgNames[MESH_CHANNELS] = {};
static int       s_chanCfgSelection = 0;

// Editor rows are fixed; the enum doubles as the navigation index AND, through
// the column-major fill below, as the on-screen order — left column first, then
// right. Reordering these values moves the buttons, so keep CHAN_EDIT_LABELS in
// step with them. Laid out, that is:
//     Name  | Location
//     Enc   | Key
//     AEAD  | Hops
//     Save  |
// AEAD sits under Enc because it is part of how the channel is encrypted, and
// Save stays bottom-left where it has always been. Seven rows come to four per
// column -- the height of the channel picker's eight, so the editor fits
// wherever the picker already does.
enum ChanEditRow : uint8_t {
    CHAN_EDIT_NAME = 0,
    CHAN_EDIT_ENC,
    CHAN_EDIT_AEAD,
    CHAN_EDIT_SAVE,
    CHAN_EDIT_LOCATION,
    CHAN_EDIT_KEY,
    CHAN_EDIT_HOPS,
    CHAN_EDIT_ROW_COUNT
};

// The editor uses the picker's grid, so its rows fill column-major too and a
// step of one keeps walking down a column. Seven rows over two columns leaves
// the last cell empty rather than a gap mid-grid, which is why the build loop
// can use the same index mapping without special-casing the short column.
static constexpr int kChanEditRowsPerCol =
    (CHAN_EDIT_ROW_COUNT + kChanModalCols - 1) / kChanModalCols;

static lv_obj_t *s_chanEditBackdrop = nullptr;
static lv_obj_t *s_chanEditModal    = nullptr;
static lv_obj_t *s_chanEditRows[CHAN_EDIT_ROW_COUNT]   = {};
static lv_obj_t *s_chanEditValues[CHAN_EDIT_ROW_COUNT] = {};
static int       s_chanEditSelection = 0;
static int       s_chanEditSlot      = -1;

// Staged edits.
static char    s_chanEditName[16] = {};
static uint8_t s_chanEditKey[32]  = {};
static uint8_t s_chanEditKeyLen   = 0;
static bool    s_chanEditShareLoc = false;
static bool    s_chanEditAead     = false;
// Staged per-channel hop budget in the ChannelKey encoding: 0 = follow the
// device default, else hops + 1.
static uint8_t s_chanEditHopPlus1 = 0;

// Text-entry sub-modal, shared by the name and key fields.
static lv_obj_t *s_chanTextBackdrop = nullptr;
static lv_obj_t *s_chanTextModal    = nullptr;
static lv_obj_t *s_chanTextInput    = nullptr;
static lv_obj_t *s_chanTextKeyboard = nullptr;
static lv_obj_t *s_chanTextStatus   = nullptr;
static int       s_chanTextField    = -1;   // ChanEditRow currently being typed

static void openChanCfgModal();
static void closeChanCfgModal();
static void refreshChanCfgSelection();
static void refreshChanCfgLabels();
static void openChanEditModal(int slot);
static void closeChanEditModal();
static void refreshChanEditRows();
static void openChanTextModal(int field);
static void closeChanTextModal();
static void commitChanTextModal();
// And as the editor for every text row in the Config table (kCfgGenRows):
// which row is open is s_cfgGenOpen, and the hooks below are defined there.
static constexpr int kChanTextCfgGen = 0x43;
static const char *cfgGenTextTitle();
static uint32_t    cfgGenTextMaxLen();
static bool        cfgGenTextMasked();
static void        cfgGenTextInitial(char *out, size_t cap);
static const char *cfgGenTextCommit(const char *text);   // nullptr = accepted
static void        cfgGenTextDone();
// The text modal doubles as the Mesh Beacons message editor: this field id
// sits outside ChanEditRow, and its commit goes to s_cfg, not the channel.
static constexpr int kChanTextBeaconMessage = 0x40;
static void meshBeaconCfgMessageCommitted(const char *msg);
// And as the licensed-mode call sign / long-name suffix editor; both are staged
// in that popup and only saved by enterLicensedMode().
static constexpr int kChanTextLicCall   = 0x41;
static constexpr int kChanTextLicSuffix = 0x42;
#if FEATURE_LICENSED_MODE
static char s_licCfgCall[lic::kCallSignMax + 1] = {};
static char s_licCfgSuffix[MESH_LONG_NAME_MAX_BYTES + 1] = {};
static void licCfgTextCommitted(int field, const char *text);
#endif

// Channel-name limit. Meshtastic caps the on-air name at 11 bytes and the web
// config's input already enforces that, so the on-device editor matches rather
// than letting someone type a name that only this firmware can round-trip.
static constexpr size_t kChanNameMax = 11;

static ChanEncType chanEncTypeOf(const uint8_t *key, uint8_t keyLen) {
    if (keyLen == 0) return CHAN_ENC_NONE;
    if (keyLen == 1) return (key[0] == 0x00) ? CHAN_ENC_NONE : CHAN_ENC_DEFAULT;
    if (keyLen == 32) return CHAN_ENC_AES256;
    return CHAN_ENC_AES128;
}

// Describes what is actually stored, which is not always something the type
// cycler can produce — a key pasted through the web config or an import can be
// any length, and silently relabelling it as AES-128 would be a lie.
static void chanEncLabel(const uint8_t *key, uint8_t keyLen, char *out, size_t outLen) {
    if (keyLen == 0 || (keyLen == 1 && key[0] == 0x00)) {
        snprintf(out, outLen, TR("None"));
    } else if (keyLen == 1) {
        if (key[0] == 0x01) snprintf(out, outLen, TR("Default"));
        else                snprintf(out, outLen, TR("Default +%u"), (unsigned)key[0]);
    } else if (keyLen == 16) {
        snprintf(out, outLen, "AES-128");
    } else if (keyLen == 32) {
        snprintf(out, outLen, "AES-256");
    } else {
        snprintf(out, outLen, TR("Custom (%u B)"), (unsigned)keyLen);
    }
}

static void chanEditApplyEncType(ChanEncType t) {
    switch (t) {
        case CHAN_ENC_NONE:
            s_chanEditKey[0] = 0x00;
            s_chanEditKeyLen = 1;
            break;
        case CHAN_ENC_DEFAULT:
            s_chanEditKey[0] = 0x01;
            s_chanEditKeyLen = 1;
            break;
        case CHAN_ENC_AES128:
            // Keep a key that is already the right size: cycling past a type and
            // back must not roll a fresh key, which would silently cut off every
            // peer already provisioned with this one.
            if (s_chanEditKeyLen != 16) {
                esp_fill_random(s_chanEditKey, 16);
                s_chanEditKeyLen = 16;
            }
            break;
        case CHAN_ENC_AES256:
            if (s_chanEditKeyLen != 32) {
                esp_fill_random(s_chanEditKey, 32);
                s_chanEditKeyLen = 32;
            }
            break;
        default:
            break;
    }
}

static void chanEditCycleEncType(int delta) {
    if (lic::active()) return;   // licensed: channels carry no key
    int next = (int)chanEncTypeOf(s_chanEditKey, s_chanEditKeyLen) + delta;
    while (next < 0) next += CHAN_ENC_TYPE_COUNT;
    next %= CHAN_ENC_TYPE_COUNT;
    chanEditApplyEncType((ChanEncType)next);
    refreshChanEditRows();
}

// Slot label for the picker: the name when the slot is configured, otherwise a
// plain 1-based channel number.
static void chanSlotLabel(int slot, char *out, size_t outLen) {
    const char *nm = channelName(slot);
    if (nm && nm[0]) snprintf(out, outLen, "%s", nm);
    else             snprintf(out, outLen, TR("Channel %d"), slot);
}

// Same label with the slot index in front. A named slot otherwise gives no clue
// which of the eight it is, and in the two-column grid there is no row order to
// infer it from either.
static void chanCfgRowLabel(int slot, char *out, size_t outLen) {
    const char *nm = channelName(slot);
    if (nm && nm[0]) snprintf(out, outLen, "%d  %s", slot, nm);
    else             snprintf(out, outLen, TR("Channel %d"), slot);
}

static void chanEditSave() {
    if (s_chanEditSlot < 0 || s_chanEditSlot >= MESH_CHANNELS) return;
    ChannelKey &ck = CHANNEL_KEYS[s_chanEditSlot];

    strncpy(ck.name_buf, s_chanEditName, sizeof(ck.name_buf) - 1);
    ck.name_buf[sizeof(ck.name_buf) - 1] = '\0';
    ck.name = ck.name_buf;

    // Licensed mode: whatever was staged, the channel is saved keyless.
    if (lic::active()) {
        memset(s_chanEditKey, 0, sizeof(s_chanEditKey));
        s_chanEditKeyLen = 0;
    }
    memset(ck.key, 0, sizeof(ck.key));
    memcpy(ck.key, s_chanEditKey, s_chanEditKeyLen);
    ck.keyLen = s_chanEditKeyLen;
    ck.shareLocation = s_chanEditShareLoc;
    ck.hopLimitPlus1 = s_chanEditHopPlus1;
    // Cleared on a channel with no key, as Meshtastic's fixupChannel() does:
    // there is nothing to authenticate with, and a flag left set would switch
    // AEAD on by surprise the day a key is added.
    ck.useAead = s_chanEditAead;
    if (!channelUsesAead(ck)) ck.useAead = false;

    // Role is deliberately untouched — the web config owns it, and an empty
    // secondary slot already ships as SECONDARY, so naming one is enough to put
    // it on the air.
    recomputeChannelHashes();

    // Channel::name is bound once in ChannelMgr::init(), and the assignment above
    // may have just moved ChannelKey::name off its compiled literal.
    Channels.get(s_chanEditSlot).name = ck.name;

    persistChannelsToPrefs();

    char label[24];
    chanSlotLabel(s_chanEditSlot, label, sizeof(label));
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Saved: %s"), label);
}

static void onChanCfgRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_chanCfgSelection = idx;
    refreshChanCfgSelection();
    openChanEditModal(idx);
}

#if UI_TOUCH_ONLY_PROFILE
static void onChanCfgClosePressed(lv_event_t *e) {
    LV_UNUSED(e);
    closeChanCfgModal();
    refreshCfgModal();
}
#endif

static void onChanCfgBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_chanCfgBackdrop) return;
    closeChanCfgModal();
    refreshCfgModal();
}

static void closeChanCfgModal() {
    closeChanEditModal();
    if (lvObjValid(s_chanCfgBackdrop)) {
        lv_obj_del(s_chanCfgBackdrop);
    } else if (lvObjValid(s_chanCfgModal)) {
        lv_obj_del(s_chanCfgModal);
    }
    s_chanCfgBackdrop = nullptr;
    s_chanCfgModal = nullptr;
    memset(s_chanCfgRows, 0, sizeof(s_chanCfgRows));
    memset(s_chanCfgNames, 0, sizeof(s_chanCfgNames));
}

// Re-reads the live channel plan into the picker rows. Used after a save so the
// list agrees with what was stored, without deleting and rebuilding the modal
// from inside one of its own row event callbacks.
static void refreshChanCfgLabels() {
    if (!s_chanCfgModal) return;
    for (int i = 0; i < MESH_CHANNELS; i++) {
        if (!s_chanCfgNames[i]) continue;
        char slotText[28];
        chanCfgRowLabel(i, slotText, sizeof(slotText));
        lv_label_set_text(s_chanCfgNames[i], slotText);
    }
}

static void refreshChanCfgSelection() {
    if (!s_chanCfgModal) return;
    // Recursive: the scrollable object is the modal, two levels up from a row
    // sitting inside the grid.
    paintPickerRows(s_chanCfgRows, MESH_CHANNELS, s_chanCfgSelection, PICKER_SCROLL_RECURSIVE);
}

static void openChanCfgModal() {
    if (!s_rootScreen || s_chanCfgModal || s_chanCfgBackdrop) return;
    if (s_chanCfgSelection < 0 || s_chanCfgSelection >= MESH_CHANNELS) s_chanCfgSelection = 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 16;
    if (modalW > kChanModalMaxW) modalW = kChanModalMaxW;
    if (modalW < 120) modalW = w - 4;

    s_chanCfgBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_chanCfgBackdrop, w, h);
    lv_obj_align(s_chanCfgBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_chanCfgBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_chanCfgBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_chanCfgBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_chanCfgBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_chanCfgBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_chanCfgBackdrop, 0, 0);
    lv_obj_add_event_cb(s_chanCfgBackdrop, onChanCfgBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_chanCfgModal = lv_obj_create(s_chanCfgBackdrop);
    lv_obj_set_size(s_chanCfgModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_chanCfgModal,
                                (h > 40) ? (h - 2 * kChanModalPad) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_chanCfgModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_chanCfgModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_chanCfgModal, LV_DIR_VER);
    lv_obj_add_flag(s_chanCfgModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_chanCfgModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_chanCfgModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_chanCfgModal, 1, 0);
    lv_obj_set_style_border_color(s_chanCfgModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_chanCfgModal, kChanModalPad, 0);
    lv_obj_set_style_pad_row(s_chanCfgModal, kChanModalGap, 0);
    lv_obj_set_flex_flow(s_chanCfgModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_chanCfgModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_chanCfgBackdrop);

    lv_obj_t *title = lv_label_create(s_chanCfgModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, kChanModalTitleFont, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Channels"));
#if UI_TOUCH_ONLY_PROFILE
    // The corner X this modal has always had, now the shared one. It is a child
    // of the modal like every other: the FLOATING flag keeps it pinned to the
    // corner while the channel list scrolls underneath.
    reserveHeltecCloseXRow(title);
    appendHeltecCloseX(s_chanCfgModal, onChanCfgClosePressed);
#endif

    lv_obj_t *hint = lv_label_create(s_chanCfgModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if UI_TOUCH_ONLY_PROFILE
    lv_label_set_text(hint, TR("Tap a channel to edit"));
#else
    lv_label_set_text_fmt(hint, TR("Move  Enter=Edit  %s=Back"), modalCloseKeyLabel());
#endif

    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);

    lv_obj_t *grid = lv_obj_create(s_chanCfgModal);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, kChanModalGap, 0);
    lv_obj_set_style_pad_column(grid, kChanModalGap, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);

    for (int pos = 0; pos < MESH_CHANNELS; pos++) {
        // Children are laid out row-major, so walking the slots column-major here
        // is what puts 0-3 down the left column and 4-7 down the right.
        const int i = (pos % kChanModalCols) * kChanModalRowsPerCol + (pos / kChanModalCols);
        if (i >= MESH_CHANNELS) continue;

        lv_obj_t *row = lv_btn_create(grid);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    #endif
        s_chanCfgRows[i] = row;
        lv_obj_set_width(row, lv_pct(kChanModalCellPct));
        lv_obj_set_height(row, kChanModalRowH);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_left(row, 5, 0);
        lv_obj_set_style_pad_right(row, 5, 0);
        lv_obj_set_style_pad_top(row, 1, 0);
        lv_obj_set_style_pad_bottom(row, 1, 0);
        lv_obj_set_style_pad_column(row, 4, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(row, onChanCfgRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        s_chanCfgNames[i] = name;
        lv_obj_set_flex_grow(name, 1);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(name, kChanModalRowFont, 0);
        lv_obj_set_style_text_color(name, rowTextColor, 0);
    }

    refreshChanCfgLabels();
    // scroll_to_view() is a no-op until the tree has geometry, so resolve the
    // layout before the initial selection tries to scroll itself into view.
    lv_obj_update_layout(s_chanCfgModal);
    refreshChanCfgSelection();
}

static void onChanEditRowPressed(lv_event_t *e);

static void onChanEditBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_chanEditBackdrop) return;
    closeChanEditModal();   // discards staged edits
}

static void closeChanEditModal() {
    closeChanTextModal();
    if (lvObjValid(s_chanEditBackdrop)) {
        lv_obj_del(s_chanEditBackdrop);
    } else if (lvObjValid(s_chanEditModal)) {
        lv_obj_del(s_chanEditModal);
    }
    s_chanEditBackdrop = nullptr;
    s_chanEditModal = nullptr;
    memset(s_chanEditRows, 0, sizeof(s_chanEditRows));
    memset(s_chanEditValues, 0, sizeof(s_chanEditValues));
    s_chanEditSlot = -1;
}

static void refreshChanEditRows() {
    if (!s_chanEditModal) return;

    char encText[24];
    chanEncLabel(s_chanEditKey, s_chanEditKeyLen, encText, sizeof(encText));

    for (int i = 0; i < CHAN_EDIT_ROW_COUNT; i++) {
        lv_obj_t *row = s_chanEditRows[i];
        if (!row) continue;
        const bool sel = (i == s_chanEditSelection);
        paintPickerRow(row, sel);
        // Recursive: with the rows in a grid the scrollable object is the modal,
        // two levels up.
        if (sel) lv_obj_scroll_to_view_recursive(row, LV_ANIM_OFF);

        lv_obj_t *val = s_chanEditValues[i];
        if (!val) continue;
        switch (i) {
            case CHAN_EDIT_NAME:
                lv_label_set_text(val, s_chanEditName[0] ? s_chanEditName : "(none)");
                break;
            case CHAN_EDIT_ENC:
                lv_label_set_text(val, encText);
                break;
            case CHAN_EDIT_LOCATION:
                lv_label_set_text(val, s_chanEditShareLoc ? TR("On") : TR("Off"));
                break;
            case CHAN_EDIT_AEAD:
                lv_label_set_text(val, s_chanEditAead ? TR("On") : TR("Off"));
                break;
            case CHAN_EDIT_HOPS:
                // "Default" names where the number comes from when unset, and
                // shows it, because "this channel follows the device" is only
                // useful if you can see what the device is set to.
                if (!chanHopLimitSet(s_chanEditHopPlus1)) {
                    lv_label_set_text_fmt(val, TR("Default (%u)"), (unsigned)s_cfg.loraHopLimit);
                } else {
                    lv_label_set_text_fmt(val, "%u", (unsigned)chanHopLimitGet(s_chanEditHopPlus1));
                }
                break;
            default:
                break;
        }
    }
}

static void chanEditToggleShareLoc() {
    s_chanEditShareLoc = !s_chanEditShareLoc;
    refreshChanEditRows();
}

static void chanEditToggleAead() {
    s_chanEditAead = !s_chanEditAead;
    refreshChanEditRows();
}

// Default -> 0 -> 1 -> ... -> 7 -> Default. Nine stops rather than the handful
// of values anyone is likely to want, because a hop budget is not a preference
// list — someone setting 5 has a reason, and hiding it behind a config file
// would be worse than one extra press.
static void chanEditCycleHops(int dir) {
    const int steps = 9;                       // Default plus 0..7
    int cur = chanHopLimitSet(s_chanEditHopPlus1)
                  ? (int)chanHopLimitGet(s_chanEditHopPlus1) + 1
                  : 0;
    cur = (cur + dir + steps) % steps;
    s_chanEditHopPlus1 = (cur == 0) ? 0 : chanHopLimitMake((uint8_t)(cur - 1));
    refreshChanEditRows();
}

static void chanEditActivateRow(int row) {
    switch (row) {
        case CHAN_EDIT_NAME:
        case CHAN_EDIT_KEY:
            if (row == CHAN_EDIT_KEY && lic::active()) break;   // disabled while licensed
            openChanTextModal(row);
            break;
        case CHAN_EDIT_ENC:
            chanEditCycleEncType(1);
            break;
        case CHAN_EDIT_LOCATION:
            chanEditToggleShareLoc();
            break;
        case CHAN_EDIT_AEAD:
            chanEditToggleAead();
            break;
        case CHAN_EDIT_HOPS:
            chanEditCycleHops(1);
            break;
        case CHAN_EDIT_SAVE:
            chanEditSave();
            closeChanEditModal();
            // The picker underneath still shows the pre-save name/encryption.
            refreshChanCfgLabels();
            break;
        default:
            break;
    }
}

static void onChanEditRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_chanEditSelection = idx;
    refreshChanEditRows();
    chanEditActivateRow(idx);
}

static void openChanEditModal(int slot) {
    if (!s_rootScreen || s_chanEditModal || s_chanEditBackdrop) return;
    if (slot < 0 || slot >= MESH_CHANNELS) return;

    s_chanEditSlot = slot;
    s_chanEditSelection = CHAN_EDIT_NAME;

    // Stage the current values.
    const ChannelKey &ck = CHANNEL_KEYS[slot];
    const char *nm = ck.name_buf[0] ? ck.name_buf : (ck.name ? ck.name : "");
    snprintf(s_chanEditName, sizeof(s_chanEditName), "%s", nm);
    s_chanEditKeyLen = (ck.keyLen <= sizeof(s_chanEditKey)) ? ck.keyLen
                                                            : (uint8_t)sizeof(s_chanEditKey);
    memset(s_chanEditKey, 0, sizeof(s_chanEditKey));
    memcpy(s_chanEditKey, ck.key, s_chanEditKeyLen);
    if (lic::active()) {
        memset(s_chanEditKey, 0, sizeof(s_chanEditKey));
        s_chanEditKeyLen = 0;
    }
    s_chanEditShareLoc = ck.shareLocation;
    s_chanEditAead     = ck.useAead;
    s_chanEditHopPlus1 = ck.hopLimitPlus1;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 16;
    if (modalW > kChanModalMaxW) modalW = kChanModalMaxW;
    if (modalW < 120) modalW = w - 4;

    s_chanEditBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_chanEditBackdrop, w, h);
    lv_obj_align(s_chanEditBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_chanEditBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_chanEditBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_chanEditBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_chanEditBackdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_chanEditBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_chanEditBackdrop, 0, 0);
    lv_obj_add_event_cb(s_chanEditBackdrop, onChanEditBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_chanEditModal = lv_obj_create(s_chanEditBackdrop);
    lv_obj_set_size(s_chanEditModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_chanEditModal,
                                (h > 40) ? (h - 2 * kChanModalPad) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_chanEditModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_chanEditModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_chanEditModal, LV_DIR_VER);
    lv_obj_add_flag(s_chanEditModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_chanEditModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_chanEditModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_chanEditModal, 1, 0);
    lv_obj_set_style_border_color(s_chanEditModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_chanEditModal, kChanModalPad, 0);
    lv_obj_set_style_pad_row(s_chanEditModal, kChanModalGap, 0);
    lv_obj_set_flex_flow(s_chanEditModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_chanEditModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_chanEditBackdrop);

    char titleText[32];
    chanCfgRowLabel(slot, titleText, sizeof(titleText));
    lv_obj_t *title = lv_label_create(s_chanEditModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, kChanModalTitleFont, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_label_set_text(title, titleText);

    lv_obj_t *hint = lv_label_create(s_chanEditModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if UI_TOUCH_ONLY_PROFILE
    // Cancel/Save is on the buttons below now, so this only has to explain the
    // one gesture the buttons do not.
    lv_label_set_text(hint, TR("Tap a field to change it"));
#else
    lv_label_set_text_fmt(hint, TR("Move  Enter=Change  %s=Cancel"), modalCloseKeyLabel());
#endif

    static const char *kRowLabel[CHAN_EDIT_ROW_COUNT] = CHAN_EDIT_LABELS;
    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);

    lv_obj_t *grid = lv_obj_create(s_chanEditModal);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, kChanModalGap, 0);
    lv_obj_set_style_pad_column(grid, kChanModalGap, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);

    for (int pos = 0; pos < CHAN_EDIT_ROW_COUNT; pos++) {
        // Same column-major mapping as the picker grid, so a step of one walks
        // down a column: left holds Name/Encryption/AEAD/Save, right
        // Location/Key/Hops.
        const int i = (pos % kChanModalCols) * kChanEditRowsPerCol + (pos / kChanModalCols);
        if (i >= CHAN_EDIT_ROW_COUNT) continue;

        lv_obj_t *row = lv_btn_create(grid);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    #endif
        s_chanEditRows[i] = row;
        lv_obj_set_width(row, lv_pct(kChanModalCellPct));
        // Label and value share one line: stacked, the rows plus the header ran
        // past the bottom of both small panels.
        lv_obj_set_height(row, kChanModalRowH);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_left(row, 5, 0);
        lv_obj_set_style_pad_right(row, 5, 0);
        lv_obj_set_style_pad_top(row, 1, 0);
        lv_obj_set_style_pad_bottom(row, 1, 0);
        lv_obj_set_style_pad_column(row, 6, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(row, onChanEditRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        // Licensed: Key and Encryption do nothing, and look it.
        if (lic::active() && (i == CHAN_EDIT_KEY || i == CHAN_EDIT_ENC)) {
            lv_obj_set_style_opa(row, LV_OPA_40, 0);
        }

        lv_obj_t *label = lv_label_create(row);
        lv_obj_set_style_text_font(label, kChanModalRowFont, 0);
        lv_obj_set_style_text_color(label, rowTextColor, 0);
        lv_label_set_text(label, TR(kRowLabel[i]));

        // Key is a plain button into the text editor: its base64 runs to 44
        // characters, which no half-width cell can show, and a truncated key
        // reads as a wrong one. The editor displays it in full, which is where a
        // key gets checked anyway. Save has nothing to show either.
        if (i == CHAN_EDIT_SAVE || i == CHAN_EDIT_KEY) {
            s_chanEditValues[i] = nullptr;
            continue;
        }
        lv_obj_t *val = lv_label_create(row);
        s_chanEditValues[i] = val;
        lv_obj_set_flex_grow(val, 1);
        lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(val, rowTextColor, 0);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_text_opa(val, LV_OPA_COVER, 0);
    #else
        lv_obj_set_style_text_opa(val, LV_OPA_80, 0);
    #endif
        lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_text(val, "");
    }

#if UI_TOUCH_ONLY_PROFILE
    // Appended under the grid rather than added as a grid cell. A cell would
    // drag CHAN_EDIT_ROW_COUNT, three per-board CHAN_EDIT_LABELS copies, the
    // column-major index maths and the keyboard builds' selection walk along
    // with it — and would put a Cancel on boards that already have a close key,
    // which is not what this is for.
    //
    // Save has a twin in the grid on this board; both run the same sequence, so
    // either works. Cancel is the half that had no signposted route at all: a
    // backdrop tap discarded staged edits and nothing said so.
    appendHeltecCancelSaveRow(
        s_chanEditModal,
        [](lv_event_t *e) { LV_UNUSED(e); closeChanEditModal(); },
        [](lv_event_t *e) {
            LV_UNUSED(e);
            chanEditSave();
            closeChanEditModal();
            // The picker underneath still shows the pre-save name/encryption.
            refreshChanCfgLabels();
        });
#endif

    lv_obj_update_layout(s_chanEditModal);
    refreshChanEditRows();
}

// ── Channel field text entry ─────────────────────────────────────────────────
static void onChanTextBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_chanTextBackdrop) return;
    closeChanTextModal();
}

#if UI_TOUCH_ONLY_PROFILE
static void onChanTextOkPressed(lv_event_t *e) {
    LV_UNUSED(e);
    commitChanTextModal();
}

static void onChanTextCancelPressed(lv_event_t *e) {
    LV_UNUSED(e);
    closeChanTextModal();
}
#endif

#if defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO)
// Base64 keys are '='-padded, and neither T-Deck keyboard has an '=' key or
// shortcut — every other board can type one. Tapping this inserts it at the
// cursor, so a key can also be repaired mid-string, not just padded at the end.
static void onChanTextEqualsPressed(lv_event_t *e) {
    LV_UNUSED(e);
    if (!s_chanTextInput) return;
    lv_textarea_add_char(s_chanTextInput, '=');
}
#endif

static void closeChanTextModal() {
    if (lvObjValid(s_chanTextBackdrop)) {
        lv_obj_del(s_chanTextBackdrop);
    } else if (lvObjValid(s_chanTextModal)) {
        lv_obj_del(s_chanTextModal);
    }
    s_chanTextBackdrop = nullptr;
    s_chanTextModal = nullptr;
    s_chanTextInput = nullptr;
    s_chanTextKeyboard = nullptr;
    s_chanTextStatus = nullptr;
    s_chanTextField = -1;
}

static void commitChanTextModal() {
    if (!s_chanTextInput) return;
    const char *text = lv_textarea_get_text(s_chanTextInput);
    if (!text) text = "";

    if (s_chanTextField == kChanTextCfgGen) {
        char val[100];
        utf8util::copyTruncate(val, sizeof(val), text);   // before close frees it
        const char *err = cfgGenTextCommit(val);
        if (err) {
            if (s_chanTextStatus) lv_label_set_text(s_chanTextStatus, TR(err));
            return;
        }
        closeChanTextModal();
        cfgGenTextDone();
        return;
    }

    if (s_chanTextField == kChanTextBeaconMessage) {
        char msg[61];
        beaconCopyMessage(msg, text);   // copied before closing frees the textarea
        closeChanTextModal();
        meshBeaconCfgMessageCommitted(msg);
        return;
    }

#if FEATURE_LICENSED_MODE
    if (s_chanTextField == kChanTextLicCall || s_chanTextField == kChanTextLicSuffix) {
        const int field = s_chanTextField;
        char val[MESH_LONG_NAME_MAX_BYTES + 1];
        if (field == kChanTextLicCall) {
            // Rejected, never truncated: a clipped call sign is someone else's.
            val[0] = '\0';
            if (text[0] && !lic::normalizeCallSign(text, val, lic::kCallSignMax + 1)) {
                if (s_chanTextStatus) lv_label_set_text(s_chanTextStatus, TR("Invalid call sign"));
                return;
            }
        } else {
            utf8util::copyTruncate(val, sizeof(val), text);
        }
        closeChanTextModal();
        licCfgTextCommitted(field, val);
        return;
    }
#endif

    if (s_chanTextField == CHAN_EDIT_NAME) {
        snprintf(s_chanEditName, sizeof(s_chanEditName), "%s", text);
        if (strlen(s_chanEditName) > kChanNameMax) s_chanEditName[kChanNameMax] = '\0';
    } else if (s_chanTextField == CHAN_EDIT_KEY) {
        uint8_t decoded[32];
        const int len = base64Decode(text, decoded, (int)sizeof(decoded));
        // A key that won't decode is rejected outright: accepting a truncated one
        // would leave the channel silently unable to talk to anybody, with the
        // modal reporting success.
        if (len <= 0) {
            if (s_chanTextStatus) lv_label_set_text(s_chanTextStatus, TR("Invalid base64 key"));
            return;
        }
        memset(s_chanEditKey, 0, sizeof(s_chanEditKey));
        memcpy(s_chanEditKey, decoded, len);
        s_chanEditKeyLen = (uint8_t)len;
    }

    closeChanTextModal();
    refreshChanEditRows();
}

static void openChanTextModal(int field) {
    if (!s_rootScreen || s_chanTextModal || s_chanTextBackdrop) return;
    if (field != CHAN_EDIT_NAME && field != CHAN_EDIT_KEY && field != kChanTextBeaconMessage
        && field != kChanTextLicCall && field != kChanTextLicSuffix
        && field != kChanTextCfgGen) return;
    s_chanTextField = field;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 320) modalW = 320;
#if UI_EDGE_TO_EDGE_TEXT_ENTRY
    modalW = w;
#endif

    s_chanTextBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_chanTextBackdrop, w, h);
    lv_obj_align(s_chanTextBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_chanTextBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_chanTextBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_chanTextBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_chanTextBackdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_chanTextBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_chanTextBackdrop, 0, 0);
    lv_obj_add_event_cb(s_chanTextBackdrop, onChanTextBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_chanTextModal = lv_obj_create(s_chanTextBackdrop);
#if UI_TOUCH_ONLY_PROFILE
    lv_obj_set_size(s_chanTextModal, modalW, h - 18);
#else
    lv_obj_set_size(s_chanTextModal, modalW, LV_SIZE_CONTENT);
#endif
    lv_obj_align(s_chanTextModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_chanTextModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_chanTextModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_chanTextModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_chanTextModal, 1, 0);
    lv_obj_set_style_border_color(s_chanTextModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_chanTextModal, 8, 0);
#if UI_EDGE_TO_EDGE_TEXT_ENTRY
    lv_obj_set_style_pad_left(s_chanTextModal, 2, 0);
    lv_obj_set_style_pad_right(s_chanTextModal, 2, 0);
#endif
    lv_obj_set_style_pad_row(s_chanTextModal, 6, 0);
    lv_obj_set_flex_flow(s_chanTextModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_chanTextModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_move_foreground(s_chanTextBackdrop);

    const bool editingName   = (field == CHAN_EDIT_NAME);
    const bool editingKey    = (field == CHAN_EDIT_KEY);
    const bool editingBeacon = (field == kChanTextBeaconMessage);
    const bool editingLicCall = (field == kChanTextLicCall);
    const bool editingLicSuffix = (field == kChanTextLicSuffix);
    const bool editingCfgGen = (field == kChanTextCfgGen);

    lv_obj_t *title = lv_label_create(s_chanTextModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, editingCfgGen ? cfgGenTextTitle()
                             : editingBeacon ? TR("Beacon message")
                             : editingLicCall ? TR("Call sign")
                             : editingLicSuffix ? TR("Long name suffix")
                             : editingName ? TR("Channel Name") : TR("Channel Key"));

    s_chanTextInput = lv_textarea_create(s_chanTextModal);
    if (!s_chanTextInput) {
        logLvglMemDiag("channel text modal aborted (low LVGL mem)");
        closeChanTextModal();
        return;
    }
    lv_obj_set_width(s_chanTextInput, lv_pct(100));
    lv_obj_set_height(s_chanTextInput, 42);
    lv_obj_set_style_text_font(s_chanTextInput, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_chanTextInput, lv_color_hex(0xE8F1FF), 0);
    lv_obj_set_style_bg_color(s_chanTextInput, lv_color_hex(0x102B61), 0);
    lv_obj_set_style_bg_opa(s_chanTextInput, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_chanTextInput, 1, 0);
    lv_obj_set_style_border_color(s_chanTextInput, lv_color_hex(0x4C76BA), 0);
    lv_textarea_set_one_line(s_chanTextInput, true);
    // Characters, not bytes: commit truncates to 60 bytes on a UTF-8 boundary.
    lv_textarea_set_max_length(s_chanTextInput, editingCfgGen ? cfgGenTextMaxLen()
                                                : editingBeacon ? 60u
                                                : editingLicCall ? (uint32_t)lic::kCallSignMax
                                                : editingLicSuffix ? (uint32_t)(MESH_LONG_NAME_MAX_BYTES - 3)
                                                : editingName ? (uint32_t)kChanNameMax : 48);
    lv_textarea_set_placeholder_text(s_chanTextInput,
                                     editingKey ? TR("base64 key") : TR("Leave blank to clear"));

    if (editingCfgGen) {
        char cur[100];
        cfgGenTextInitial(cur, sizeof(cur));
        lv_textarea_set_text(s_chanTextInput, cur);
        if (cfgGenTextMasked()) lv_textarea_set_password_mode(s_chanTextInput, true);
    } else if (editingBeacon) {
        lv_textarea_set_text(s_chanTextInput, s_cfg.meshBeaconMessage);
#if FEATURE_LICENSED_MODE
    } else if (editingLicCall) {
        lv_textarea_set_text(s_chanTextInput, s_licCfgCall);
    } else if (editingLicSuffix) {
        lv_textarea_set_text(s_chanTextInput, s_licCfgSuffix);
#endif
    } else if (editingName) {
        lv_textarea_set_text(s_chanTextInput, s_chanEditName);
    } else {
        char b64[48];
        base64Encode(s_chanEditKey, s_chanEditKeyLen, b64);
        lv_textarea_set_text(s_chanTextInput, b64);
    }

    s_chanTextStatus = lv_label_create(s_chanTextModal);
    lv_obj_set_width(s_chanTextStatus, lv_pct(100));
    lv_obj_set_style_text_font(s_chanTextStatus, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_chanTextStatus, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(s_chanTextStatus, LV_TEXT_ALIGN_LEFT, 0);
#if UI_TOUCH_ONLY_PROFILE
    lv_label_set_text(s_chanTextStatus, TR("Type, then tap OK"));
#else
    // Backspace erases here on every board (and backs out of an empty field), so
    // where Esc is the close key both have to be named.
    if (kModalCloseUsesEscape) {
        lv_label_set_text_fmt(s_chanTextStatus, TR("Enter=OK  Bksp=Erase  %s=Back"),
                              modalCloseKeyLabel());
    } else {
        lv_label_set_text_fmt(s_chanTextStatus, TR("Enter=OK  %s=Erase/Back"),
                              modalCloseKeyLabel());
    }
#endif

#if defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO)
    // Key field only: the other fields have no use for a character the
    // keyboard can't produce.
    if (editingKey) {
        lv_obj_t *eqRow = lv_obj_create(s_chanTextModal);
        lv_obj_set_width(eqRow, lv_pct(100));
        lv_obj_set_height(eqRow, 30);
        lv_obj_clear_flag(eqRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(eqRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(eqRow, 0, 0);
        lv_obj_set_style_pad_all(eqRow, 0, 0);
        lv_obj_set_flex_flow(eqRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(eqRow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        lv_obj_t *eqBtn = lv_btn_create(eqRow);
        lv_obj_set_size(eqBtn, 46, 30);
        lv_obj_set_style_radius(eqBtn, 4, 0);
        lv_obj_set_style_pad_all(eqBtn, 2, 0);
        lv_obj_set_style_shadow_width(eqBtn, 0, 0);
        lv_obj_set_style_bg_color(eqBtn, lv_color_hex(0x2A4E8F), 0);
        lv_obj_set_style_bg_opa(eqBtn, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(eqBtn, onChanTextEqualsPressed, LV_EVENT_CLICKED, nullptr);

        lv_obj_t *eqLbl = lv_label_create(eqBtn);
        lv_obj_set_style_text_font(eqLbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(eqLbl, lv_color_hex(0xE8F1FF), 0);
        lv_label_set_text(eqLbl, "=");
        lv_obj_center(eqLbl);
    }
#endif

#if UI_TOUCH_ONLY_PROFILE
    lv_obj_t *btnRow = lv_obj_create(s_chanTextModal);
    lv_obj_set_width(btnRow, lv_pct(100));
    lv_obj_set_height(btnRow, 30);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 4, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancelBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(cancelBtn, 1);
    lv_obj_set_height(cancelBtn, lv_pct(100));
    lv_obj_add_event_cb(cancelBtn, onChanTextCancelPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancelLbl = lv_label_create(cancelBtn);
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(cancelLbl, TR("Cancel"));
    lv_obj_center(cancelLbl);

    lv_obj_t *okBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(okBtn, 1);
    lv_obj_set_height(okBtn, lv_pct(100));
    lv_obj_add_event_cb(okBtn, onChanTextOkPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *okLbl = lv_label_create(okBtn);
    lv_obj_set_style_text_font(okLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(okLbl, TR("OK"));
    lv_obj_center(okLbl);

    // Not with a physical keyboard attached (the T-Display P4's expansion):
    // keys typed on it reach this field through pumpKeyboardInput(), and an
    // on-screen one would only take half the panel. Checked as this opens.
    if (!keyboardAttached()) {
        s_chanTextKeyboard = lv_keyboard_create(s_chanTextModal);
        configureOnScreenKeyboard(s_chanTextKeyboard);
        lv_keyboard_set_textarea(s_chanTextKeyboard, s_chanTextInput);
    }
#endif
}

// ── Time and Date ────────────────────────────────────────────────────────────
// One modal for both halves of the question: where the clock comes from, and —
// when that answer is Manual — what to set it to. The date/time rows only exist
// in Manual mode, so Automatic stays a two-line modal.
//
// Edits are staged: nothing reaches the system clock or the saved config until
// Save, so backing out of a half-typed date leaves the running clock alone.
enum TimeCfgRow : uint8_t {
    TIME_CFG_SOURCE = 0,
    TIME_CFG_YEAR,
    TIME_CFG_MONTH,
    TIME_CFG_DAY,
    TIME_CFG_HOUR,
    TIME_CFG_MINUTE,
    TIME_CFG_ROW_COUNT
};

static lv_obj_t *s_timeCfgBackdrop = nullptr;
static lv_obj_t *s_timeCfgModal    = nullptr;
static lv_obj_t *s_timeCfgRows[TIME_CFG_ROW_COUNT]   = {};
static lv_obj_t *s_timeCfgValues[TIME_CFG_ROW_COUNT] = {};
// Carriers for the two grid lines, hidden wholesale in Automatic mode.
static lv_obj_t *s_timeCfgDateGrid = nullptr;
static lv_obj_t *s_timeCfgTimeGrid = nullptr;
static lv_obj_t *s_timeCfgHint = nullptr;
static int       s_timeCfgSelection = 0;

// Staged edits.
static uint8_t s_timeCfgSource = TIME_SOURCE_AUTO;
static int     s_timeCfgYear   = 2026;
static int     s_timeCfgMonth  = 1;
static int     s_timeCfgDay    = 1;
static int     s_timeCfgHour   = 0;
static int     s_timeCfgMinute = 0;

static void openTimeCfgModal();
static void closeTimeCfgModal();
static void refreshTimeCfgRows();

static bool timeCfgIsManual() { return s_timeCfgSource == TIME_SOURCE_MANUAL; }

