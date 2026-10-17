# greenhouse-alert

[Back to index](../README.md)

A DHT22 over-limit alarm for a greenhouse corner: every 5 minutes the script
reads the registered sensor's cached temperature and humidity through the
`sensor <name> <quantity>` value tokens and beeps when either limit is
exceeded. Both limits are remotely retunable.

## Wiring (NodeMCU v2)

```
3V3 ────[ 10k resistor ]──── D2 (GPIO4) ────[ DHT22 data ]
                    │                       DHT22 VDD ── 3V3
                  GND (idle)                DHT22 GND ── GND

D5 (GPIO14) ──[ 100R ]──[ buzzer + ]   [ buzzer − ] ── GND
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
sensor register dht22 4 greenhouse
notepad /scripts/greenhouse-alert.hx    # paste the contents of greenhouse-alert.hx, save
run /scripts/greenhouse-alert.hx        # run once: init registers the schedule and defaults the limits
```

The script is self-contained: its `init` block registers the check schedule and
`vars setdefault` the limits once per boot. Limits you set remotely survive
reboots (the vars store auto-loads at boot) and are never overwritten by
setdefault. (`sensor register` still runs once in the shell — re-registering
reports a duplicate.)

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/sensor/greenhouse/temperature` | °C telemetry (every 5 min via `sensor publish`) |
| `<prefix>/sensor/greenhouse/humidity` | %RH telemetry (same publish) |
| `<prefix>/availability` | `online` / `offline` |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set max_temp=28'  # tighter heat limit
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set max_hum=60'   # tighter humidity limit
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'             # persist across reboots
```

Monitor:

```sh
mosquitto_sub -t '<prefix>/sensor/greenhouse/temperature' -t '<prefix>/sensor/greenhouse/humidity'
```

## Notes

- The two `if` blocks use different beep pitches (1200 Hz heat, 1600 Hz humidity)
  so the alarm is identifiable by ear. `beep` blocks for its duration — fine at
  a 5-minute cadence.
- Home Assistant auto-discovers both `sensor.greenhouse_temperature` and
  `sensor.greenhouse_humidity` from the retained publishes.
- DHT22 samples slowly; the 5-minute cadence matches its comfortable rate.
