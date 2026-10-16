# doorbell-remote

Reed-switch door sensor on D2. Open/close edges reach MQTT through the built-in
`onchange` rule (`<prefix>/gpio/4`, plus a Home Assistant binary_sensor with device
class `door`) — no script needed for that. A short check script, run by the built-in
scheduler every 2 s, beeps the local buzzer when the door opens; it exits at once, so
the shell, web UI and telemetry stay responsive between ticks.

## Wiring (NodeMCU v2)

```
GND ────[ reed switch ]──── D2 (GPIO4) ────[ 10k resistor ]──── 3V3
        reed closed when door shut; the external pull-up is required because
        "onchange add" sets plain INPUT (no internal pull-up)
GND ────[ buzzer − ]   D5 (GPIO14) ──[ buzzer + ]── 3V3
GND ────[ relay module IN ]──────────── D1 (GPIO5)   optional bell/lock relay
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
notepad /scripts/doorbell-remote.hx   # paste the contents of doorbell-remote.hx, save
run /scripts/doorbell-remote.hx       # run once: init registers the bell relay, the onchange rule, the schedule, prev=0
```

The script is self-contained: its `init` block does `relay add`, `onchange add`, `schedule add` and
`vars setdefault prev=0` once per boot (all idempotent — re-adding a relay or rule that already
exists is a no-op). After a reboot the persisted schedule re-fires and re-runs init itself.

Replace `harixos` with your own prefix (check `mqtt settings`).

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/gpio/4` | `on` / `off` on every reed edge (built-in `onchange` rule) |
| `<prefix>/relay/bell/state` | relay state after every change |
| `<prefix>/availability` | `online` / `offline` |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'beep 14 880 300'      # beep buzzer 300 ms
mosquitto_pub -t '<prefix>/shell/in' -m 'relay set bell on'    # ring/lock remotely
mosquitto_pub -t '<prefix>/shell/in' -m 'relay set bell off'
```

Monitor:

```sh
mosquitto_sub -t '<prefix>/gpio/4' -t '<prefix>/relay/bell/state'
```

## Notes

- Cron schedules need a valid system time; it syncs automatically via NTP when Wi-Fi
  connects. Schedule entries persist across reboots (schedule.cfg auto-reloads), and
  the door state persists through the script's `vars save` (the vars store auto-loads at boot).
- Why not a `while 1` script: the engine hard-caps while loops (5 min / 100 000
  iterations) and a running script blocks shell, web and MQTT telemetry. Short
  scheduled checks share the device with everything else.
- Script-free variant: drop the schedule entry and let a Home Assistant automation on
  `<prefix>/gpio/4` publish `beep 14 880 300` back to `shell/in`.
