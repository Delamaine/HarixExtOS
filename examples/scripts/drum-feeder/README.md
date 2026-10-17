# drum-feeder

[Back to index](../README.md)

A feeder for **large biscuits** where "exactly N biscuits" is a mechanical
guarantee, not a timing bet — the pet-feeder's servo flap can only open for
so many milliseconds, and with big biscuits that delivers zero, one or three.
Here a rotary-valve drum does the counting: each cycle parks a single pocket
under the hopper throat (biscuit settles in), swings it over the exit chute
(biscuit falls out), and the pocket physically holds **one** biscuit — a
cycle delivers one or none, never two. Portion size is `$count` cycles.

## How the mechanism works

```
        hopper (one-biscuit-wide throat)
              │
      ┌───────▼───────┐
      │   [ pocket ]  │  drum, pocket lip up   servo D2
      └───────────────┘
              │
        exit chute → bowl
```

- `$load_angle` (default 0): pocket under the throat — fits the horn before
  first power-up; `$settle` ms gives the biscuit time to drop in.
- `$deliver_angle` (default 180): pocket swung over the chute — `$pass` ms
  for the fall. The pocket needs a lip so the biscuit survives the swing
  until it faces down.
- Failure mode is fail-short: a bridged throat delivers **zero** (top the
  hopper up and re-run), because the pocket can't carry two.

## Wiring (NodeMCU v2)

```
drum servo signal ─────────────────────────── D2 (GPIO4)
servo V+ ── 5V, servo GND ── GND (common)
```

No sensors — the count is the mechanism.

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
notepad /scripts/drum-feeder.hx   # paste the contents of drum-feeder.hx, save
run /scripts/drum-feeder.hx       # run once: init attaches the drum, registers the 07:30/18:30 cron jobs and the defaults
```

## Portions and feeding

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set count=3'   # big dinner
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'          # persist the portion
mosquitto_pub -t '<prefix>/shell/in' -m 'run /scripts/drum-feeder.hx'   # feed now, on top of cron
```

`count` is clamped to 1-12 per run; each cycle takes `$settle + $pass`
(1.4 s at the defaults), so even 12 biscuits finish in ~17 s.

## Notes

- The cron jobs (07:30 / 18:30) need valid system time — NTP syncs once
  Wi-Fi is up. Retune via `schedule list` → `schedule remove <id>` →
  `schedule add` (or pin an id: `schedule add id=7 ...`).
- `$load_angle` / `$deliver_angle` are calibration knobs: fit the horn at
  load, run `servo write 4 180` from the shell and rotate the horn to the
  chute by hand before tightening — that pair of positions is the whole
  timing-free contract.
- Comparisons: **pet-feeder** (timed flap, fine for kibble), **drum-feeder**
  (positive displacement, for anything irregular or large).
- The script detaches the servo after the last cycle, so the horn is limp
  between feeds: no holding-torque draw (~10-100 mA) and no pulse glitches
  under `powerprofile light`. Delete the detach line to keep the drum
  gripping.
