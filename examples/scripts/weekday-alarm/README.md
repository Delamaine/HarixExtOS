# weekday-alarm

A weekday wake-up alarm: three beeps plus the lamp at 07:00 Mon-Fri, lamp off
again at 07:30. The example teaches hour- and dow-restricted cron — every other
example only uses `*/N` intervals — and running a script (not a single command)
from a scheduled job.

Cron recap (6 fields): `sec min hour dom month dow`, dow `0` = Sunday, so
`1,2,3,4,5` is Mon-Fri. `dom` and `dow` are AND-ed when both restrict (unlike
Linux cron); here `dom` is `*`, so only the dow list applies.

## Wiring (NodeMCU v2)

```
GND ────[ relay module IN ]───────────────── D1 (GPIO5)   lamp
D5 (GPIO14) ──[ 100R ]──[ buzzer + ]   [ buzzer − ] ── GND
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
time sync 'CET-1CEST,M3.5.0,M10.5.0/3'   # optional: set your POSIX/IANA tz
notepad /scripts/weekday-alarm.hx        # paste the contents of weekday-alarm.hx, save
run /scripts/weekday-alarm.hx            # run once: init registers both cron jobs and the relay
```

The script is self-contained: its `init` block registers the relay and both
schedule entries once per boot. A valid system time is required — NTP syncs
automatically once Wi-Fi is up; before that the scheduler prints
`no valid system time, cron not running` and fires nothing.

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/relay/lamp/state` | relay state after every change |
| `<prefix>/availability` | `online` / `offline` |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'schedule list'                       # find the job ids
mosquitto_pub -t '<prefix>/shell/in' -m 'schedule remove 1'                   # drop the 07:00 job
mosquitto_pub -t '<prefix>/shell/in' -m 'schedule add 0 0 6 * * 1,2,3,4,5 run /scripts/weekday-alarm.hx'   # wake at 06:00
```

(Schedule ids are reassigned in file order at boot — check `schedule list`
before removing. Scheduled tasks auto-save; there is no `schedule save`.)

## Notes

- The script blocks for ~1.2 s while beeping (three 300 ms beeps + 400 ms
  gaps) — fine for a once-a-day job.
- The off-at-07:30 job is a plain scheduled single command (`relay set lamp
  off`), showing that the scheduler runs both whole scripts and one-liners.
- Change the alarm sound by editing the `beep` lines (hz 37-5000, ms 10-10000).
