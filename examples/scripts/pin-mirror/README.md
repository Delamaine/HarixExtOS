# pin-mirror

Mirrors an input pin onto an output relay, with input edges published to MQTT by the
built-in `onchange` rule. A remote client overrides the relay by sending a **value** on
`shell/in` — the scheduled check script reads it and sets the pin from it.

## Wiring (NodeMCU v2)

```
3V3 ────[ slide switch ]──── D2 (GPIO4) ────[ 10k resistor ]──── GND
        external pull-down is required: "onchange add" sets plain INPUT (no internal pull)
GND ────[ relay module IN ]──────────────── D1 (GPIO5)   mirrored output (LED + 330R also fine)
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
onchange add 4 both
relay add 5 mirror
vars set force=2
vars save
notepad /scripts/pin-mirror.hx        # paste the contents of pin-mirror.hx, save
schedule add * * * * * * run /scripts/pin-mirror.hx
```

`<prefix>` is your `mqtt_prefix` setting (default `harixos/<chipid>`).

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/gpio/4` | `on` / `off` on every input edge (built-in `onchange` rule) |
| `<prefix>/relay/mirror/state` | relay state after every change (built-in relay class) |
| `<prefix>/availability` | `online` / `offline` |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set force=1'   # force relay on
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set force=0'   # force relay off
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set force=2'   # back to mirroring input
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'          # persist the override
mosquitto_pub -t '<prefix>/shell/in' -m 'relay set mirror on' # direct command (until next tick)
```

Monitor:

```sh
mosquitto_sub -t '<prefix>/gpio/4' -t '<prefix>/relay/mirror/state'
```

## Notes

- Send `vars save` after `vars set`: the script's `vars load` at the top of each run
  restores values from the saved file, which is also what survives a reboot.
- The check runs every second (cron `*` in the seconds field); commands take effect at
  the next tick. The script exits immediately so the device stays responsive — see the
  doorbell-remote README for why `while 1` scripts are a bad fit.
