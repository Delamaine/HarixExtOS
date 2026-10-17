# lamp-remote

[Back to index](../README.md)

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
notepad /scripts/lamp-remote.hx       # paste the contents of lamp-remote.hx, save
run /scripts/lamp-remote.hx           # run once: the init block registers the lamp relay and both schedules
```

The script is self-contained: its `init` block registers the lamp relay (`relay add` is
idempotent) and the schedules once per boot. After a reboot the persisted schedule fires
again and re-runs init itself, so nothing else needs to be re-sent. (`sensor register`
still runs once in the shell — re-registering reports a duplicate.)

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
  commands. For pure remote control, delete the `if/else/end` block and the first
  `schedule add` line in the script's `init` block (keep the telemetry one).
- Flip the LDR/resistor positions and the comparison direction if your divider is inverted.
