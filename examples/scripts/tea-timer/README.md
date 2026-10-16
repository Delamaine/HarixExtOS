# tea-timer

A brew timer with a real appliance relay: the kettle (or hot plate) stays on
for `$minutes`, then switches off with a beep. The whole point is the
`delay $ms` line — command arguments expand `$vars`, so the duration is a
knob you retune over MQTT instead of editing the script. The script clamps
`minutes` to 1–16 (values above ~16 format in scientific notation the delay
parser cannot read).

## Wiring (NodeMCU v2)

```
kettle relay input ────────────────────── D1 (GPIO5)
buzzer + ──[ 100R ]── D5 (GPIO14)        buzzer − ── GND
relay load side switches the appliance live; mains wiring is your responsibility
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
notepad /scripts/tea-timer.hx   # paste the contents of tea-timer.hx, save
run /scripts/tea-timer.hx       # run once: init registers the kettle relay and the minutes default
```

The script is self-contained: its `init` block registers the relay and the
duration default once per boot.

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/relay/kettle` | `on` / `off` per brew |
| `<prefix>/availability` | `online` / `offline` |

Plus `kettle on for N min` to the console / web UI via `print`.

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set minutes=5'           # 5-minute steep
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'                    # persist it
mosquitto_pub -t '<prefix>/shell/in' -m 'run /scripts/tea-timer.hx'    # start the brew
```

## Notes

- `set ms = $minutes * 60000` shows the pattern for big durations: build the
  millisecond value in an expression, then `delay $ms` — `delay` itself
  takes no arithmetic.
- A running brew blocks the scheduler and shell for the full duration
  (`delay` is synchronous); the watchdog is fed throughout by the delay
  implementation.
- The beep is your "brew finished" signal — run it where you can hear it.
