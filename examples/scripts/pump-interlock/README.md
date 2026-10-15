# pump-interlock

Soil-moisture telemetry on A0 plus a pump relay whose behavior follows a
**remotely-sent threshold value**: a client publishes `vars set target=<value>` (then
`vars save`) on `shell/in`, and the scheduled check script sets the pump pin from the
stored value (pump on while `raw > target`, i.e. dry side).

## Wiring (NodeMCU v2)

```
3V3 ──── A0 ────[ capacitive soil sensor ]     dry = high reading
GND ────[ relay module IN ]────────────────── D1 (GPIO5)  pump on/off
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
sensor register soil A0 moisture
notepad /scripts/pump-interlock.hx    # paste the contents of pump-interlock.hx, save
run /scripts/pump-interlock.hx        # run once: init registers the pump relay, the schedules and defaults target=600
```

The script is self-contained: its `init` block registers the pump relay (`relay add` is
idempotent), the schedules and `vars setdefault target=600` once per boot. A `target` you
set remotely survives reboots (the vars store auto-loads at boot) and is never overwritten
by setdefault. (`sensor register` still runs once in the shell — re-registering reports a
duplicate.)

Calibrate: probe dry and wet readings (`sensor publish soil` and subscribe, or `adc` on
the shell), then pick a `target` between them (setup default: 600).

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/sensor/moisture` | A0 raw moisture telemetry (every 5 min via schedule) |
| `<prefix>/relay/pump/state` | relay state after every change |
| `<prefix>/availability` | `online` / `offline` |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set target=650'  # retune the dry threshold
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'            # persist it across reboots
mosquitto_pub -t '<prefix>/shell/in' -m 'relay set pump on'    # manual override (until next tick)
```

Monitor:

```sh
mosquitto_sub -t '<prefix>/sensor/moisture' -t '<prefix>/relay/pump/state'
```

## Notes

- This is the canonical "receive a value, set a pin from it" pattern: the device
  subscribes only to `<prefix>/shell/in`, and payloads there run as commands —
  `vars set` changes the value the check script compares against.
- Send `vars save` after `vars set`: the vars store auto-loads at boot, so the saved
  `target` survives reboots and the script's `vars setdefault` leaves it untouched.
- A manual `relay set` is re-evaluated at the next scheduled tick (30 s).
