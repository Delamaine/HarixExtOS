# leak-webhook

[Back to index](../README.md)

A water-leak notifier: every 30 s the script reads the registered soil
sensor's cached moisture through the `sensor <name> moisture` value token and
posts an HTTP webhook the moment the reading crosses a remotely tunable
threshold — wet floor, burst pipe, overflowing sump.

The post is **edge-triggered**: a RAM-only `active` flag fires the webhook
once per wet episode and re-arms when the sensor dries, so a soaked sensor
does not spam your phone every 30 s.

## Wiring (NodeMCU v2)

```
3V3 ────[ capacitive soil sensor VCC ]   sensor GND ── GND
sensor AOUT ───────────────────────────── A0            dry = high reading
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
sensor register soil A0 sump
notepad /scripts/leak-webhook.hx    # paste, then edit the post URL to your own ntfy/ntfy.sh topic (or any HTTP endpoint), save
run /scripts/leak-webhook.hx        # run once: init registers the check schedule and defaults
```

The script is self-contained: its `init` block registers the schedule and
`vars setdefault` the threshold once per boot. (`sensor register` runs once
in the shell — re-registering reports a duplicate.)

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/sensor/sump/moisture` | moisture reading (every 30 s via `sensor publish`) |
| `<prefix>/availability` | `online` / `offline` |

Plus one webhook POST per wet episode.

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set wet=350'   # trip earlier (wetter)
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'          # persist across reboots
```

Monitor:

```sh
mosquitto_sub -t '<prefix>/sensor/sump/moisture'
```

## Notes

- Calibrate: dip the probe and watch `<prefix>/sensor/sump/moisture`, then pick
  `wet` above the fully-dry reading and below the wet reading (default 400).
- `post` expands `$vars` **leniently** — defined vars substitute, undefined
  `$name` stays literal so JSON survives. The body
  `{"event":"leak","raw":"$reading"}` embeds the live reading:
  `set reading = $raw` copies it, then `$reading` substitutes in the post.
- `active` is RAM-only on purpose: after a reboot a still-wet sensor posts
  once more (a feature — power-cut leaks get reported), then latches again.
- Try it with a wet finger on the probe; subscribe to the ntfy topic on your
  phone to see the push.
