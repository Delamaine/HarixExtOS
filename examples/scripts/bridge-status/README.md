# bridge-status

[Back to index](../README.md)

Tasmota-style telemetry over raw `mqtt pub`: every 5 minutes one JSON line
lands on a **fixed** topic (`tele/harixos/state` by default) — no
`<prefix>` added, so a bridge, Node-RED or an HA MQTT sensor can subscribe
to a topic it already knows. The payload is built from `$vars`:
`{"heap":"…","uptime":"…","time":"…"}`. Zero hardware, five lines of
script.

## Wiring

None — pure software.

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
notepad /scripts/bridge-status.hx   # paste the contents of bridge-status.hx, save
run /scripts/bridge-status.hx       # run once: init registers the 5-minute cron job
```

The script is self-contained: its `init` block registers the schedule and
the topic default once per boot. Cron needs a valid system time; NTP syncs
automatically once Wi-Fi is up.

## What the device publishes

| Topic | Content |
|---|---|
| `tele/harixos/state` (fixed, no prefix) | `{"heap":"…","uptime":"…","time":"…"}` every 5 min |
| `<prefix>/availability` | `online` / `offline` (LWT, always on) |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set topic=tele/kitchen/state'
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'
mosquitto_pub -t '<prefix>/shell/in' -m 'run /scripts/bridge-status.hx'   # fire one now
mosquitto_pub -t '<prefix>/shell/in' -m 'schedule add 0 */1 * * * * run /scripts/bridge-status.hx'  # every minute
```

Monitor:

```sh
mosquitto_sub -t 'tele/#'
```

## Notes

- The firmware already publishes `<prefix>/telemetry` (rssi, adc, servo,
  motor state) on its own timer — this example is the **recipe** for any
  custom fixed topic/payload, not a replacement.
- `mqtt pub` is strict about `$name`: an undefined var fails the command,
  which is why the three `set` lines come first — they guarantee the vars
  exist even if the store was cleared.
- To extend the payload, add `set` lines and JSON fields; keep the body
  space-free (`post`/`pub` split arguments on spaces).
- Subscribe to `tele/#` to watch every bridge-status device at once.
