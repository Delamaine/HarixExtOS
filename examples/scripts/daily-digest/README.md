# daily-digest

[Back to index](../README.md)

A zero-hardware daily health digest: one cron job at 08:00 prints a one-line
summary (free heap, uptime, epoch) to the console / web UI and posts the
same numbers as JSON to an ntfy topic. It is also the `print` example —
`print` and `post` expand `$vars` leniently: defined vars substitute, an
undefined `$name` stays literal, so the JSON braces and quotes in the body
are never touched.

## Wiring

None — pure software.

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
notepad /scripts/daily-digest.hx   # paste the contents of daily-digest.hx, save
run /scripts/daily-digest.hx       # run once: init registers the 08:00 cron job
```

The script is self-contained: its `init` block registers the schedule and
the ntfy topic default once per boot. Cron needs a valid system time; NTP
syncs automatically once Wi-Fi is up.

## What the device publishes

| Topic | Content |
|---|---|
| ntfy topic (default `harixos-digest`) | `{"heap":"…","uptime":"…","time":"…"}` daily at 08:00 |
| `<prefix>/availability` | `online` / `offline` |

Plus a matching line to the console / web UI via `print`.

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set topic=my-digest'  # other ntfy topic
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'                 # persist it
mosquitto_pub -t '<prefix>/shell/in' -m 'run /scripts/daily-digest.hx'   # test fire now
mosquitto_pub -t '<prefix>/shell/in' -m 'schedule add 0 0 18 * * * run /scripts/daily-digest.hx'  # also at 18:00
```

## Notes

- Retune the hour the usual way: `schedule list` → `schedule remove <id>` →
  `schedule add <new six fields> <command>`.
- The digest prints **and** posts from the same three vars — no duplicated
  `set` blocks.
- `heap` and `uptime` are value tokens; they only exist after the `set`
  lines, which is why the script copies them into vars first (value tokens
  never expand in arguments, only in expressions).
