#pragma once
// Pure helpers for the wardrive log (wardrive_log.cpp). No Arduino types, so
// they compile and are tested on the host: tools/test_wardrive_util.cpp.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

// Great-circle distance in metres between two degrees*1e7 points. Equirectangular
// approximation: within a fraction of a percent at the tens-of-metres scale the
// throttle works at, and no trig beyond one cos().
static inline float wardriveDistanceM(int32_t lat1I, int32_t lon1I,
                                      int32_t lat2I, int32_t lon2I) {
    const double kDeg = 1e-7 * 3.14159265358979323846 / 180.0;
    const double lat1 = lat1I * kDeg, lat2 = lat2I * kDeg;
    const double dLat = (double)(lat2I - lat1I) * kDeg;
    const double dLon = (double)((int64_t)lon2I - (int64_t)lon1I) * kDeg;
    const double x = dLon * cos((lat1 + lat2) * 0.5);
    return (float)(6371000.0 * sqrt(x * x + dLat * dLat));
}

// Whether a node last logged `sinceMs` ago at (lastLat,lastLon) should get a new
// line now that we are at (lat,lon).
static inline bool wardriveShouldLog(uint32_t sinceMs,
                                     int32_t lastLatI, int32_t lastLonI,
                                     int32_t latI, int32_t lonI,
                                     uint32_t minIntervalMs, uint32_t minMoveM) {
    if (sinceMs >= minIntervalMs) return true;
    return wardriveDistanceM(lastLatI, lastLonI, latI, lonI) >= (float)minMoveM;
}

// Degrees * 1e7 as "-12.3456789". Integer maths, so nothing is lost to float.
static inline void wardriveDeg(int32_t v, char *out, size_t outLen) {
    const int64_t a = (v < 0) ? -(int64_t)v : (int64_t)v;
    snprintf(out, outLen, "%s%ld.%07ld", (v < 0) ? "-" : "",
             (long)(a / 10000000LL), (long)(a % 10000000LL));
}

// CSV-quote: wrap in quotes, double any embedded quote, drop CR/LF.
static inline void wardriveQuote(const char *in, char *out, size_t outLen) {
    if (!out || outLen < 3) { if (out && outLen) out[0] = '\0'; return; }
    size_t o = 0;
    out[o++] = '"';
    for (const char *p = in ? in : ""; *p && o + 2 < outLen; p++) {
        if (*p == '\r' || *p == '\n') continue;
        if (*p == '"') {
            if (o + 3 >= outLen) break;
            out[o++] = '"';
        }
        out[o++] = *p;
    }
    out[o++] = '"';
    out[o] = '\0';
}

// One log line, no newline. Matches the header in wardrive_log.cpp:
// epoch,utc,nodeId,shortName,longName,rssi,snr,hops,portnum,chanIdx,
// lat,lon,altM,sats,hdop,speedKmh,nodeLat,nodeLon
static inline void wardriveFormatLine(char *out, size_t outLen,
                                      long epoch, uint32_t nodeId,
                                      const char *shortName, const char *longName,
                                      float rssi, float snr, int hops,
                                      int portnum, int chanIdx,
                                      int32_t latI, int32_t lonI, int32_t altM,
                                      unsigned sats, float hdop, float speedKmh,
                                      bool nodeHasPos, int32_t nodeLatI, int32_t nodeLonI) {
    char utc[24] = "";
    if (epoch > 0) {
        time_t t = (time_t)epoch;
        struct tm tmv;
        gmtime_r(&t, &tmv);
        strftime(utc, sizeof(utc), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    }
    char sq[24], lq[96];
    wardriveQuote(shortName, sq, sizeof(sq));
    wardriveQuote(longName, lq, sizeof(lq));
    char hopsS[12] = "";
    if (hops >= 0) snprintf(hopsS, sizeof(hopsS), "%d", hops);
    char lat[24], lon[24], nlat[24] = "", nlon[24] = "";
    wardriveDeg(latI, lat, sizeof(lat));
    wardriveDeg(lonI, lon, sizeof(lon));
    if (nodeHasPos) {
        wardriveDeg(nodeLatI, nlat, sizeof(nlat));
        wardriveDeg(nodeLonI, nlon, sizeof(nlon));
    }
    snprintf(out, outLen,
             "%ld,%s,!%08lx,%s,%s,%.0f,%.2f,%s,%d,%d,%s,%s,%ld,%u,%.1f,%.1f,%s,%s",
             epoch, utc, (unsigned long)nodeId, sq, lq,
             (double)rssi, (double)snr, hopsS, portnum, chanIdx,
             lat, lon, (long)altM, sats, (double)hdop, (double)speedKmh,
             nlat, nlon);
}
