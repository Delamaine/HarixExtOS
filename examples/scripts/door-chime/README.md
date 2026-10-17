# door-chime

[Back to index](../README.md)

A door melody instead of a single beep: every closed→open edge plays a
short `while`-driven tune — `$notes` blips at 880 Hz with `$gap` ms between
them, then a longer 1175 Hz finish. Mute it from the couch with
`vars set enabled=0` over MQTT; the toggle is RAM-only, so the chime is
back after a reboot. The reed edges also reach Home Assistant via
`onchange` (HA binary_sensor class `door`) whether the chime is muted or
not.

## Wiring (NodeMCU v2)

```
reed switch ──┬─────────────────────────── D2 (GPIO4)  to GND + 10k pull-up to 3V3
              │                             (closed = door shut)
buzzer + ──[ 100R ]── D5 (GPIO14)          buzzer − ── GND
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
notepad /scripts/door-chime.hx   # paste the contents of door-chime.hx, save
run /scripts/door-chime.hx       # run once: init registers the onchange rule, the schedule and the defaults
```

The script is self-contained: its `init` block registers the edge rule, the
2 s poll schedule and the chime defaults once per boot. (`onchange add` is
not idempotent for a pin already registered — a reboot reloads the saved
rule cleanly; a bare re-run of the init line reports the duplicate.)

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/gpio/4` | door edges (HA binary_sensor `door`) |
| `<prefix>/availability` | `online` / `offline` |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set enabled=0'   # mute the chime
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set enabled=1'   # unmute
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set notes=4'    # longer melody
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set gap=250'    # lazier tempo
```

## Notes

- The melody is a `while` loop with a countdown var — the engine's only
  loop, kept tiny (max 5 notes after the clamp) so the poll cycle stays
  short.
- `prev` is RAM-only on purpose: a mid-episode reboot replays the chime
  once (door still open), then latches — same trade-off as doorbell-remote.
- Muting (`enabled=0`) silences only the melody; MQTT/HA still see every
  edge.
- The poll schedule (2 s) plus edge rule duplicates doorbell-remote on
  purpose — this example is about the melody and the mute knob, not the
  edge plumbing.
