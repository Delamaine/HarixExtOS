# auto-vent

[Back to index](../README.md)

A temperature-driven window/roof vent with hysteresis, the servo twin of
fan-control: the vent swings open at `$open_at` °C and only closes again
once the reading drops below `$close_at` — no chattering around a single
setpoint. The applied state is acked on `stat/harixos/vent`, and a manual
`vars set vent=1` / `=0` nudges it between thresholds.

## Wiring (NodeMCU v2)

```
3V3 ────[ 10k resistor ]──── D5 (GPIO14) ────[ DHT22 data ]
                                DHT22 VCC ── 3V3
                                DHT22 GND ── GND

vent servo signal ─────────────────────────── D2 (GPIO4)
servo V+ ── 5V, servo GND ── GND (common)
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
sensor register dht22 14 ventbox
notepad /scripts/auto-vent.hx   # paste the contents of auto-vent.hx, save
run /scripts/auto-vent.hx       # run once: init attaches the servo, registers the schedule and the thresholds
```

`sensor register` is the one shell command (as with fan-control) —
re-registering reports a duplicate.

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set open_at=32'    # open later
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set close_at=29'   # close earlier
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set vent=1'        # manual open (until temperature takes over)
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'              # persist thresholds + last position
```

Monitor the ack:

```sh
mosquitto_sub -t 'stat/harixos/vent'
```

## What the device publishes

| Topic | Content |
|---|---|
| `stat/harixos/vent` | applied state (0 closed / 1 open) per change |
| `<prefix>/sensor/ventbox/temperature` | °C telemetry (every 5 min via `sensor publish`) |
| `<prefix>/availability` | `online` / `offline` |

## Notes

- Keep `close_at < open_at`. Between the thresholds nothing happens —
  that dead band **is** the hysteresis.
- Position memory: `vent` (state) and `applied` (RAM) start at `-1`, so a
  fresh boot commands nothing until the first threshold crossing. After
  `vars save`, the stored `vent` re-applies on the first tick (`applied` is
  always RAM-only) — the vent returns to where it was.
- Angles are knobs (`open_angle=160`, `closed_angle=20`) — fit the horn so
  those numbers match your mechanical stops, don't force the servo past
  them.
- `vars set vent=0/1` outside the thresholds is honored immediately, then
  the next 5-minute reading decides again — the same "override until next
  tick" contract as fan-control's manual `relay set`.
- Sensor is read through the registered DHT22 cache (`sensor <name>
  temperature`), so the 5-minute schedule never blocks on the wire.
- A held servo needs continuous pulses. Under `powerprofile light` the CPU
  pauses between DTIM beacons and pulses glitch (droop/jitter), so keep the
  profile at `full`/`balanced` while the vent must hold its angle.
