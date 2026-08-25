#pragma once

#include <stdint.h>

// ── Terrain-aware line of sight ──────────────────────────────────────────────
// Ported from the wadamesh MeshCore port's "Sightline" feature.
//
// Samples the great circle between this node and a contact, fetches ground
// elevation for each sample, adds the earth-curvature "bulge" under a
// 4/3-earth-radius radio model, and compares the result against a straight
// antenna-to-antenna sight line. Fresnel-zone clearance (0.6·F1) is what
// separates a clear path from a marginal one.
//
// The elevation data comes from an operator-supplied HTTP endpoint — see
// losRequest(). This firmware has no TLS client, so that URL must be plain
// http://. wadamesh has the same constraint and solves it the same way.

static constexpr int kLosSamples = 24;

enum LosVerdict : uint8_t {
    LOS_CLEAR = 0,
    LOS_MARGINAL,      // sight line clears the ground but cuts into 0.6·F1
    LOS_BLOCKED,
};

enum LosState : uint8_t {
    LOS_IDLE = 0,
    LOS_FETCHING,
    LOS_OK,
    LOS_ERR_NO_SERVER,   // no elevation server configured
    LOS_ERR_NO_WIFI,
    LOS_ERR_HTTP,        // transport/status failure; see losHttpCode()
    LOS_ERR_BADREPLY,    // 200, but the body was not an elevation CSV at all
    LOS_ERR_DATA,        // reply parsed but too few real samples to mean anything
};

struct LosAnalysis {
    LosVerdict verdict;
    double distanceKm;
    double bearingDeg;
    const char *compass;      // "NE" etc, static storage
    double minClearM;         // tightest clearance; negative means blocked
    double worstD1M;          // distance along the path to that point
    double worstF1M;          // first Fresnel radius there
    double freqMhz;
    float  antSelfM;
    float  antPeerM;
    int    worstIdx;
    // Terrain + curvature bulge per sample, metres. What the modal plots.
    float  terrainM[kLosSamples];
    double h0M;               // our antenna, metres above sea level
    double hNM;               // peer antenna, same
};

// Kicks off an elevation fetch on a worker task. Returns false and sets the
// state when it cannot start (no server, no Wi-Fi, already running).
//
// `server` is the base URL of an endpoint answering
//   GET <server>/elev?locations=lat,lon|lat,lon|...  ->  CSV of metres
// exactly as wadamesh's tile proxy does. "null" is accepted for no-data points.
bool losRequest(const char *server,
                double selfLat, double selfLon,
                double peerLat, double peerLon,
                double freqMhz);

LosState losState();
int      losHttpCode();     // last HTTP status, or a negative internal code
void     losReset();        // back to LOS_IDLE, for closing the modal

// Recomputes the verdict from the fetched profile. Cheap and network-free, so
// it can be called again whenever an input like antenna height changes.
// Returns false unless losState() == LOS_OK.
bool losAnalyze(float antSelfM, float antPeerM, LosAnalysis &out);
