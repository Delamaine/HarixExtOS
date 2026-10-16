# sun-blind

A servo-driven blind that opens when the room gets bright and closes in
darkness, with both thresholds remotely retunable. The script reads the
registered `ldr` sensor's cached illuminance through the `sensor <name> illuminance`
value token — the same cache Home Assistant sees via MQTT discovery.

## Wiring (NodeMCU v2)

```
3V3 ────[ LDR ]──── A0 ────[ 10k resistor ]──── GND     bright = high light level
                    │
                 (analog in)

servo signal ────────────────────────────────── D2 (GPIO4)
servo V+ (5V from USB)   servo GND ─────────── GND
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
sensor register ldr A0 sun
notepad /scripts/sun-blind.hx    # paste the contents of sun-blind.hx, save
run /scripts/sun-blind.hx        # run once: init attaches the servo, registers the schedule and defaults the thresholds
```

The script is self-contained: its `init` block attaches the servo (`servo attach`
re-attaches harmlessly), registers the schedule and `vars setdefault` the
thresholds once per boot. Thresholds you set remotely survive reboots (the vars
store auto-loads at boot) and are never overwritten by setdefault. (`sensor
register` still runs once in the shell — re-registering reports a duplicate.)

Calibrate: cover/uncover the LDR and watch `<prefix>/sensor/sun/illuminance`
(0-100 %), then pick `dark` below the dark-side reading and `bright` above the
bright-side reading (setup defaults: 20 / 60).

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/sensor/sun/illuminance` | light level % (every 10 min via `sensor publish`) |
| `<prefix>/availability` | `online` / `offline` |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set bright=45'   # open earlier
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'            # persist across reboots
```

Monitor:

```sh
mosquitto_sub -t '<prefix>/sensor/sun/illuminance'
```

## Notes

- The blind only moves when the light level crosses a threshold (hysteresis
  between `dark` and `bright`), so a servo does not twitch every tick.
- Home Assistant auto-discovers `sensor.sun_illuminance` (device class
  `illuminance`) from the retained publishes — no script-side HA work needed.
- Servo jitter: the servo holds its last angle between runs; only threshold
  crossings write a new angle.
