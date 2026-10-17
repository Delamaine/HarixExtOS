# scene-ack

[Back to index](../README.md)

The full remote-knob round trip: you set `scene` over MQTT, the device
actuates its relays **and confirms** the applied value on
`stat/harixos/scene` — so the dashboard knows the scene landed, not just
that the command was sent. Change detection is a `prev` var compare (the
same edge pattern as doorbell-remote, aimed inward instead of at a pin).

## Wiring (NodeMCU v2)

```
lamp relay input ───────────────────────── D1 (GPIO5)
strip relay input ──────────────────────── D6 (GPIO12)
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
notepad /scripts/scene-ack.hx   # paste the contents of scene-ack.hx, save
run /scripts/scene-ack.hx       # run once: init registers both relays, the schedule and the defaults
```

The script is self-contained: its `init` block registers the relays and the
scene defaults once per boot.

## Scenes

| `scene` | Effect |
|---|---|
| `0` | everything off |
| `1` | lamp on |
| `2` | lamp + strip on |
| anything else | nothing actuated — still acked |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set scene=2'   # movie time
mosquitto_pub -t 'stat/harixos/scene' -m '' --wait           # (subscribe instead, see below)
```

Monitor the ack:

```sh
mosquitto_sub -t 'stat/harixos/scene'
```

## What the device publishes

| Topic | Content |
|---|---|
| `stat/harixos/scene` | applied scene value per change (raw pub, fixed topic) |
| `<prefix>/relay/lamp`, `<prefix>/relay/strip` | HA switch state per actuation |
| `<prefix>/availability` | `online` / `offline` |

## Notes

- `mqtt pub stat/harixos/scene $scene` — strict `$name` expansion; `scene`
  always exists thanks to `vars setdefault`.
- `prev=-1` via `vars setdefault`: first run after boot always actuates
  (restores the scene if the relays rebooted off). If you `vars save`, the
  old `prev` survives and the scene re-applies only on the next change —
  either behavior is defensible; pick one deliberately.
- Add scenes by adding `if $scene == N` blocks; the engine's nesting cap
  (8) is plenty.
- Pair with **remote-button** by pointing its payload at
  `<prefix>/shell/in` with `vars set scene=1`.
