# remote-button

[Back to index](../README.md)

The missing cross-device link: a button on one NodeMCU drives a vars knob on
another, with no wiring between them. Every press publishes
`vars set cmd=open` to the target device's `shell/in` topic over MQTT —
literally the same command you would type yourself. Pair it with
**curtain-motor**, **lamp-remote** or anything else that watches a var.
The target topic is a `$vars` knob, so one script serves any pair.

## Wiring (NodeMCU v2)

```
momentary button ──┬──────────────────── D2 (GPIO4)  to GND + 10k pull-up to 3V3
                   │                      (released = HIGH, pressed = LOW)
button other side ─┘
```

The script fires on the press edge (released → pressed).

## Setup (button device)

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
notepad /scripts/remote-button.hx    # paste, set the target prefix, save
vars set target=curtain-node/shell/in
run /scripts/remote-button.hx        # run once: init registers the edge rule, schedule and target
```

The target device needs its own example already running (curtain-motor's
knob is `cmd`, lamp-remote's is `raw`-driven — point the payload at
whatever it watches).

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/gpio/4` | button edges (HA binary_sensor `door`) |
| `<target>` (e.g. `curtain-node/shell/in`) | `vars set cmd=open` per press |
| `<prefix>/availability` | `online` / `offline` |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set target=other/shell/in'  # retarget
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'                       # persist the target
mosquitto_pub -t '<prefix>/shell/in' -m 'run /scripts/remote-button.hx'   # test fire path
```

## Notes

- `mqtt pub $target vars set cmd=open` — the topic expands `$vars`; the
  payload keeps its spaces because `mqtt pub` takes everything after the
  first token as payload.
- Change the payload line to anything the other device's shell understands —
  `vars set cmd=close`, `relay set lamp on`, `reboot`. It is one line.
- `prev` is RAM-only: a reboot mid-press replays the command once, then
  latches — same trade-off as doorbell-remote.
- The other device must already be subscribed (`mqtt start`); publishing to
  a dead subscriber is a silent no-op.
