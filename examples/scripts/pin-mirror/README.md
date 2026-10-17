# pin-mirror

[Back to index](../README.md)

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
notepad /scripts/pin-mirror.hx        # paste the contents of pin-mirror.hx, save
run /scripts/pin-mirror.hx            # run once: init registers the mirror relay, the onchange rule, the 1 s schedule, force=2
```

The script is self-contained: its `init` block does `relay add`, `onchange add`, `schedule add` and
`vars setdefault force=2` once per boot (all idempotent). After a reboot the persisted
schedule re-fires and re-runs init itself.

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

- Send `vars save` after `vars set`: the vars store auto-loads at boot, so the saved
  `force` survives reboots and the script's `vars setdefault` leaves it untouched.
- The check runs every second (cron `*` in the seconds field); commands take effect at
  the next tick. The script exits immediately so the device stays responsive — see the
  doorbell-remote README for why `while 1` scripts are a bad fit.
