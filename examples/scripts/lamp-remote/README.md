# lamp-remote

Publishes the A0 light reading as MQTT telemetry and switches a lamp relay. A short
scheduled check keeps the lamp on when dark as local fallback; a remote client can
command the lamp directly.

## Wiring (NodeMCU v2)

```
3V3 ────[ LDR ]──── A0 ────[ 10k resistor ]──── GND     bright = high reading
GND ────[ relay module IN ]──────────────────── D1 (GPIO5)  lamp on/off
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
sensor register ldr A0 light
relay add 5 lamp
notepad /scripts/lamp-remote.hx       # paste the contents of lamp-remote.hx, save
schedule add */30 * * * * * run /scripts/lamp-remote.hx
schedule add 0 */5 * * * * * sensor publish light
```

Calibrate the dark threshold: watch readings with `adc` on the shell, then adjust the
`if $raw <= 150` line.

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/sensor/light` | A0 light telemetry (every 5 min via schedule) |
| `<prefix>/relay/lamp/state` | relay state after every change |
| `<prefix>/availability` | `online` / `offline` |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'relay set lamp on'
mosquitto_pub -t '<prefix>/shell/in' -m 'relay set lamp off'
mosquitto_pub -t '<prefix>/shell/in' -m 'sensor publish light'   # ask for a fresh reading
```

Monitor:

```sh
mosquitto_sub -t '<prefix>/sensor/light' -t '<prefix>/relay/lamp/state'
```

## Notes

- The scheduled dark-detection re-latches the relay every 30 s, overriding remote
  commands. Delete the `if/else/end` block (and its schedule entry) for pure remote control.
- Flip the LDR/resistor positions and the comparison direction if your divider is inverted.
