# Terrain Line of Sight (LOS)

Camillia can tell you whether the ground between you and another node is likely
to block the radio path — and if so, where. This document covers the one piece
of setup it needs: an elevation proxy.

- [What you get](#what-you-get)
- [Why it needs a proxy](#why-it-needs-a-proxy)
- [Quick start](#quick-start)
- [Point the device at it](#point-the-device-at-it)
- [Using it](#using-it)
- [Running it properly](#running-it-properly)
- [Limits](#limits)
- [Troubleshooting](#troubleshooting)

## What you get

A node that has reported a position gets a **LOS** entry in its actions menu.
Opening it samples 24 points along the great circle between you and that node,
fetches the ground elevation at each, adds the earth's curvature, and compares
the result against a straight antenna-to-antenna line.

The verdict is one of three:

| | meaning |
|---|---|
| **LINE OF SIGHT** | the path clears the ground with Fresnel margin to spare |
| **MARGINAL (Fresnel)** | the line clears the dirt but cuts into the zone the signal actually needs |
| **NO LINE OF SIGHT** | terrain is above the sight line; the modal says by how much and how far along |

The cross-section shows the terrain in green and the sight line coloured by
verdict, with a dot on the tightest point.

Antenna heights are fixed at **2 m at both ends** and stated in the modal. They
move the verdict materially, so treat the result as "rough estimation" — which
is what it is.

## Why it needs a proxy

**This firmware has no TLS client.** It was removed deliberately, to give the
Wi-Fi stack and the web config page the internal memory they need. Every public
elevation API is HTTPS-only, so the device cannot reach one.

A small proxy bridges that gap:

```
device  --HTTP-->  elev_proxy.py  --HTTPS-->  api.opentopodata.org (SRTM 30 m)
```

The device speaks plain HTTP to something you run; that something speaks HTTPS
to the elevation backend. This is the same shape the Web Config map download
uses, and the same approach the wadamesh port takes.

## Quick start

The proxy lives at [`tools/elev-proxy/elev_proxy.py`](../tools/elev-proxy/elev_proxy.py).
Run it on any always-on machine on your network — a NAS, a Pi, the desktop you
already leave running:

```sh
pip install flask requests
python3 tools/elev-proxy/elev_proxy.py
```

It listens on port **5005** on all interfaces, because the device has to reach
it across the LAN. Check it:

```sh
curl http://localhost:5005/healthz
# ok

curl "http://localhost:5005/elev?locations=39.7,-105.2|39.7,-105.4|39.7,-105.6"
# 1955,2451,2908
```

Those are metres above sea level, one per point, in order. That is the entire
contract — anything answering `GET /elev?locations=lat,lon|...` with a CSV of
metres will work, so swap the backend if you have a better elevation source.
`null` is accepted for a point with no data; the firmware interpolates across
gaps and only fails when too many are missing.

## Point the device at it

1. Open Web Config
2. Find **Elevation Server (LOS)**
3. Enter `http://<host>:5005` — the machine running the proxy
4. Save

It must be **http://**, not https://. An https:// address cannot be reached at
all, and will fail on every attempt.

Leave the field empty to disable LOS entirely — the action stays visible but the
modal tells you it is unconfigured rather than failing with a network error.

## Using it

1. Open **Nodes**
2. Select the node
3. Press **S** for LOS (or tap **LOS** on touch builds)

The modal shows "Analyzing terrain..." while the fetch runs. It runs on a
background worker, so the rest of the UI stays responsive; a slow lookup costs
you the label staying up, not a frozen screen.

**LOS needs a position at both ends.** The row is greyed when the contact has
never reported one, or when this node has neither a GPS fix nor a location set
in config. It stays visible in its usual place rather than disappearing.

## Running it properly

For anything beyond trying it out, run it under a service manager so it survives
reboots. A minimal systemd unit:

```ini
[Unit]
Description=Camillia elevation proxy
After=network-online.target

[Service]
ExecStart=/usr/bin/python3 /opt/camillia/elev_proxy.py
Restart=on-failure
User=nobody

[Install]
WantedBy=multi-user.target
```

The script keeps a small in-process cache keyed on the exact query, so
re-analysing the same pair of nodes costs nothing and does not spend quota. That
cache dies with the process. If you want it to survive restarts — or you are
serving several devices — put nginx in front with `proxy_cache` keyed on
`elev:$args` and a 30-day validity.

## Behind Nginx Proxy Manager

NPM is a reverse proxy — it forwards requests, it does not run the service. So
this is two pieces: the container (or systemd unit) running `elev_proxy.py`, and
an NPM proxy host in front of it.

### 1. Run the service

`tools/elev-proxy/` carries a `Dockerfile` and `docker-compose.yml`. The service
does not have to live on the same machine as NPM, and usually will not:

```sh
scp -r tools/elev-proxy <host>:~/
ssh <host> 'cd ~/elev-proxy && docker compose up -d --build'
```

As shipped it publishes port 5005 and joins no custom network, so NPM reaches it
at `http://<that-host>:5005`. Co-locating it with NPM instead is a small edit —
see the comment at the top of the compose file.

Confirm it answers before involving NPM:

```sh
curl "http://localhost:5005/elev?locations=39.7,-105.2|39.7,-105.6"
```

### 2. Add the proxy host

**Hosts → Proxy Hosts → Add Proxy Host**

| field | value |
|---|---|
| Domain Names | `los.camillia.sumat.org` |
| Scheme | `http` |
| Forward Hostname / IP | `camillia-elev` (or the host's LAN IP) |
| Forward Port | `5005` |
| Cache Assets | **off** — the custom config below does it properly |
| Block Common Exploits | on |
| Websockets Support | off |

**On the SSL tab, leave it as `None`, and do not enable "Force SSL".**

This is not a preference. The firmware has no TLS client: an HTTPS redirect makes
the endpoint unreachable, and the device reports it as a fetch failure with no
hint that a redirect was the cause. The proxy host must answer plain HTTP on 80.

### 3. Caching

Elevation for a given pair of nodes never changes, so a cache turns repeat
analyses into free lookups and protects the upstream daily quota.

`proxy_cache_path` has to live in nginx's `http` context, which NPM exposes
through a custom include. Create **`/data/nginx/custom/http.conf`** in the NPM
container's data volume:

```nginx
proxy_cache_path /data/cache/camillia_elev
                 levels=1:2
                 keys_zone=camillia_elev:5m
                 max_size=64m
                 inactive=30d
                 use_temp_path=off;
```

`/data` rather than `/var/cache/nginx` on purpose: it is NPM's persisted volume,
so the cache survives recreating the container.

Then in the proxy host's **Advanced** tab:

```nginx
location = /elev {
    proxy_pass http://camillia-elev:5005;
    proxy_set_header Host $host;

    proxy_cache camillia_elev;
    proxy_cache_key "elev:$args";
    proxy_cache_valid 200 30d;
    proxy_cache_valid 400 404 1h;
    proxy_cache_lock on;
    add_header X-Cache-Status $upstream_cache_status always;

    # The upstream retries once, so a cold request can legitimately take ~25 s.
    # Keep this above that and below nothing — the firmware waits 20 s, so a
    # genuinely slow first lookup may need a second attempt from the device.
    proxy_read_timeout 30s;
    proxy_connect_timeout 5s;
}
```

Do not add a `location /` block here — NPM generates its own, and a second one
makes nginx refuse to start with a duplicate location error.

Restart the NPM container after adding `http.conf`. Verify the cache:

```sh
curl -sD- "http://los.camillia.sumat.org/elev?locations=39.7,-105.2|39.7,-105.6" | grep -i x-cache
# X-Cache-Status: MISS   ... then HIT on a repeat
```

### 4. Point the device at it

Web Config → **Elevation Server (LOS)** → `http://los.camillia.sumat.org`

No port, no `/elev` — the firmware appends that itself.

### A word about exposure

`*.camillia.sumat.org` is a wildcard pointing at a public address, so this name
resolves from the open internet, not just your LAN. If ports 80/443 reach NPM
from outside, so does this endpoint.

It is not an open proxy — every coordinate is validated before anything is
forwarded — but anyone who finds it can spend your upstream quota, and the public
tier is 1000 requests/day.

Two ways to close that:

- **An NPM Access List** on the proxy host, allowing only your LAN range. Note
  that a device reaching the public name from inside may arrive hairpinned
  through the router, so check what source address NPM actually logs before
  relying on the rule
- **Local DNS**, overriding `los.camillia.sumat.org` to the Pi's LAN address on
  your router. The device never leaves the network, nothing is exposed, and no
  port forward is involved. This is the cleaner option when every device that
  needs LOS is on your own network

## Limits

The default backend is opentopodata's public SRTM 30 m dataset:

- **100 locations per request** — Camillia sends 24, so one query is one request
- **~1 request/second**
- **1000 requests/day** — roughly 1000 LOS queries per day

That is generous for personal use. If you outgrow it,
[self-host opentopodata](https://www.opentopodata.org/server/) and point
`ELEV_URL` in the script at your own instance; nothing else changes.

The proxy validates every coordinate before forwarding. Without that it would be
an open proxy for arbitrary upstream query strings — worth keeping if you adapt
the script.

## Troubleshooting

**"No elevation server set."** The Web Config field is empty. See
[Point the device at it](#point-the-device-at-it).

**"Wi-Fi needed to fetch the terrain profile."** The device is not on your
network. LOS cannot work from the device's own access point — there is no route
to the proxy from there.

**"Elevation fetch failed (-1)."** No Wi-Fi at the moment of the request.

**"Elevation fetch failed (404)"** or similar. The device reached something, but
not the proxy. Check the URL includes the port, and that it has no `/elev` on the
end — the firmware appends that itself.

**"Elevation fetch failed (-2)."** The URL did not parse. Almost always a missing
`http://` prefix.

**"Elevation fetch failed (502)."** The proxy is running but the upstream refused
it. Usually the daily quota, or a transient rate limit — try again in a minute.

**"Elevation data too sparse for this path."** The dataset has no coverage for
most of the path. SRTM does not cover latitudes beyond roughly ±60°, and has
holes over large water bodies.

**The LOS row is greyed.** One end has no position. Check the contact has
actually reported one, and that this node has a GPS fix or a location set.
