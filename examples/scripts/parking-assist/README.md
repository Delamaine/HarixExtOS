# parking-assist

A garage parking aid: HC-SR04 distance drives a buzzer that beeps slowly under
`warn` cm and fast under `stop` cm. Both distances are remotely retunable. The
script reads the registered ultrasonic sensor's cached distance through the
`sensor <name> distance` value token.

## Wiring (NodeMCU v2)

```
HC-SR04 VCC ── 5V (USB)     HC-SR04 GND ── GND
HC-SR04 trig ───────────────────────────── D5 (GPIO14)
HC-SR04 echo ──[ 1k ]──┬──[ 2k ]── GND   (3.3V divider)
                       └───────────────── D6 (GPIO12)

D1 (GPIO5) ──[ 100R ]──[ buzzer + ]   [ buzzer − ] ── GND
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
sensor register ultrasonic 14 12 garage
notepad /scripts/parking-assist.hx    # paste the contents of parking-assist.hx, save
run /scripts/parking-assist.hx        # run once: init registers the schedule and defaults the distances
```

The script is self-contained: its `init` block registers the check schedule and
`vars setdefault` the distances once per boot. Distances you set remotely
survive reboots (the vars store auto-loads at boot) and are never overwritten
by setdefault. (`sensor register` still runs once in the shell — re-registering
reports a duplicate.)

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/sensor/garage/distance` | cm telemetry (every 2 s via `sensor publish`) |
| `<prefix>/availability` | `online` / `offline` |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set warn=120'  # start warning earlier
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set stop=40'   # fast-beep zone earlier
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'          # persist across reboots
```

Monitor:

```sh
mosquitto_sub -t '<prefix>/sensor/garage/distance'
```

## Notes

- Keep `stop < warn`: below `stop` the fast 2 kHz beep takes over from the
  slow 1 kHz one.
- The 2-second cadence is the ultrasonic refresh rate — the beeping tracks the
  car in near-real time.
- Home Assistant auto-discovers `sensor.garage_distance` from the retained
  publishes if you want a dashboard number.
