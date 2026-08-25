#!/usr/bin/env python3
"""Elevation proxy for Camillia's terrain line-of-sight (LOS) feature.

Camillia has no TLS client, and every public elevation API is HTTPS-only. This
sits in the middle: the device talks plain HTTP to this service, and this service
talks HTTPS to the elevation backend.

    device --HTTP--> elev_proxy.py --HTTPS--> api.opentopodata.org (SRTM 30 m)

Contract (what the firmware expects, see src/los.cpp):

    GET /elev?locations=lat,lon|lat,lon|...
    200 text/plain, a compact CSV of integer metres, one per point, in order:
        "182,190,205,201,..."
    "null" for any point the dataset has no data for — the firmware
    interpolates across those and only fails if too many are missing.

Run:
    pip install flask requests
    python3 elev_proxy.py                 # listens on 0.0.0.0:5005

Then set Web Config -> Elevation Server (LOS) to  http://<this-host>:5005

Backend limits: opentopodata's public endpoint allows 100 locations/request,
about 1 request/second, and 1000 requests/day. Camillia sends 24 points as a
single request per LOS query, so that is ~1000 queries/day. Self-host
opentopodata (https://www.opentopodata.org/server/) if you outgrow it.
"""

import time

import requests
from flask import Flask, Response, abort, request

ELEV_URL = "https://api.opentopodata.org/v1/srtm30m"
ELEV_MAX_POINTS = 100
UPSTREAM_TIMEOUT = 12          # seconds for one upstream attempt
CACHE_SECONDS = 30 * 24 * 3600

app = Flask(__name__)
_session = requests.Session()
_session.headers.update({"User-Agent": "camillia-elev-proxy/1.0"})

# Tiny in-process cache keyed on the exact query. The firmware samples the same
# 24 points every time it re-analyses a given pair of nodes, so a repeat lookup
# is free and does not spend the daily quota.
_cache: dict[str, str] = {}
_CACHE_MAX = 512


@app.get("/elev")
def elev():
    locations = request.args.get("locations", "").strip()
    if not locations:
        abort(400)

    pts = locations.split("|")
    if len(pts) < 2 or len(pts) > ELEV_MAX_POINTS:
        abort(400)

    # Validate every point before forwarding. Without this the service is an
    # open proxy: anyone could push an arbitrary query string upstream.
    for p in pts:
        try:
            lat_s, lon_s = p.split(",")
            lat, lon = float(lat_s), float(lon_s)
        except ValueError:
            abort(400)
        if not (-90.0 <= lat <= 90.0 and -180.0 <= lon <= 180.0):
            abort(400)

    hit = _cache.get(locations)
    if hit is not None:
        return Response(hit, mimetype="text/plain",
                        headers={"Cache-Control": f"public, max-age={CACHE_SECONDS}",
                                 "X-Cache": "HIT"})

    # opentopodata rate-limits to ~1 req/sec and occasionally 5xx's. One retry
    # with a >1 s gap absorbs that. Two attempts at 12 s plus the gap is ~25 s
    # worst case, which is why the firmware's read timeout is 20 s and not the
    # 9 s it started with — keep any change to these two numbers in step.
    r = None
    for attempt in range(2):
        if attempt:
            time.sleep(1.2)
        try:
            r = _session.get(ELEV_URL, params={"locations": locations},
                             timeout=UPSTREAM_TIMEOUT)
        except requests.RequestException:
            r = None
            continue
        if r.status_code == 200:
            break
        if r.status_code in (429, 500, 502, 503, 504):
            continue
        break

    if r is None or r.status_code != 200:
        abort(502)
    try:
        data = r.json()
    except ValueError:
        abort(502)
    if data.get("status") != "OK":
        abort(502)

    out = []
    for res in data.get("results", []):
        e = res.get("elevation")
        out.append(str(int(round(e))) if e is not None else "null")
    if not out:
        abort(502)

    body = ",".join(out)
    if len(_cache) >= _CACHE_MAX:
        _cache.clear()
    _cache[locations] = body
    return Response(body, mimetype="text/plain",
                    headers={"Cache-Control": f"public, max-age={CACHE_SECONDS}",
                             "X-Cache": "MISS"})


@app.get("/healthz")
def healthz():
    return "ok\n", 200


if __name__ == "__main__":
    # 0.0.0.0 on purpose: the device has to reach this across the LAN.
    app.run(host="0.0.0.0", port=5005, threaded=True)
