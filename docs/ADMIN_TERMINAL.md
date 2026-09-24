# Remote Administration Terminal

Camillia can administer another Meshtastic node over the air — read its
configuration, change it, and reboot it — from the device itself or from web
config. This is Meshtastic's own `ADMIN_APP`, spoken by the firmware you are
already running, so the node on the other end does not have to be a Camillia
device. Any Meshtastic node that will accept your key will answer.

Everything here goes over LoRa or through your MQTT bridge, PKI encrypted, one
command at a time.

## Before anything works: the remote has to know you

A node does not take orders from strangers. The remote must carry **your**
public key in its `security.admin_key` list, and there is no way around that
from this end — it is set on the remote, by whoever owns it.

Your public key is on the web config page, top left, on the same row as the
battery and GPS readouts:

```
 ┌────────────────────────────────────────────────────────────┐
 │  [ pub: BHyLd7q2…f04A= ⧉ ]            ▮ 82%   ⌖ GPS 9   ≋  │
 ├────────────────────────────────────────────────────────────┤
 │  Config   Nodes   Chat   Live   Map   Utilities            │
```

Click it to copy. Then, on the remote node, add it to `security.admin_key`
(Meshtastic app: *Settings → Security → Admin keys*). A node can hold several,
so adding yours does not displace anyone else's.

Until that is done, every command you send comes back refused, and Camillia will
say so rather than leave you guessing.

## How Camillia decides what you may administer

There is a short list of **admin peers**, held in NVS, and the terminal is
offered only for nodes on it. That is deliberate: a menu that offered a terminal
on any node you have ever heard is a menu that invites you to spray
unauthorized admin packets across the mesh.

Each peer is in one of three states:

| State | Meaning | Terminal offered? |
|---|---|---|
| **Confirmed** | A probe came back with a session passkey. Proved. | Yes |
| **Unconfirmed** | On the list, nobody has asked yet. | Yes |
| **Denied** | The remote answered *no*. | No |

Unconfirmed still gets a terminal because after every reboot *every* peer is
unconfirmed — the list is reloaded but the proofs are not, on the grounds that
a remote may have been reconfigured while you were switched off. Waiting for
the sweep before the terminal reappeared made the feature look broken for the
first minute of every boot. Opening on an unproved peer risks one refused
packet to a node already on your list; if it refuses, it is recorded Denied and
you will not be offered it again.

### How nodes get on the list

**Automatically, from your favourites.** About 45 seconds after boot, Camillia
walks your favourite nodes and asks each one — quietly, one every eight
seconds — whether it accepts administration from you. Anything that says yes
appears in the list on its own.

Favourites rather than every node you have heard, and that is the whole safety
argument: a favourite is a short, deliberate, curated list. Sweeping everything
would put an admin packet in front of every stranger on the mesh once per
reboot, which is a fair description of scanning.

**On demand.** Web config → *Utilities* → *Remote Admin* has a **Scan
favourites** button that runs the same sweep immediately. Useful when a node was
out of range or switched off at boot. It is limited to once every 30 seconds —
a sweep is a packet every eight seconds for as long as your favourites last,
and restarting it from the top on every click is how one browser tab turns into
a node that will not stop transmitting.

## Web config

*Utilities → Remote Admin.*

```
 ┌────────────────────────────────────────────────────────────┐
 │  Remote Admin                                              │
 │                                                            │
 │  Administer another Meshtastic node over LoRa or MQTT. The │
 │  remote must carry this node's public key in its           │
 │  security.admin_key — copy it from the chip at the top of  │
 │  this page. Favourite nodes are probed once per boot…      │
 │  (C) confirmed   (U) unconfirmed                           │
 │                                                            │
 │  [ Scan favourites ]  Asking 3 favourites…                 │
 │  Asks every favourite node with a public key… once every   │
 │  30 seconds.                                               │
 │                                                            │
 │  Node              Last proved        │                    │
 │  ─────────────────────────────────────┼──────────────────  │
 │  Ridgeline Relay   (C)  2026-09-20 08:14  [Terminal][Verify]│
 │  Barn Repeater     (C)  2026-09-19 21:02  [Terminal][Verify]│
 │  !7a3c91ff         (U)  never             [Terminal][Verify]│
 └────────────────────────────────────────────────────────────┘
```

Nodes are listed by long name, falling back to short name, falling back to the
node id — which is all a node heard only as a relayed packet ever gives you.
**Last proved** is when, not how. **Verify** re-runs the probe for one node
without reloading the page; **Scan favourites** re-runs it for all of them.

There is also an **Admin** button beside *Favorite* on the Nodes tab, for any
node the list will allow. Nodes that are not administerable get no button at
all rather than a greyed one — a disabled *Admin* on a stranger's node is an
invitation to try.

### The terminal

```
 ┌───────────────────────────────────────────────────────────┐
 │  Remote Admin Ridgeline Relay                    [ Help ] │
 │ ┌───────────────────────────────────────────────────────┐ │
 │ │ session open - type help                              │ │
 │ │ > info                                                │ │
 │ │ sent id 4db21384 via rf                               │ │
 │ │ get_device_metadata_response (rf)                     │ │
 │ │   firmware: 2.7.1                                     │ │
 │ │   hardware: 43                                        │ │
 │ │   role: 2                                             │ │
 │ │   can shutdown: yes                                   │ │
 │ │ > set lora hop_limit 4                                │ │
 │ │ fetching lora config                                  │ │
 │ │ splicing lora.hop_limit = 4                           │ │
 │ │ ack (rf)                                              │ │
 │ └───────────────────────────────────────────────────────┘ │
 │ [ help                          ] [ Send ]  [ Close ]     │
 └───────────────────────────────────────────────────────────┘
```

Typing `help`, or pressing **Help**, opens the command table in a second panel
to the *right* of the terminal, and the terminal slides left so you can read the
table while you type. Close the help panel and the terminal returns to centre.

```
 ┌──────────────────────────────┐ ┌────────────────────────┐
 │  Remote Admin Ridgeline …    │ │  Commands              │
 │ ┌──────────────────────────┐ │ │  Meta   help [cmd]     │
 │ │ session open - type help │ │ │         whoami session │
 │ │ > _                      │ │ │  Read   info           │
 │ │                          │ │ │         owner          │
 │ └──────────────────────────┘ │ │         config <block> │
 │ [ help        ] [Send][Close]│ │  …          [ Hide ]   │
 └──────────────────────────────┘ └────────────────────────┘
```

`help <command>` is different: that answer is one line and goes into the
transcript with everything else.

## On the device

*Nodes → select a node → Enter → **(A)dmin***.

The row is only there for nodes the peer list allows, exactly as on the web.

```
        ┌──────────────────────────────────────────┐
        │  Ridgeline Relay              !a1b2c3d4  │
        │ ┌──────────────────────────────────────┐ │
        │ │  (T)raceroute                        │ │
        │ │  Sen(d) DM                           │ │
        │ │  Un(f)avorite                        │ │
        │ │  Re(q)uest Node                      │ │
        │ │  Request (P)osition                  │ │
        │ │  I(g)nore                            │ │
        │ │  (L)ocate                            │ │
        │ │  Del(e)te                            │ │
        │ │▸ (A)dmin                             │ │
        │ └──────────────────────────────────────┘ │
        └──────────────────────────────────────────┘
```

The terminal itself, on a T-Deck:

```
 ┌──────────────────────────────────────────────────────────┐
 │ Ridgeline Relay  rf                                      │
 │ session open - type help                                 │
 │ > config lora                                            │
 │ sent id 5e01aa73 via rf                                  │
 │ get_config_response (rf)                                 │
 │   lora:                                                  │
 │   field 2: 0                                             │
 │   field 7: 1                                             │
 │   field 8: 3                                             │
 │   field 9: 1                                             │
 │ > set lora hop_limit 4                                   │
 │ fetching lora config                                     │
 │ splicing lora.hop_limit = 4                              │
 │ ack (rf)                                                 │
 │ ┌──────────────────────────────────────────────────────┐ │
 │ │ _                                                    │ │
 │ └──────────────────────────────────────────────────────┘ │
 └──────────────────────────────────────────────────────────┘
```

The title line is the remote's name and the route in use. When a destructive
command is waiting for you it reads `CONFIRM?` on the right.

Config and module blocks come back as field *numbers*, not names — there is no
name table for them, and a number beside its value is still the remote's real
configuration where "62 bytes" would not be. The names in the `set` table above
are the translation: `field 8` in the LoRa block is `hop_limit`, which is why
`set lora hop_limit 4` works even though the read did not say so. `info`,
`owner` and `channel` do have name tables and read plainly.

### Keys

| Key | What it does |
|---|---|
| Letters, digits | Type the command |
| **Enter** | Send |
| **Backspace** | Delete a character |
| **Trackball ↑ / ↓** | Walk your command history (last 8) |
| **Backspace** or **Esc** on an empty line | Close the terminal |
| Touch drag | Scroll the transcript |

Closing the window does not end the session. The remote holds its session key
for five minutes, so reopening inside that window continues the same
conversation instead of costing another round trip.

Boards differ. The Cardputer closes modals with **Esc** only; the Mesh Deck has
no Esc, so Backspace is the close key. On the **T-Deck Pro**, whose one-bit
e-paper panel cannot tell the four kinds of transcript line apart by colour,
each line carries a marker instead — `>` what you typed, `-` progress, `+`
success, `!` failure — and a pair of **▲ ▼** buttons sits to the right of the
transcript, because `j` and `k` are letters the moment there is a text field on
screen.

## Commands

```
Meta   help [cmd]  whoami  session  transport [rf|mqtt|auto]
       peers  verify  clear  exit
Read   info                 firmware, hardware, role
       owner                names, id, public key
       config <block>       device position power network display
                            lora bluetooth security
       module <block>       mqtt serial telemetry neighborinfo ...
       channel <0-7>        name, role, uplink/downlink
Write  set owner long <text> | set owner short <text>
       set lora <field> <value>
            modem_preset region hop_limit tx_enabled tx_power
            channel_num override_duty_cycle rx_boosted_gain
       set device <field> <value>
            role rebroadcast_mode node_info_broadcast_secs tzdef
       set position <field> <value>
       fixedpos <lat> <lon> [alt] | fixedpos clear
       fav add|rm <!id>     ignore add|rm <!id>
       time                 push our clock
Danger reboot [secs]  shutdown [secs]
       reset nodedb | reset config | reset device
       (each needs `confirm` on the next line)
```

This table is served by the firmware, not written into the web page, so the
device terminal and the browser can never disagree about what exists.

### Writes are read-modify-write

`set lora hop_limit 4` does not send a LoRa config containing only a hop limit —
that would blank every other field on the remote. It fetches the block, splices
the one field into the bytes that came back, and sends the result. Fields the
firmware has no name for survive the trip untouched, because they are carried
through as bytes rather than re-encoded.

You will see this in the transcript as two steps: `fetching lora config`, then
`splicing lora.hop_limit = 4`.

### Destructive commands need `confirm`

`reboot`, `shutdown` and the three `reset` variants arm and wait:

```
 > reset config
 factory reset the remote's config - type `confirm` to proceed
 > confirm
 factory reset the remote's config
 sent id 90fe2b11 via rf
 ack (rf)
```

Anything other than `confirm` on the next line cancels. These also require a
fresh session key, so if the cached one has aged out you will be told to run a
read first — that is the protocol's own protection against a replayed
destructive command, not Camillia being cautious.

## Transport

`transport auto` is the default and prefers RF when the node has been heard
recently, falling back to the MQTT bridge. `transport rf` and `transport mqtt`
pin it. Replies are accepted from either route regardless of how the request
went out, because a node may be reachable one way and answer another.

Over MQTT the exchange uses the `PKI` pseudo-channel, so it is end-to-end
encrypted between the two nodes and the broker relays ciphertext.

## When it goes wrong

| Transcript line | What happened |
|---|---|
| `nak: unauthorized (33)` / `(37)` | The remote does not carry your public key. |
| `authorization lost - closing` | It did, and no longer does. The peer is marked Denied. |
| `session key rejected - run the command again` | The passkey aged out mid-command. Harmless; retry. |
| `timeout after 30s` | Nothing came back. Out of range, asleep, or a bridge that is down — the peer's state is left alone, because silence is not a refusal. |
| `mqtt bridge is down` | Pinned to MQTT with no broker connection. |
| `this node no longer accepts administration from us` | A favourites sweep discovered your grant was revoked while you had the terminal open. |

That last one is worth knowing about: a sweep can be running while you are
mid-session, and if it discovers the node you are talking to has stopped
accepting you, it says so in your transcript rather than letting you find out
from the next write that fails.

## What Camillia does not keep

The transcript is not persisted. It lives in a ring buffer of the last 40 lines
while the session is open and is freed when it closes — a transcript can hold a
remote node's entire configuration, and that has no business outliving the
window it was shown in.

The admin peer list *is* persisted, and rides along in `config.yaml` export and
import. An imported list is a list, not a proof: every peer comes back in as
unconfirmed and has to prove itself again on first use.
