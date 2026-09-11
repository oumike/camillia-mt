# Weather

Current conditions for wherever the node is, fetched through a proxy you run.

- [What you get](#what-you-get)
- [Why it needs a proxy](#why-it-needs-a-proxy)
- [Quick start](#quick-start)
- [Point the device at it](#point-the-device-at-it)
- [Using it](#using-it)
- [What leaves the device](#what-leaves-the-device)
- [The contract](#the-contract)
- [Swapping the backend](#swapping-the-backend)
- [Troubleshooting](#troubleshooting)

## What you get

One screen under **Tools → Weather**: temperature, what it feels like, the sky
("Partly cloudy"), humidity, and wind with its gusts and direction. Current
conditions only — no forecast. It is the thing you check before going outside,
not a weather station.

Units follow **Config → Units**. Nothing to set separately.

Available on every board. The Cardputer has no Utilities tab in its lite web
config, so set its address through the microSD `config.yaml` instead:

```yaml
config:
  network:
    weatherServer: http://192.168.1.50:5006
```

The shipped default means it works there without any of that; the file is how
you point it somewhere else. Its 240x135 panel uses a compact layout — a
smaller headline and three detail lines rather than four.

## Why it needs a proxy

**This firmware has no TLS client.** It was removed deliberately, to give the
Wi-Fi stack and web config back the internal memory they need. Effectively
every weather API is HTTPS-only, so the device cannot reach one directly.

So the device speaks plain HTTP to something you run, and that speaks HTTPS
upstream — the same shape the elevation proxy uses for LOS:

```
device  --HTTP-->  weather proxy  --HTTPS-->  api.open-meteo.com
```

Two things fall out of that, both of them good:

- **Any API key lives in the proxy, not on the device.** Open-Meteo needs none,
  but if you swap in a service that does, the key goes in the proxy's
  environment — never in the firmware config, and never in the
  `/camillia/config.yaml` a microSD card carries around.
- **Your position goes to your own machine first.** No project-run service sees
  it, and only a rounded pair is passed upstream.

## Quick start

```sh
cd tools/weather-proxy
docker compose up -d --build

curl "http://localhost:5006/healthz"
# ok

curl "http://localhost:5006/weather?lat=39.74&lon=-105.21&units=m"
# v2,7,4,52,14,27,290,3,1,1757592000,C,km/h,Overcast,Golden, CO
```

Or without Docker:

```sh
pip install flask requests
python3 weather_proxy.py        # listens on 0.0.0.0:5006
```

It listens on **5006** so it can sit beside the elevation proxy on 5005. They
are separate services on purpose: run either without the other, and neither can
take the other down.

## Point the device at it

**There is a default, so the screen works out of the box**:
`http://weather.camillia.sumat.org` — a public instance of this proxy. Nothing
to set up unless you want to.

To run your own instead, put its address in Web Config → **Utilities → Weather
Server**:

```
http://192.168.1.50:5006
```

The base URL only — the firmware appends `/weather?...` itself. It must be
`http://`; an `https://` address cannot be reached at all, for the reason
above. **Clearing the field turns the screen off**, and is the way to make the
device send nothing at all.

Running your own is still the better answer if you would rather your position
did not leave your network: the default instance sees the same rounded pair
every device sends it, and nothing else.

## Using it

Open **Tools → Weather**. The reading is fetched when you open the screen and
then cached, so reopening it is free for a few minutes; there is no background
polling and the screen never brings Wi-Fi up on its own.

Every failure says which one it is, rather than one generic error:

| What you see | What happened |
| --- | --- |
| `Weather server not set` | No address in Web Config |
| `No position` | No GPS fix and no last-known position to fall back on |
| `Wi-Fi not connected` | The radio is not on a network |
| `Proxy unreachable` | Nothing answered at that address |
| `Proxy error (502)` | The proxy answered, but its upstream did not |
| `Bad response` | Something answered that is not this contract |

A stale cached reading is shown with its age rather than discarded, so a
failed refresh still leaves you with the last thing that worked.

## What leaves the device

The rounded position, and nothing else. No node name, no node ID, no channel,
nothing about the mesh.

**The position is rounded to two decimals — about a kilometre — before it
leaves**, and the proxy rounds again on arrival. Weather is accurate at town
resolution, so neither end has any use for a precise fix, and the screen says
the reading is for a rounded position rather than implying it is your exact
one.

This is **independent of Config → Share Location**. That setting governs
whether your position goes out over the mesh, to everyone in range; this sends
a rounded pair to a machine you run, over your own LAN. They are different
questions, so one does not gate the other — but if you would rather nothing at
all left the device, leave the weather server unset and the feature never
sends anything.

## The contract

Anything that answers this will work. The device knows nothing about which
service is behind it.

```
GET /weather?lat=<deg>&lon=<deg>&units=m|i
200 text/plain, one line:

v2,temp,feels,humidity,wind,gust,dir,code,isday,epoch,tunit,sunit,desc,place
```

`v1` was the same line without the trailing `place`. The firmware accepts
either, so an older proxy keeps working — it simply has no place name to put in
the title.

| # | Field | Meaning |
| --- | --- | --- |
| 0 | `v1` | Contract version. The firmware refuses anything else rather than guessing at a layout it does not know |
| 1 | `temp` | Air temperature, whole degrees, already in the requested unit |
| 2 | `feels` | Apparent temperature, same unit |
| 3 | `humidity` | Relative humidity, whole percent |
| 4 | `wind` | Wind speed, whole units |
| 5 | `gust` | Gust speed, same unit |
| 6 | `dir` | Direction the wind is coming *from*, 0-359 |
| 7 | `code` | WMO weather code, 0-99 |
| 8 | `isday` | 1 in daylight at that location, else 0 |
| 9 | `epoch` | Observation time, UTC seconds |
| 10 | `tunit` | `C` or `F` |
| 11 | `sunit` | `km/h` or `mph` |
| 12 | `desc` | Short description, ASCII, never contains a comma |
| 13 | `place` | `City, ST` in the US, `City, Country` elsewhere. ASCII, and the one field allowed to contain a comma — it is last, so the firmware takes the remainder of the line verbatim. Empty when the location could not be resolved, which is not an error |

Two deliberate choices in that shape:

- **Every number is whole**, so the on-device parser is `atoi()` and a walk over
  twelve commas. The device has no business parsing a vendor JSON payload.
- **Unit conversion happens in the proxy.** The device knows which units the
  user picked, asks for them, and prints what comes back.
- **The place name is resolved here too**, by reverse geocoding the rounded
  coordinate through Nominatim and caching it for the life of the process. A
  rounded coordinate's town does not change, so each one is looked up once. A
  failed lookup returns an empty field rather than an error: the temperature is
  what was asked for, and the name is decoration.

## Swapping the backend

Open-Meteo is the default because it needs no key and allows non-commercial
use. The choice is entirely proxy-side — changing it needs no firmware release.

- **Open-Meteo** — no key. The default.
- **NWS (`api.weather.gov`)** — no key, US only, wants a real User-Agent.
- **OpenWeatherMap** and similar — key required, which is fine: it lives here.

Check the current terms of whichever you point at; they change, and this
document is not one of the parties.

## Troubleshooting

**`Weather server not set`** — the field is empty, or was saved with an
`https://` scheme, which this firmware cannot use.

**`Proxy unreachable`** — check the device and the proxy are on the same
network and nothing is filtering the port:

```sh
curl "http://<proxy-host>:5006/healthz"
```

**`Proxy error (502)`** — the proxy could not reach its upstream. Its log says
why; the usual causes are no outbound DNS from the container, or the upstream
rate-limiting you.

**`Bad response`** — something other than this contract answered. A captive
portal or a reverse proxy returning an HTML error page will do this.

**It says `No position`** — the node has no GPS fix and no last known position.
Take it outside for a fix; the position is remembered afterwards.
