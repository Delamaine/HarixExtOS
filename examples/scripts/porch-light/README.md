# porch-light

[Back to index](../README.md)

A PIR-triggered porch light with a remotely tunable hold time. Motion sets an
epoch deadline (the `time` value token), and the light drops when the clock
passes it — no loops, the script exits every tick. The deadline lives in RAM,
so a reboot always starts with the light off.

## Wiring (NodeMCU v2)

```
PIR VCC ── 5V    PIR GND ── GND    PIR OUT ──────────── D7 (GPIO13)
GND ────[ relay module IN ]─────────────────── D1 (GPIO5)   porch light
```

The PIR output is push-pull 3.3 V (e.g. HC-SR501 with its voltage divider, or
any 3.3 V-logic module), so it is safe on a GPIO directly. `onchange add` sets
plain INPUT, which is fine because the PIR always drives its output.

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
notepad /scripts/porch-light.hx    # paste the contents of porch-light.hx, save
run /scripts/porch-light.hx        # run once: init registers the relay, the motion rule, the schedule and the defaults
```

The script is self-contained: its `init` block does `relay add`, `onchange add`,
`schedule add` and `vars setdefault` once per boot (all idempotent). It needs a
valid system time — NTP syncs automatically once Wi-Fi is up (the scheduler
requires it too).

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/gpio/13` | `on` / `off` on every PIR edge (HA binary_sensor `motion`) |
| `<prefix>/relay/light/state` | relay state after every change |
| `<prefix>/availability` | `online` / `offline` |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set secs=180'   # hold 3 minutes
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'           # persist across reboots
```

Monitor:

```sh
mosquitto_sub -t '<prefix>/relay/light/state' -t '<prefix>/gpio/13'
```

## Notes

- Motion keeps re-arming the deadline, so the light stays on while the PIR
  keeps triggering and drops `secs` seconds after the last edge.
- `until` is deliberately never saved: after a reboot it resets to 0 and the
  light stays off until the first motion — never auto-light after a power cut.
- The 1 s check re-latches the relay each tick (same pattern as pin-mirror);
  harmless, and it keeps the MQTT state fresh.
