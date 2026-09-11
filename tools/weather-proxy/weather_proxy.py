#!/usr/bin/env python3
"""Weather proxy for Camillia's weather screen.

Camillia has no TLS client — it was removed so the Wi-Fi stack and web config
could have the internal memory back — and effectively every weather API is
HTTPS-only. This sits in the middle, exactly as the elevation proxy does for
terrain line-of-sight:

    device --HTTP--> weather_proxy.py --HTTPS--> api.open-meteo.com

Contract (what the firmware expects, see src/weather.cpp):

    GET /weather?lat=<deg>&lon=<deg>&units=m|i
    200 text/plain, one line, comma separated, fixed order:

        v2,temp,feels,humidity,wind,gust,dir,code,isday,epoch,tunit,sunit,desc,place

    v1 was the same line without the trailing `place`. The firmware accepts
    either, so an older proxy still works — it just has no place name to show.

    field  meaning
    0      "v1" — contract version. The firmware refuses anything else rather
           than guessing at a layout it does not know.
    1      air temperature, whole degrees, already in the requested unit
    2      apparent ("feels like") temperature, same unit
    3      relative humidity, whole percent
    4      wind speed, whole units (km/h for m, mph for i)
    5      wind gust, same unit
    6      wind direction the wind is coming FROM, whole degrees 0-359
    7      WMO weather code (0-99)
    8      1 if it is daytime at that location, else 0
    9      observation time, UTC seconds since the epoch
    10     temperature unit label, "C" or "F"
    11     wind speed unit label, "km/h" or "mph"
    12     short description, ASCII, never contains a comma
    13     place name, "City, ST" in the US and "City, Country" elsewhere,
           ASCII, and the ONE field allowed to contain a comma — it is last, so
           the firmware takes the remainder of the line verbatim. Empty when
           the location could not be resolved, which is not an error.

    Every numeric field is a whole number so the firmware can parse the line
    with atoi() and nothing else. Unit conversion happens HERE, not on the
    device: the device knows which units the user picked and asks for them, and
    then only has to print what comes back.

Why a fixed CSV rather than the upstream's JSON: the device has no business
parsing a vendor payload, and this shape keeps the on-device parser to a walk
over twelve commas.

Run:
    pip install flask requests
    python3 weather_proxy.py              # listens on 0.0.0.0:5006

Then set Web Config -> Weather Server to  http://<this-host>:5006

Privacy: the firmware rounds the position to two decimals (~1 km) before it
sends it, and this service rounds again on arrival — weather is accurate at
town resolution, so neither end has any use for a precise fix. Coordinates go
to the machine you run this on and nowhere else; only the rounded pair reaches
the upstream API.

Swapping the backend: anything that answers the contract above will do. Open-
Meteo is the default because it needs no API key. If you move to a service that
does, the key lives in this process's environment — never in the firmware
config, and never in the /camillia/config.yaml a microSD card carries around.
"""

import os
import time

import requests
from flask import Flask, Response, abort, request

UPSTREAM_URL = "https://api.open-meteo.com/v1/forecast"
# Reverse geocoding, for the place name. Nominatim's usage policy wants a real
# User-Agent and no more than one request a second; the cache below makes that
# trivially true — a rounded coordinate's place name never changes, so each one
# is looked up once for the life of the process.
GEOCODE_URL = "https://nominatim.openstreetmap.org/reverse"
GEOCODE_TIMEOUT = 8
UPSTREAM_TIMEOUT = 12          # seconds for one upstream attempt
# How long a reading stays good. Conditions do not move fast enough to justify
# asking again sooner, and the firmware caches on its own side too.
CACHE_SECONDS = int(os.environ.get("WEATHER_CACHE_SECONDS", "600"))
# Matches the firmware's own rounding. Two decimals is about a kilometre.
COORD_DECIMALS = 2

app = Flask(__name__)
_session = requests.Session()
_session.headers.update({"User-Agent": "camillia-weather-proxy/1.0"})

# key -> (expires_at, body). Small because the realistic number of distinct
# rounded positions one household's nodes report is small.
_cache: dict[str, tuple[float, str]] = {}
_CACHE_MAX = 256

# Place names, keyed on the rounded coordinate. No expiry: towns do not move,
# and the key space is small because the coordinate is already rounded.
_places: dict[str, str] = {}
_PLACES_MAX = 512

# WMO weather codes. Kept short on purpose: these are printed on a 320x240
# panel, and several share a line with the temperature.
_WMO = {
    0: "Clear", 1: "Mainly clear", 2: "Partly cloudy", 3: "Overcast",
    45: "Fog", 48: "Rime fog",
    51: "Light drizzle", 53: "Drizzle", 55: "Heavy drizzle",
    56: "Freezing drizzle", 57: "Freezing drizzle",
    61: "Light rain", 63: "Rain", 65: "Heavy rain",
    66: "Freezing rain", 67: "Freezing rain",
    71: "Light snow", 73: "Snow", 75: "Heavy snow", 77: "Snow grains",
    80: "Light showers", 81: "Showers", 82: "Violent showers",
    85: "Snow showers", 86: "Heavy snow showers",
    95: "Thunderstorm", 96: "Thunderstorm, hail", 99: "Thunderstorm, hail",
}


def _describe(code: int) -> str:
    # Commas would break the CSV the firmware is about to split, so the table
    # above is comma-free by construction and this is the backstop for a code
    # that is not in it.
    return _WMO.get(code, f"Code {code}").replace(",", " ")


def _ascii(text: str) -> str:
    """ASCII only, and no control characters.

    The firmware draws this with a font that covers ASCII and little else, so an
    accented place name would reach it as a row of missing-glyph boxes. Folding
    here keeps that problem on the machine that has a full Unicode library.
    """
    out = text.encode("ascii", "ignore").decode("ascii")
    return "".join(c for c in out if 0x20 <= ord(c) < 0x7F).strip()


def _place_name(lat: float, lon: float) -> str:
    """"City, ST" for a rounded coordinate, or "" if it cannot be resolved.

    Never raises and never blocks the weather answer: a missing place name is a
    cosmetic loss, and the temperature is the thing that was asked for.
    """
    key = f"{lat},{lon}"
    hit = _places.get(key)
    if hit is not None:
        return hit

    name = ""
    try:
        r = _session.get(
            GEOCODE_URL,
            params={"lat": lat, "lon": lon, "format": "jsonv2", "zoom": 10,
                    "addressdetails": 1},
            timeout=GEOCODE_TIMEOUT,
        )
        if r.status_code == 200:
            addr = (r.json() or {}).get("address") or {}
            # Whichever of these exists, most specific first. Nominatim returns
            # a different one depending on how the area is administered.
            town = next((addr[k] for k in
                         ("city", "town", "village", "hamlet", "suburb",
                          "municipality", "county")
                         if addr.get(k)), "")
            # US states come back as full names; the two-letter code reads
            # better in a title and is what "city, state" means to most people.
            region = addr.get("ISO3166-2-lvl4") or ""
            if region and "-" in region:
                region = region.split("-", 1)[1]
            if not region:
                region = addr.get("state") or addr.get("country") or ""
            town, region = _ascii(town), _ascii(region)
            if town and region:
                name = f"{town}, {region}"
            else:
                name = town or region
    except (requests.RequestException, ValueError):
        name = ""

    if len(_places) >= _PLACES_MAX:
        _places.clear()
    _places[key] = name
    return name


def _whole(value, fallback: int = 0) -> int:
    try:
        return int(round(float(value)))
    except (TypeError, ValueError):
        return fallback


@app.get("/weather")
def weather():
    try:
        lat = round(float(request.args.get("lat", "")), COORD_DECIMALS)
        lon = round(float(request.args.get("lon", "")), COORD_DECIMALS)
    except ValueError:
        abort(400)
    if not (-90.0 <= lat <= 90.0 and -180.0 <= lon <= 180.0):
        abort(400)

    # Anything that is not an explicit "i" is metric. The firmware always sends
    # one or the other; a missing value should not become a silent unit switch.
    imperial = request.args.get("units", "m").lower().startswith("i")
    tunit, sunit = ("F", "mph") if imperial else ("C", "km/h")

    key = f"{lat},{lon},{tunit}"
    hit = _cache.get(key)
    now = time.time()
    if hit and hit[0] > now:
        return Response(hit[1], mimetype="text/plain",
                        headers={"Cache-Control": f"public, max-age={CACHE_SECONDS}",
                                 "X-Cache": "HIT"})

    params = {
        "latitude": lat,
        "longitude": lon,
        "current": ("temperature_2m,apparent_temperature,relative_humidity_2m,"
                    "wind_speed_10m,wind_gusts_10m,wind_direction_10m,"
                    "weather_code,is_day"),
        "temperature_unit": "fahrenheit" if imperial else "celsius",
        "wind_speed_unit": "mph" if imperial else "kmh",
        "timeformat": "unixtime",
        "timezone": "UTC",
    }

    # One retry, as the elevation proxy does: a transient 5xx or a dropped
    # connection should not surface on the device as "upstream error".
    r = None
    for attempt in range(2):
        if attempt:
            time.sleep(1.0)
        try:
            r = _session.get(UPSTREAM_URL, params=params, timeout=UPSTREAM_TIMEOUT)
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
        cur = r.json().get("current") or {}
    except ValueError:
        abort(502)
    if "temperature_2m" not in cur:
        abort(502)

    code = _whole(cur.get("weather_code"), -1)
    fields = [
        "v2",
        _whole(cur.get("temperature_2m")),
        _whole(cur.get("apparent_temperature"), _whole(cur.get("temperature_2m"))),
        _whole(cur.get("relative_humidity_2m")),
        _whole(cur.get("wind_speed_10m")),
        _whole(cur.get("wind_gusts_10m")),
        _whole(cur.get("wind_direction_10m")) % 360,
        code if code >= 0 else 0,
        1 if _whole(cur.get("is_day"), 1) else 0,
        _whole(cur.get("time"), int(now)),
        tunit,
        sunit,
        _describe(code),
        # Last on purpose: it is the only field allowed to contain a comma, so
        # the firmware can take the rest of the line without splitting it.
        _place_name(lat, lon),
    ]
    body = ",".join(str(f) for f in fields)

    if len(_cache) >= _CACHE_MAX:
        _cache.clear()
    _cache[key] = (now + CACHE_SECONDS, body)
    return Response(body, mimetype="text/plain",
                    headers={"Cache-Control": f"public, max-age={CACHE_SECONDS}",
                             "X-Cache": "MISS"})


@app.get("/healthz")
def healthz():
    return "ok\n", 200


if __name__ == "__main__":
    # 0.0.0.0 on purpose: the device has to reach this across the LAN.
    app.run(host="0.0.0.0", port=5006, threaded=True)
