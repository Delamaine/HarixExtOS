# pan-tilt-remote

[Back to index](../README.md)

Two servos as a remotely driven mount — camera, antenna, laser pointer —
with a knob per axis: `vars set pan=45` / `vars set tilt=120` over MQTT
moves the head within a second, and each axis **acks the applied angle** on
its own topic (`stat/harixos/pan`, `stat/harixos/tilt`), scene-ack style.
Out-of-range values are ignored (the axis just holds — no ack), and both
knobs default to `-1` = untouched, so a fresh boot doesn't sweep the head
anywhere until it is told to.

## Wiring (NodeMCU v2)

```
pan servo signal ──────────────────────────── D2 (GPIO4)
tilt servo signal ─────────────────────────── D7 (GPIO13)
servo V+ ── 5V (both), servo GND ── GND (common)
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
notepad /scripts/pan-tilt-remote.hx   # paste the contents of pan-tilt-remote.hx, save
run /scripts/pan-tilt-remote.hx       # run once: init attaches both servos, registers the 1 s poll and the defaults
```

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set pan=45'    # look left
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set tilt=120'  # nod down
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'          # persist the pose across reboots
```

Monitor both acks:

```sh
mosquitto_sub -t 'stat/harixos/pan' -t 'stat/harixos/tilt'
```

## What the device publishes

| Topic | Content |
|---|---|
| `stat/harixos/pan` | applied pan angle per change (raw pub, fixed topic) |
| `stat/harixos/tilt` | applied tilt angle per change (raw pub, fixed topic) |
| `<prefix>/availability` | `online` / `offline` |

## Notes

- A **pose** is just two knobs: `vars save` stores both, and after a reboot
  the first tick re-asserts them (`applied_*` is RAM-only) — the head returns
  to where you left it. With nothing saved, `pan=tilt=-1` means the servo is
  attached but never commanded: it stays wherever the horn was fitted.
- Validation is a nested `if $v >= 0 / if $v <= 180` guard — no ack leaves
  the axis, which is exactly how a dashboard detects a rejected command.
- Poll cadence is 1 s (`* * * * * *`); a knob change lands within that
  window — the servo slew itself is mechanical (~0.1-0.2 s per 60°).
- Both servos on the 5V rail: budget a real 5V supply above ~2 servos
  moving at once; the ESP's onboard 3V3 cannot carry servo motors.
- For a camera that *follows* something instead, see **radar-sweep** (sweep
  pattern) and **sun-blind** (sensor-driven) — this example is the pure
  teleoperation case.
- A held servo needs continuous pulses. Under `powerprofile light` the CPU
  pauses between DTIM beacons and pulses glitch (droop/jitter), so keep the
  profile at `full`/`balanced` while the mount must hold its aim.
