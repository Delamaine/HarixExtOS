# pet-feeder

An automatic pet feeder: a servo-driven hopper opens at 07:30 and 18:30 for
`$hold` milliseconds, then closes. Portion size is the open angle
(`$open_angle`), dwell is the hold time — both are `$vars` knobs, so you
retune dinner over MQTT without editing the script. The simplest timed
`servo` + cron example in the repo.

## Wiring (NodeMCU v2)

```
servo signal ──────────────────────────── D2 (GPIO4)
servo V+ (5V from USB)   servo GND ───── GND
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
notepad /scripts/pet-feeder.hx   # paste the contents of pet-feeder.hx, save
run /scripts/pet-feeder.hx       # run once: init attaches the servo and registers both cron jobs
```

The script is self-contained: its `init` block attaches the servo
(`servo attach` re-attaches harmlessly), registers the schedule and the
portion defaults once per boot. Cron needs a valid system time; NTP syncs
automatically once Wi-Fi is up.

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/availability` | `online` / `offline` |

Meals are pure actuation — nothing is published per feed.

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set hold=2500'   # bigger portion dwell
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set open_angle=120'  # wider hopper opening
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'            # persist both knobs
mosquitto_pub -t '<prefix>/shell/in' -m 'run /scripts/pet-feeder.hx'   # feed now
```

## Notes

- `servo write 4 $open_angle` and `delay $hold` are the arg-expansion
  feature in action — both values come from the vars store.
- Skip a meal without editing anything: `schedule list` →
  `schedule remove <id>`.
- If the servo buzzes at rest, add `delay $hold` before the `servo write 4 0`
  return — already in the script; remove it only if your servo handles the
  full-travel snap.
