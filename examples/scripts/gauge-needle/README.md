# gauge-needle

[Back to index](../README.md)

A physical dial gauge for room temperature: the DHT22's cached reading is
mapped linearly onto a servo needle — `$min_t` °C sits at 0°, `$max_t` °C at
180° — clamped at both stops so an out-of-range reading parks the needle
against the scale mark instead of grinding into the servo wall. The scale is
remotely retunable; this is the value→angle direction every other servo
example lacks (they all *drive* things, this one *displays*).

## Wiring (NodeMCU v2)

```
3V3 ────[ 10k resistor ]──── D5 (GPIO14) ────[ DHT22 data ]
                                DHT22 VCC ── 3V3
                                DHT22 GND ── GND

needle servo signal ──────────────────────── D2 (GPIO4)
servo V+ ── 5V, servo GND ── GND (common)
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
sensor register dht22 14 dial
notepad /scripts/gauge-needle.hx   # paste the contents of gauge-needle.hx, save
run /scripts/gauge-needle.hx       # run once: init attaches the servo, registers the schedule and the scale
```

The script is self-contained apart from `sensor register` (one shell command,
same as fan-control and greenhouse-alert — re-registering reports a
duplicate).

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set min_t=-10'  # widen the cold end
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set max_t=60'   # widen the hot end
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'           # persist the scale
```

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/sensor/dial/temperature` | °C telemetry (every 10 s via `sensor publish`) |
| `<prefix>/availability` | `online` / `offline` |

## Notes

- The mapping is one `set`: `($t - $min_t) * 180 / ($hi - $min_t)` —
  arithmetic in `set` uses real division, then the two `if` clamps hold the
  needle at the stops.
- A degenerate scale (`max_t <= min_t`, a typo) is repaired into a local
  `hi` var — your knobs are never mutated, the needle just gets a 1 °C span.
- Home Assistant auto-discovers `sensor.dial_temperature` from the retained
  publishes — point an HA gauge card at it to have the same number twice,
  physical and digital.
- Re-fit the horn so the servo's 0° is the left scale mark; the README's
  defaults assume the needle sweeps left→right as temperature rises.
- A held servo needs continuous pulses. Under `powerprofile light` the CPU
  pauses between DTIM beacons and pulses glitch (droop/jitter), so keep the
  profile at `full`/`balanced` while the needle must stay put.
