# curtain-motor

[Back to index](../README.md)

A motorized curtain driven by the `motor` command family with limit switches.
`vars set cmd=open` (or `close`) drives m1 forward/reverse until the end
switch closes or `$travel` seconds pass — whichever comes first. The stop
check is a `while` loop with `break`, the engine's only loop construct,
demonstrated here with a real hardware job.

## Wiring (NodeMCU v2)

```
motor driver direction input ───────────── D5 (GPIO14)   HIGH = forward
motor driver speed / PWM input ─────────── D6 (GPIO12)   0-100 % duty
driver 5V + motor supply, common GND with the NodeMCU

open-limit switch ──┬───────────────────── D2 (GPIO4)  to GND + 10k pull-up to 3V3
                    │                      (closed = curtain fully open)
close-limit switch ─┴───────────────────── D7 (GPIO13) to GND + 10k pull-up to 3V3
                                           (closed = curtain fully closed)
```

- The driver must accept a direction + PWM input pair; the repo documents the
  motor family as "L293D H-bridge" (SCRIPT-REFERENCE.md, Motor API).
- Limit switches are active-LOW: `onchange add` forces plain INPUT, so the
  external pull-ups are required. Already-at-a-limit is detected as a closed
  switch and refuses to drive.

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
notepad /scripts/curtain-motor.hx   # paste the contents of curtain-motor.hx, save
run /scripts/curtain-motor.hx       # run once: init sets up m1, both limit rules, the schedule and the defaults
```

The script is self-contained: `motor init`, `onchange add`, `schedule add` and
`vars setdefault` run once per boot inside `init`. (`motor init` is safe here
because the block executes once per boot; it is not idempotent on re-run.)

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/gpio/4` | open-limit edges (HA binary_sensor `window`) |
| `<prefix>/gpio/13` | close-limit edges (HA binary_sensor `window`) |
| `<prefix>/availability` | `online` / `offline` |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set cmd=open'    # raise the curtain
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set cmd=close'   # lower it
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set travel=12'   # longer travel budget (s)
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set pct=50'      # slower motor (0-100 %)
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'            # persist travel/pct (cmd stays RAM-only)
```

## Notes

- A travel run owns the CPU for up to `$travel` seconds: shell, web UI and
  scheduler wait until it finishes. The `while` loop feeds the watchdog every
  pass and stays far inside the engine's 5 min / 100 000-iteration caps.
- `cmd` resets to `idle` at boot on purpose — never auto-drive a curtain after
  a power cut; drive it explicitly from HA or the shell.
- Motor speed comes from `$pct` (`motor speed $pct m1`) — command arguments
  expand `$vars`, so the knob is tuneable at runtime; the default is set in
  `init` with `vars setdefault pct=75`.
- `motor brake` (fast stop) ends each travel; `motor stop` (coast) is used
  when already at a limit and nothing is moving.
