# rain-closure

[Back to index](../README.md)

Rain closes the awning — automatically, once per shower. A moisture probe on
the fabric is polled every 5 s; the moment the reading crosses the
remotely-tunable `$wet` threshold the script posts a webhook
(`{"event":"rain","raw":"…"}` — the body embeds the live reading via
lenient `$vars` expansion) and runs one motor reverse travel until the
close-limit switch or `$travel` seconds. A RAM-only `closed` flag makes it
edge-triggered: a soaked sensor does not re-run the travel every 5 s.
Reopening is always deliberate — `vars set cmd=open` — never automatic; if
rain returns while open, the next poll closes again.

## Wiring (NodeMCU v2)

```
motor driver direction input ───────────── D5 (GPIO14)   HIGH = forward (open)
motor driver speed / PWM input ─────────── D6 (GPIO12)   0-100 % duty
driver 5V + motor supply, common GND with the NodeMCU

moisture probe VCC ── 3V3   GND ── GND
probe AOUT ─────────────────────────────── A0            dry = high reading
open-limit switch ──┬───────────────────── D2 (GPIO4)  to GND + 10k pull-up to 3V3
close-limit switch ─┴───────────────────── D7 (GPIO13) to GND + 10k pull-up to 3V3
```

- Same probe convention as leak-webhook: capacitive/resistive, **dry = high
  reading**, so `wet = raw < wet_threshold`.
- Limit switches are active-LOW: `onchange add` forces plain INPUT, so the
  external pull-ups are required. Already-at-a-limit is detected as a closed
  switch and refuses to drive.
- The driver must accept a direction + PWM input pair (L293D family,
  SCRIPT-REFERENCE.md, Motor API).

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
notepad /scripts/rain-closure.hx   # paste, then edit the post URL to your own ntfy topic, save
run /scripts/rain-closure.hx       # run once: init sets up m1, the probe, both limit rules, the schedule and the defaults
```

The script is self-contained: `motor init`, `sensor register`, `onchange
add`, `schedule add` and `vars setdefault` run once per boot inside `init`.
(`motor init` is safe here because the block executes once per boot; it is
not idempotent on re-run.)

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/sensor/rain/moisture` | probe reading (every 5 s via `sensor publish`) |
| `<prefix>/gpio/4` | open-limit edges (HA binary_sensor `window`) |
| `<prefix>/gpio/13` | close-limit edges (HA binary_sensor `window`) |
| `<prefix>/availability` | `online` / `offline` |

Plus one webhook POST per wet episode.

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set cmd=open'    # manual reopen
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set wet=350'     # trip on lighter rain
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set pct=50'      # gentler travel
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'            # persist wet/travel/pct -- only while dry!
```

## Notes

- A close travel owns the CPU for up to `$travel` seconds: shell, web UI
  and scheduler wait until it finishes. The `while` loop feeds the watchdog
  every pass and stays far inside the engine's caps.
- **`vars save` only while dry** — the whole store is saved, including the
  `closed` latch; persisting `closed=1` would stop the auto-close after a
  power cut mid-shower.
- The webhook body embeds the trip reading: `set raw` → `post …
  "raw":"$raw"` — `post` expands `$vars` leniently, so the JSON braces
  survive.
- Calibration: watch `<prefix>/sensor/rain/moisture` dry vs. wet, then pick
  `wet` between the two (default 400).
