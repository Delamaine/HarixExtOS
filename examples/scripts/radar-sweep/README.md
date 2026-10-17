# radar-sweep

[Back to index](../README.md)

An ultrasonic distance radar on a servo head. Every 2 s the script advances
the head to the next position (0° → 90° → 180° → 0°), lets it settle, then
refreshes the cached distance — so `<prefix>/sensor/radar/distance` traces the
room as the head rotates, and Home Assistant graphs it for free via discovery.

The head angle is `$step * 90`, passed to the hardware as `servo write 4 $pos`
— command arguments expand `$name`. A `while` sweep inside one run would
publish only the final distance, so the state advances across scheduled
ticks instead: one position (and one distance publish) per tick.

## Wiring (NodeMCU v2)

```
HC-SR04 VCC ── 5V (USB)     HC-SR04 GND ── GND
HC-SR04 trig ──────────────────────────── D5 (GPIO14)
HC-SR04 echo ──[ 1k ]──┬──[ 2k ]── GND   (3.3V divider)
                       └─────────────── D6 (GPIO12)

servo signal ──────────────────────────── D2 (GPIO4)
servo V+ (5V from USB)   servo GND ───── GND
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
sensor register ultrasonic 14 12 radar
notepad /scripts/radar-sweep.hx    # paste the contents of radar-sweep.hx, save
run /scripts/radar-sweep.hx        # run once: init attaches the servo and registers the sweep schedule
```

The script is self-contained: its `init` block attaches the servo
(`servo attach` re-attaches harmlessly), registers the schedule and defaults
the step index once per boot.

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/sensor/radar/distance` | cm at the current head position (every 2 s) |
| `<prefix>/availability` | `online` / `offline` |

A full three-position cycle takes 6 s.

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set step=0'   # park the head at 0 on the next tick
```

Monitor:

```sh
mosquitto_sub -t '<prefix>/sensor/radar/distance'
```

## Notes

- The `delay 350` after each write lets the servo settle before the ultrasonic
  read; without it the readings smear across the sweep.
- Home Assistant auto-discovers `sensor.radar_distance` (device class
  `distance`) from the retained publishes.
- Finer resolution: multiply smaller and wrap earlier — e.g.
  `set pos = $step * 30` with `if $step > 6` for seven positions.
  `vars setdefault step=0` keeps the head home after every reboot.
- A held servo needs continuous pulses. Under `powerprofile light` the CPU
  pauses between DTIM beacons and pulses glitch (droop/jitter), so keep the
  profile at `full`/`balanced` while the script runs or the head must hold.
