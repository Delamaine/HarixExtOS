# fan-control

A temperature-controlled fan: DS18B20 readings drive a relay with hysteresis
(on above `on_at`, off below `off_at`), and both switch points are remotely
retunable. The script reads the registered DS18B20's cached temperature through
the `sensor <name> temperature` value token.

## Wiring (NodeMCU v2)

```
3V3 ────[ 4.7k resistor ]──── D2 (GPIO4) ────[ DS18B20 data ]
                    │                        DS18B20 VDD ── 3V3
                  GND (idle)                 DS18B20 GND ── GND

GND ────[ relay module IN ]────────────────── D1 (GPIO5)   fan on/off
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
sensor register ds18b20 4 room
notepad /scripts/fan-control.hx    # paste the contents of fan-control.hx, save
run /scripts/fan-control.hx        # run once: init registers the fan relay, the schedule and defaults the switch points
```

The script is self-contained: its `init` block registers the fan relay (`relay add`
is idempotent), the check schedule and `vars setdefault` the switch points once
per boot. Switch points you set remotely survive reboots (the vars store
auto-loads at boot) and are never overwritten by setdefault. (`sensor register`
still runs once in the shell — re-registering reports a duplicate.)

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/sensor/room/temperature` | °C telemetry (every 2 min via `sensor publish`) |
| `<prefix>/relay/fan/state` | relay state after every change |
| `<prefix>/availability` | `online` / `offline` |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set on_at=28'    # start earlier
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set off_at=25'   # stop earlier
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'            # persist across reboots
mosquitto_pub -t '<prefix>/shell/in' -m 'relay set fan on'     # manual override (until next tick)
```

Monitor:

```sh
mosquitto_sub -t '<prefix>/sensor/room/temperature' -t '<prefix>/relay/fan/state'
```

## Notes

- Hysteresis (`on_at` > `off_at`) is the whole point: without it the relay
  would chatter around a single setpoint. Keep `off_at < on_at`.
- Home Assistant auto-discovers `sensor.room_temperature` (device class
  `temperature`) from the retained publishes.
- A manual `relay set` is re-evaluated at the next scheduled tick (2 min).
