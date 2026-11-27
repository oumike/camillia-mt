// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: node info, telemetry and neighbour info announcements.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

static uint32_t announceIntervalMs(uint32_t intervalS) {
    if (intervalS == 0) return 0;
    // announceDue() compares as a signed difference, so a deadline more than
    // 2^31 ms ahead would read as already passed.
    if (intervalS >= (0x7FFFFFFFUL / 1000UL)) return 0x7FFFFFFFUL;
    uint32_t ms = intervalS * 1000UL;
    return (ms < 1000UL) ? 1000UL : ms;
}

static bool announceDue(uint32_t nowMs, uint32_t nextMs, uint32_t intervalS) {
    if (intervalS == 0) return false;
    if (nextMs == 0) return true;
    return (int32_t)(nowMs - nextMs) >= 0;
}

// ── Stationary position cadence ───────────────────────────────
// Meshtastic 2.8 turns position dedup on by default: a receiver drops a repeat
// position that lands in the same ~90 m cell within 5 hours
// (installTrafficManagementDefaults(), 19-bit grid). Upstream also halved its
// own stationary broadcast floor to 6 h. On our 30-minute default a node
// sitting on a desk therefore spends airtime on eleven of every twelve position
// broadcasts, each one discarded downstream without a trace.
//
// So a position that has not left its cell since the last one we sent is
// deferred to the floor below rather than repeated on the configured interval.
// Movement is what re-arms the normal cadence: the first fix outside the cell
// sends immediately. The configured interval still wins when it is *longer*
// than the floor — this only ever slows a stationary node down, and a user who
// asked for 12 hours keeps 12 hours.
#define POSITION_DEDUP_PRECISION_BITS 19
#define POSITION_STATIONARY_MIN_S     (6UL * 3600UL)

static bool     s_lastPosSentValid = false;
static int32_t  s_lastPosSentLatI  = 0;
static int32_t  s_lastPosSentLonI  = 0;

// Same truncation applyPositionPrecision() uses, at the dedup grid's width, so
// "same cell" here means the same cell the receiver will compute.
static bool positionWithinDedupCell(int32_t latI, int32_t lonI) {
    if (!s_lastPosSentValid) return false;
    const uint32_t keep = (uint32_t)0xFFFFFFFFu << (32 - POSITION_DEDUP_PRECISION_BITS);
    return ((uint32_t)latI & keep) == ((uint32_t)s_lastPosSentLatI & keep) &&
           ((uint32_t)lonI & keep) == ((uint32_t)s_lastPosSentLonI & keep);
}

static void scheduleAnnounceNext(uint32_t &nextMs, uint8_t &fails,
                                 uint32_t nowMs, uint32_t intervalS) {
    fails = 0;
    uint32_t intervalMs = announceIntervalMs(intervalS);
    if (intervalMs == 0) {
        nextMs = 0;
        return;
    }
    nextMs = nowMs + intervalMs;
    if (nextMs == 0) nextMs = 1;   // 0 means "due now" to announceDue()
}

// Exponential backoff, capped both by a hard ceiling and by the announce's own
// interval — retrying more often than the thing would normally be sent is never
// useful.
//
// This was a flat 5 s with no attempt counter, which is what turned a radio that
// cannot transmit into a permanently sluggish device rather than an occasional
// hitch: MeshRadio::transmit() blocks the main loop for the full airtime across
// its internal attempts (~0.85 s per packet on LongFast, up to ~3.5 s if all
// four attempts fail), and a failing announce re-entered that every five seconds
// for the rest of the session. Backing off means a dead TX path settles at
// costing no more loop time than a working one. See issue #53.
static constexpr uint32_t kAnnounceRetryBaseMs = 5000UL;
static constexpr uint8_t  kAnnounceRetryMaxShift = 6;      // 5, 10, 20, 40, 80, 160 s

static void scheduleAnnounceRetry(uint32_t &nextMs, uint8_t &fails,
                                  uint32_t nowMs, uint32_t intervalS) {
    if (fails < kAnnounceRetryMaxShift) fails++;
    uint32_t delayMs = kAnnounceRetryBaseMs << (fails - 1);
    uint32_t intervalMs = announceIntervalMs(intervalS);
    if (intervalMs != 0 && delayMs > intervalMs) delayMs = intervalMs;
    nextMs = nowMs + delayMs;
}

static bool resolveAnnouncePosition(int32_t &latI, int32_t &lonI, int32_t &altM) {
    // The one place every source of our coordinates converges, which is why the
    // switch lives here rather than beside the scheduler: turning it off has to
    // silence the live GPS fix, the last-known self position and the configured
    // fixed coordinates alike, and it has to hold for the manual announce, which
    // bypasses the broadcast interval entirely.
    if (!s_cfg.shareLocation) return false;

    if (gpsIsEnabled() && gpsHasFix()) {
        latI = gpsLatI();
        lonI = gpsLonI();
        altM = gpsAltM();
        return true;
    }

    NodeEntry *self = Nodes.find(s_myNodeId);
    if (self && self->hasPosition && (self->latI != 0 || self->lonI != 0)) {
        latI = self->latI;
        lonI = self->lonI;
        altM = self->alt;
        return true;
    }

    if (s_cfg.latI != 0 || s_cfg.lonI != 0) {
        latI = s_cfg.latI;
        lonI = s_cfg.lonI;
        altM = s_cfg.alt;
        return true;
    }

    return false;
}

// A position announce is one packet per location-sharing channel, and at SF11
// each transmit blocks the main loop for the better part of a second. Sending
// all of them in one pass is what put ann:nodeinfo at 6.3 s on an M9 with eight
// sharing channels: tx:nodeinfo accounted for 1.0 s of it and the sweep for the
// other 5.3 s, during which the web server reported the socket unserviced for
// seven seconds and the UI and keyboard queued behind it.
//
// So the sweep runs one channel per loop pass. The total airtime is unchanged —
// that belongs to the radio — but it is paid in ~0.7 s slices with everything
// else serviced in between, instead of one five-second stall.
//
// And not on consecutive passes either: a pass is ~40 ms, so the sweep was
// still one solid block of transmitting -- eleven sharing channels kept a
// Tanmatsu deaf for 7.5 s, and two messages sent meanwhile were never heard,
// relayed copies included. A gap between packets (kPosSweepGapMs) leaves the
// radio listening between them, long enough for a message or a relay of one.
//
// The position is snapshotted when the sweep starts rather than re-read per
// channel: one announce should report one position, not a different fix to each
// channel as the device moves.
//
// The gap is shared by every announce -- node info, telemetry and neighbor
// info as well as the sweep -- since at boot they all fall due together and
// went out as one more back-to-back block (2.9 s, which cost a message too).
// Anything due while the gate is closed simply stays due for a later pass.
static constexpr uint32_t kAnnounceGapMs = 3000;
static constexpr uint32_t kAnnounceJitterMs = 1000;   // so nodes don't announce in step
static uint32_t s_announceQuietUntilMs = 0;
static inline bool announceGateOpen(uint32_t nowMs) {
    return (int32_t)(nowMs - s_announceQuietUntilMs) >= 0;
}
static inline void announceGateClose() {
    s_announceQuietUntilMs = millis() + kAnnounceGapMs + (uint32_t)random(kAnnounceJitterMs);
}
struct PosAnnounceSweep {
    bool     active;
    int      nextChan;
    int32_t  latI, lonI, altM;
    int      attempted, sent;
    bool     forced;      // an explicit Announce press, for the notice below
};
static PosAnnounceSweep s_posSweep = {};

// Anchors the dedup cell and schedules the next announce. Split out because the
// sweep now finishes on a later pass than the one that started it.
static void finishPositionAnnounce(uint32_t nowMs) {
    if (s_posSweep.attempted == 0 && s_posSweep.forced) {
        // Sharing is on and we have coordinates, but no channel is set to carry
        // them — otherwise the announce button looks broken. Reported here
        // rather than at the start, because "how many channels shared" is only
        // known once the sweep has walked them.
        liveFeedAddPrefixed("", "[position] skip: no channel shares location",
                            TFT_DARKGREY, 0, false);
    }
    if (s_posSweep.sent > 0 || s_posSweep.attempted == 0) {
        // Anchor the cell to what actually went out, not to the fix we read:
        // anchoring on a failed send would start the stationary timer against a
        // position no one received.
        if (s_posSweep.sent > 0) {
            s_lastPosSentValid = true;
            s_lastPosSentLatI  = s_posSweep.latI;
            s_lastPosSentLonI  = s_posSweep.lonI;
        }
        scheduleAnnounceNext(s_nextPositionTxMs, s_positionTxFails, nowMs, s_cfg.posIntervalS);
    } else {
        scheduleAnnounceRetry(s_nextPositionTxMs, s_positionTxFails, nowMs, s_cfg.posIntervalS);
    }
    s_posSweep.active = false;
}

static void serviceNodeInfoAnnounce(uint32_t nowMs) {
    // A sweep already under way finishes, ahead of every gate below. Those gates
    // decide whether to *start* an announce; this one is already committed, and
    // abandoning it half-sent would leave some channels told and others not.
    if (s_posSweep.active) {
        if (!s_radioReady) { s_posSweep.active = false; return; }
        while (s_posSweep.nextChan < MESH_CHANNELS
               && !channelSharesLocation(s_posSweep.nextChan)) {
            s_posSweep.nextChan++;
        }
        if (s_posSweep.nextChan >= MESH_CHANNELS) {
            finishPositionAnnounce(nowMs);
            return;
        }
        if (!announceGateOpen(nowMs)) return;   // listening between announces
        const int chan = s_posSweep.nextChan++;
        s_posSweep.attempted++;
        bool ok = false;
        LOOP_PHASE("tx:position", ok = Channels.sendPosition(
                       s_myNodeId, s_posSweep.latI, s_posSweep.lonI, s_posSweep.altM,
                       s_cfg.okToMqtt, chan, s_cfg.positionPrecision));
        if (ok) s_posSweep.sent++;
        announceGateClose();
        return;   // one packet per pass, whatever else is due
    }

    bool forceAnnounce = webCfgAnnounceRequested();
    // An explicit Announce press is a deliberate instruction and still goes
    // out; a button that silently did nothing would read as broken.
    if (announceHeldForOnboarding() && !forceAnnounce) return;
    // Same exemption as onboarding above: a button the user just pressed is an
    // instruction, and waiting on it would read as broken.
    if (announceHeldForWebCfg(nowMs) && !forceAnnounce) return;

    // Licensed: station ID every 10 min, a regulatory interval that bypasses
    // the configured one (and any floor); nodeInfoIntervalS itself is untouched.
    const uint32_t nodeInfoIntervalS = lic::active() ? lic::kStationIdIntervalS
                                                     : s_cfg.nodeInfoIntervalS;
    bool nodeInfoDue = forceAnnounce || announceDue(nowMs, s_nextNodeInfoTxMs, nodeInfoIntervalS);
    bool positionDue = forceAnnounce || announceDue(nowMs, s_nextPositionTxMs, s_cfg.posIntervalS);

    if (!nodeInfoDue && !positionDue) return;

    if (!s_radioReady) {
        if (forceAnnounce) webCfgQueueAnnounce();
        return;
    }

    if (nodeInfoDue && !announceGateOpen(nowMs)) return;   // stays due
    if (nodeInfoDue) {
        // Its own phase, like tx:telemetry below. Radio.transmit() blocks for
        // the packet's airtime -- over a second at SF11 -- and without this the
        // whole cost landed under ann:nodeinfo with no way to tell the transmit
        // apart from the work around it. A boot log showing ann:nodeinfo at
        // 6.3 s against one packet of air is exactly the question this answers.
        bool ok = false;
        LOOP_PHASE("tx:nodeinfo", ok = Channels.sendNodeInfo(s_myNodeId,
                                        s_cfg.nodeLong,
                                        s_cfg.nodeShort,
                                        0xFFFFFFFF,
                                        false,
                                        s_cfg.okToMqtt));
        announceGateClose();
        if (ok) scheduleAnnounceNext(s_nextNodeInfoTxMs, s_nodeInfoTxFails, nowMs, nodeInfoIntervalS);
        else scheduleAnnounceRetry(s_nextNodeInfoTxMs, s_nodeInfoTxFails, nowMs, nodeInfoIntervalS);
    }

    // The other half of ann:nodeinfo. When both are due in the same pass -- as
    // they are on the first announce after a boot -- the phase pays for two
    // transmits, which is the first thing to rule out when it reads long.
    if (positionDue) {
        int32_t latI = 0;
        int32_t lonI = 0;
        int32_t altM = 0;
        if (resolveAnnouncePosition(latI, lonI, altM)) {
            // Still in the cell a receiver would dedup us into, so the packet
            // would be discarded on arrival. Defer rather than spend the
            // airtime. An explicit Announce press is exempt, as everywhere else
            // here: the user asked, and 2.8 drops it silently at the far end
            // rather than reporting anything, so a press that looked like it did
            // nothing would be indistinguishable from a fault.
            if (!forceAnnounce && positionWithinDedupCell(latI, lonI) &&
                s_cfg.posIntervalS < POSITION_STATIONARY_MIN_S) {
                scheduleAnnounceNext(s_nextPositionTxMs, s_positionTxFails, nowMs,
                                     POSITION_STATIONARY_MIN_S);
                return;
            }

            // One announce, one packet per sharing channel — handed to the
            // sweep above, which sends them one loop pass apart. Rescheduling
            // still happens once, when the last channel is done: a retry
            // resends to all of them, so letting a single failed channel force
            // one would keep re-announcing to the channels that already heard
            // us.
            s_posSweep.active    = true;
            s_posSweep.nextChan  = 0;
            s_posSweep.latI      = latI;
            s_posSweep.lonI      = lonI;
            s_posSweep.altM      = altM;
            s_posSweep.attempted = 0;
            s_posSweep.sent      = 0;
            s_posSweep.forced    = forceAnnounce;
        } else {
            scheduleAnnounceNext(s_nextPositionTxMs, s_positionTxFails, nowMs, s_cfg.posIntervalS);
            if (forceAnnounce) {
                // "Off" and "nothing to send" look identical from the outside,
                // and the announce button is exactly where someone checks.
                liveFeedAddPrefixed("",
                                    s_cfg.shareLocation ? "[position] skip: no fix/fallback"
                                                        : "[position] skip: location sharing off",
                                    TFT_DARKGREY, 0, false);
            }
        }
    }

    // Manual announce should also push telemetry immediately (when enabled).
    // serviceTelemetryAnnounce() runs later in the loop and will transmit now
    // because nextTx=0 means "due immediately".
    if (forceAnnounce) {
        if (s_cfg.telDeviceEnabled) s_nextDeviceTelemetryTxMs = 0;
#if HAS_ENV_SENSOR_TELEMETRY
        if (s_cfg.telEnvEnabled) s_nextEnvTelemetryTxMs = 0;
#endif
        if (s_cfg.neighborInfoEnabled && s_cfg.neighborInfoOverLora) {
            s_nextNeighborInfoTxMs = 0;
        }
    }
}

static void serviceTelemetryAnnounce(uint32_t nowMs) {
    if (announceHeldForOnboarding()) return;   // see announceHeldForOnboarding()
    bool forceTelemetry = webCfgTelemetryRequested();
    if (announceHeldForWebCfg(nowMs) && !forceTelemetry) return;

    bool devDue = forceTelemetry || (s_cfg.telDeviceEnabled
        && announceDue(nowMs, s_nextDeviceTelemetryTxMs, s_cfg.telDeviceIntervalS));

#if HAS_ENV_SENSOR_TELEMETRY
    bool envDue = s_cfg.telEnvEnabled
        && (forceTelemetry || announceDue(nowMs, s_nextEnvTelemetryTxMs, s_cfg.telEnvIntervalS));
#else
    bool envDue = false;
#endif

    if (!devDue && !envDue) return;
    if (!s_radioReady) {
        if (forceTelemetry) webCfgQueueTelemetry();
        return;
    }
    if (!announceGateOpen(nowMs)) return;   // stays due; see kAnnounceGapMs

    if (devDue) {
        bool ok = false;
        LOOP_PHASE("tx:telemetry", ok = Channels.sendTelemetryDevice(s_myNodeId, s_cfg.okToMqtt));
        announceGateClose();
        if (ok) scheduleAnnounceNext(s_nextDeviceTelemetryTxMs, s_deviceTelemetryTxFails, nowMs, s_cfg.telDeviceIntervalS);
        else scheduleAnnounceRetry(s_nextDeviceTelemetryTxMs, s_deviceTelemetryTxFails, nowMs, s_cfg.telDeviceIntervalS);
    }

#if HAS_ENV_SENSOR_TELEMETRY
    // Device telemetry may just have closed the gate; this stays due if so.
    if (envDue && !announceGateOpen(millis())) return;
    if (envDue) {
        // Timed separately: this is the probe sweep, and the first measurement
        // that covered serviceTelemetryAnnounce() as a whole read 20 s, which is
        // far more than a LoRa transmit can account for.
        bool hasSensor = false;
        LOOP_PHASE("env:probe", hasSensor = envHasSensor() || envBegin());
        if (!hasSensor) {
            // A sensor that was slow to come up at boot is worth retrying sooner
            // than the full telemetry interval. A sensor that is not there is
            // not: once envBegin() has exhausted its sweeps it will never
            // succeed this session, so fall back to the configured interval
            // rather than waking every 30 s forever. See issue #53.
            if (envProbeExhausted()) {
                scheduleAnnounceNext(s_nextEnvTelemetryTxMs, s_envTelemetryTxFails,
                                     nowMs, s_cfg.telEnvIntervalS);
            } else {
                s_nextEnvTelemetryTxMs = nowMs + 30000UL;
            }
            return;
        }

        EnvReading env = {};
        if (!envRead(env)) {
            // Detected but unreadable is a different fault from never detected,
            // and until now both looked identical from outside: the announce
            // just quietly rescheduled.
            Serial.printf("[env] read failed (%s via %s)\n",
                          envSensorName(), envProbeRouteName());
            scheduleAnnounceRetry(s_nextEnvTelemetryTxMs, s_envTelemetryTxFails, nowMs, s_cfg.telEnvIntervalS);
            return;
        }

        // Hourly at most, so this is not chatty — and it is the only way to tell
        // "the sensor is reporting" from "the sensor is reporting zeroes".
        Serial.printf("[env] read %.1fC %.1f%% %.1fhPa via %s\n",
                      (double)env.temperatureC, (double)env.humidityPct,
                      (double)env.pressureHpa, envProbeRouteName());

        bool ok = Channels.sendTelemetryEnvironment(s_myNodeId,
                                                    env.temperatureC,
                                                    env.humidityPct,
                                                    env.pressureHpa,
                                                    s_cfg.okToMqtt);
        announceGateClose();
        if (ok) scheduleAnnounceNext(s_nextEnvTelemetryTxMs, s_envTelemetryTxFails, nowMs, s_cfg.telEnvIntervalS);
        else scheduleAnnounceRetry(s_nextEnvTelemetryTxMs, s_envTelemetryTxFails, nowMs, s_cfg.telEnvIntervalS);
    }
#endif
}

// ── Auto-favorite nearby nodes ───────────────────────────────────────────────
// Great-circle distance in meters between two positions in 1e7-scaled degrees.
static float geoDistanceM(int32_t latI1, int32_t lonI1, int32_t latI2, int32_t lonI2) {
    const float kDegToRad = 0.017453292519943295f;
    const float lat1 = (float)latI1 * 1e-7f * kDegToRad;
    const float lat2 = (float)latI2 * 1e-7f * kDegToRad;
    const float dLat = lat2 - lat1;
    const float dLon = ((float)lonI2 - (float)lonI1) * 1e-7f * kDegToRad;
    float a = sinf(dLat * 0.5f); a *= a;
    float b = sinf(dLon * 0.5f); b *= b;
    float h = a + cosf(lat1) * cosf(lat2) * b;
    if (h < 0.0f) h = 0.0f;
    if (h > 1.0f) h = 1.0f;
    return 2.0f * 6371000.0f * asinf(sqrtf(h));
}

// Positions change slowly and each promotion writes NVS, so a lazy sweep is
// plenty; it also re-runs as our own position moves, not just on new packets.
static const uint32_t kAutoFavIntervalMs = 30000;
static uint32_t s_lastAutoFavMs = 0;

// Favorite any node whose last known position is within the configured radius.
// Opt-in and additive only: it never un-favorites, because a node drifting out
// of range (or simply going quiet) must not silently clear something the user
// pinned by hand — and favorites are what protect a node from eviction.
static void serviceAutoFavorite(uint32_t nowMs) {
    if (!s_cfg.autoFavoriteEnabled || s_cfg.autoFavoriteRangeM == 0) return;
    // On CLIENT_BASE a favorite is a routing decision -- whose traffic this node
    // always relays -- so it is the owner's to make, never a radius's. Upstream
    // refuses to auto-favorite on this role for the same reason. The setting is
    // left alone, and applies again if the role changes back.
    if (s_cfg.deviceRole == relay::ROLE_CLIENT_BASE) return;
    if (s_lastAutoFavMs != 0 && (uint32_t)(nowMs - s_lastAutoFavMs) < kAutoFavIntervalMs) return;
    s_lastAutoFavMs = nowMs;

    // Prefer a live fix; fall back to the last known / manually set position.
    int32_t myLat, myLon;
    if (gpsIsEnabled() && gpsHasFix()) {
        myLat = gpsLatI();
        myLon = gpsLonI();
    } else {
        myLat = s_cfg.latI;
        myLon = s_cfg.lonI;
    }
    if (myLat == 0 && myLon == 0) return;   // our own position is unknown

    const float rangeM = (float)s_cfg.autoFavoriteRangeM;
    const int count = Nodes.count();
    // Nodes.at() rather than getByRank(): setFavorite() below marks the ranking
    // dirty, so the next getByRank() would re-sort and float the just-promoted
    // node to the top — shuffling the table underneath this loop and skipping
    // entries. at() is storage order and never moves.
    for (int i = 0; i < count; i++) {
        NodeEntry *e = Nodes.at(i);
        if (!e || e->nodeId == 0 || e->favorite) continue;
        if (e->nodeId == s_myNodeId) continue;
        if (!e->hasPosition) continue;
        if (e->latI == 0 && e->lonI == 0) continue;

        const float d = geoDistanceM(myLat, myLon, e->latI, e->lonI);
        if (d <= rangeM) {
            if (Nodes.setFavorite(e->nodeId, true)) {
                Serial.printf("[autofav] favorited !%08lx at %.0f m (limit %.0f m)\n",
                              (unsigned long)e->nodeId, (double)d, (double)rangeM);
            }
        }
    }
}

static void serviceNeighborInfoAnnounce(uint32_t nowMs) {
    if (announceHeldForOnboarding()) return;   // see announceHeldForOnboarding()
    if (announceHeldForWebCfg(nowMs)) return;  // see announceHeldForWebCfg()
    bool due = s_cfg.neighborInfoEnabled
        && s_cfg.neighborInfoOverLora
        && announceDue(nowMs, s_nextNeighborInfoTxMs, s_cfg.neighborInfoIntervalS);

    if (!due) return;
    if (!s_radioReady) return;

    NeighborEdgeInfo neighbors[MESH_NEIGHBOR_MAX] = {};
    size_t neighborCount = 0;
    int totalNodes = Nodes.count();
    for (int rank = 0; rank < totalNodes && neighborCount < MESH_NEIGHBOR_MAX; rank++) {
        NodeEntry *entry = Nodes.getByRank(rank);
        if (!entry || entry->nodeId == 0 || entry->nodeId == s_myNodeId) continue;
        // Same definition of "direct" that Discovery renders, and for the same
        // reason: without hop_start, "0 hops" is just an absent field, and
        // advertising those puts links on other people's maps that may not exist.
        if (!nodeIsDirectNeighbor(*entry)) continue;

        neighbors[neighborCount].nodeId = entry->nodeId;
        neighbors[neighborCount].snr = entry->snr;
        neighborCount++;
    }

    if (neighborCount == 0) {
        scheduleAnnounceNext(s_nextNeighborInfoTxMs, s_neighborInfoTxFails, nowMs, s_cfg.neighborInfoIntervalS);
        return;
    }

    if (!announceGateOpen(nowMs)) return;   // stays due; see kAnnounceGapMs
    bool ok = Channels.sendNeighborInfo(s_myNodeId,
                                        s_cfg.neighborInfoIntervalS,
                                        neighbors,
                                        neighborCount,
                                        s_cfg.okToMqtt);
    announceGateClose();
    if (ok) scheduleAnnounceNext(s_nextNeighborInfoTxMs, s_neighborInfoTxFails, nowMs, s_cfg.neighborInfoIntervalS);
    else scheduleAnnounceRetry(s_nextNeighborInfoTxMs, s_neighborInfoTxFails, nowMs, s_cfg.neighborInfoIntervalS);
}

// ── MQTT map report ──────────────────────────────────────────────────────────
// The odd one out among the announce services above: nothing here touches the
// radio. A map report is a node describing itself directly to the broker — name,
// hardware, firmware, region, preset and a coarsened position — so that an
// MQTT-fed map can show it without waiting to overhear its traffic.
//
// Fixed cadence rather than a config field. Upstream defaults to 15 minutes and
// treats it as a floor for public brokers; a node that re-announced itself every
// few seconds would be rate-limited or blocked, and there is nothing here a user
// gains by tuning it.
static constexpr uint32_t kMapReportIntervalS = 900UL;

// How recently a node must have been heard to count as online, matching
// Meshtastic's own two-hour window. Nodes that arrived over the MQTT downlink
// are counted too — the table does not record how a node was heard, so "local"
// here means "in our node table", which on a device without a downlink-enabled
// channel is the same thing.
static constexpr uint32_t kMapReportOnlineWindowMs = 2UL * 60UL * 60UL * 1000UL;

static uint32_t mapReportOnlineLocalNodes(uint32_t nowMs) {
    uint32_t online = 1;   // ourselves: the reporting node is part of its own count
    const int count = Nodes.count();
    for (int i = 0; i < count; i++) {
        const NodeEntry *e = Nodes.at(i);
        if (!e || e->nodeId == 0 || e->nodeId == s_myNodeId) continue;
        // 0 is "not heard since boot", not "heard at time zero".
        if (e->lastHeardMs == 0) continue;
        if ((uint32_t)(nowMs - e->lastHeardMs) > kMapReportOnlineWindowMs) continue;
        online++;
    }
    return online;
}

static void serviceMapReport(uint32_t nowMs) {
    if (!s_cfg.mqttEnabled || !s_cfg.mqttMapReport) return;
    // Held while a preset scan has the radio parked, for the same reason the
    // uplink is: the report is stamped with the primary channel's name, and
    // channel 0 is wearing the scanned preset's name for the duration. It would
    // tell the map this node lives on a preset it is only visiting. The interval
    // is not consumed here, so it goes out as soon as the radio is home.
    if (discoveryRadioParked()) return;
    if (!announceDue(nowMs, s_nextMapReportMs, kMapReportIntervalS)) return;

    // Not connected yet: come back on the next tick rather than burning the
    // interval, so enabling the bridge does not mean a 15-minute wait.
    if (!mqttBridgeConnected()) {
        scheduleAnnounceRetry(s_nextMapReportMs, s_mapReportFails, nowMs, kMapReportIntervalS);
        return;
    }

    MapReportInfo info = {};
    info.longName        = s_cfg.nodeLong;
    info.shortName       = s_cfg.nodeShort;
    info.firmwareVersion = APP_VERSION;
    info.role            = s_cfg.deviceRole;
    info.region          = regionCodeToMeshtastic(s_cfg.region);
    // Reported even when custom modem settings are in force: modemPreset is
    // still what the config holds, and upstream reports the same field the same
    // way. Long Turbo is ours alone and has no value in their enum, so it goes
    // out as 0 — which a reader shows as Long Fast, the proto3 default. There is
    // no "unknown" to send instead, and the alternative is naming some other
    // preset we are definitely not running.
    {
        const int preset = presetToMeshtastic(s_cfg.modemPreset);
        info.modemPreset = (preset >= 0) ? (uint8_t)preset : 0;
    }
    // "Default channel" in the sense the map cares about: the primary still
    // carries the stock one-byte PSK, so anyone can read its traffic.
    info.hasDefaultChannel = (CHANNEL_KEYS[0].keyLen == 1 && CHANNEL_KEYS[0].key[0] == 0x01);
    info.onlineLocalNodes  = mapReportOnlineLocalNodes(nowMs);

    int32_t latI = 0, lonI = 0, altM = 0;
    if (resolveAnnouncePosition(latI, lonI, altM)) {
        // Coarsened by the same transform as the RF position, and never finer
        // than the public ceiling. A map report goes to a public map whatever
        // the channel key is, so unlike the RF clamp this one is unconditional:
        // upstream constrains its map precision to 12..15 and rejects anything
        // outside that (MQTT.cpp), where the 15 is the same ceiling D2 applies
        // on air. Only ever coarsens — a user who asked for less precision than
        // 15 keeps it, because raising it toward the ceiling would publish more
        // than they agreed to, not less.
        uint8_t bits = positionPrecisionCoerce(s_cfg.positionPrecision);
        if (bits > MESH_MAX_POSITION_PRECISION_PUBLIC) {
            bits = MESH_MAX_POSITION_PRECISION_PUBLIC;
        }
        applyPositionPrecision(latI, lonI, bits);
        info.hasPosition       = true;
        info.latI              = latI;
        info.lonI              = lonI;
        info.alt               = altM;
        info.positionPrecision = bits;
    }

    // Rescheduled before the early return, not after it: without a position
    // there is nothing to map, and a return that left the timer alone would log
    // this line on every pass through the loop.
    scheduleAnnounceNext(s_nextMapReportMs, s_mapReportFails, nowMs, kMapReportIntervalS);
    if (!info.hasPosition) {
        Serial.println("[mqtt] map report skipped: no position "
                       "(location sharing off, or no fix and no fixed position)");
        return;
    }

    const ChannelKey &primary = CHANNEL_KEYS[0];
    const char *chanName = primary.name_buf[0] ? primary.name_buf : primary.name;
    (void)mqttBridgePublishMapReport(info, chanName);
}

