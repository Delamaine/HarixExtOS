# sos-alarm

[Back to index](../README.md)

A panic button with a rich push: one press sends an **urgent** ntfy
notification with a title and tags in a single JSON body — the ntfy
publish-in-JSON form (`POST https://ntfy.sh` with a `topic` field, not the
topic-in-URL form leak-webhook uses). A cooldown knob stops a bouncing or
held button from flooding the topic. The button pin is a `$vars`-free zone —
the knobs are `topic` and `cooldown`.

## Wiring (NodeMCU v2)

```
momentary button ──┬──────────────────── D2 (GPIO4)  to GND + 10k pull-up to 3V3
                   │                      (released = HIGH, pressed = LOW)
button other side ─┘
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
notepad /scripts/sos-alarm.hx    # paste the contents of sos-alarm.hx, save
run /scripts/sos-alarm.hx        # run once: init registers the edge rule, schedule and defaults
```

Subscribe on your phone (ntfy app) to topic `harixos-sos` — or whatever you
set in `topic`.

## What the device publishes

| Destination | Content |
|---|---|
| `https://ntfy.sh` (topic via `$topic`) | `{"topic":"…","title":"SOS","message":"help","priority":"urgent","tags":["warning","sos"]}` per press |
| `<prefix>/gpio/4` | button edges (HA binary_sensor `door`) |
| `<prefix>/availability` | `online` / `offline` |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set topic=my-secret-panic'  # other ntfy topic
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set cooldown=300'           # 5-min refractory period
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'                       # persist both
mosquitto_pub -t '<prefix>/shell/in' -m 'run /scripts/sos-alarm.hx'       # test fire (if button released == no edge; toggle the pin or press it)
```

## Notes

- The JSON body has **no spaces** — `post` splits arguments on spaces, so
  `"message":"button pressed"` would truncate the body. Keep every JSON
  string space-free.
- `cooldown` compares epoch seconds (`time`), so it needs NTP; until the
  clock syncs, `time` is small and the first press may be swallowed —
  after sync everything behaves.
- `last` and `prev` are RAM-only: a reboot re-arms the button immediately.
- For a self-hosted ntfy server, change the URL host; the JSON form works
  the same.
