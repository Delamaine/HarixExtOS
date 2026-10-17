# ha-webhook

[Back to index](../README.md)

Talk to Home Assistant **without** MQTT discovery: the door state arrives as
a plain HTTP POST to HA's webhook trigger (`/api/webhook/<webhook_id>`), on
both edges. Zero broker dependency — this example runs with `mqtt stop` if
you like. The HA side is one automation with a `webhook` trigger and two
`if` branches on `json.state`.

## Wiring (NodeMCU v2)

```
reed switch ──┬────────────────────────── D2 (GPIO4)  to GND + 10k pull-up to 3V3
              │                            (closed = door shut = HIGH)
reed other side ─┘
```

## Setup (device)

```
wifi connect 'SSID' 'pass'
notepad /scripts/ha-webhook.hx   # paste, edit the two post URLs to your HA host + webhook id, save
run /scripts/ha-webhook.hx       # run once: init registers the edge rule and the schedule
```

## Setup (Home Assistant)

1. Developer Tools → YAML → create a `webhook` automation:

```yaml
automation:
  - alias: HarixOS door webhook
    trigger:
      - platform: webhook
        webhook_id: harixos_door
        allowed_methods:
          - POST
    action:
      - service: persistent_notification.create
        data:
          message: "Door is {{ json.state }}"
```

## What the device publishes

| Destination | Content |
|---|---|
| `http://homeassistant.local:8123/api/webhook/harixos_door` | `{"state":"open"}` / `{"state":"closed"}` per edge |
| `<prefix>/gpio/4` | door edges via onchange (if broker is up) |
| `<prefix>/availability` | `online` / `offline` (if broker is up) |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'run /scripts/ha-webhook.hx'   # test fire (door state posts once)
```

## Notes

- The webhook URL has no auth token — that is the point of HA webhooks;
  keep the id unguessable and LAN-only.
- `post` is lenient about `$name`, so a body containing `$schema`-style
  literals survives; here the bodies are static JSON, no vars needed.
- The `onchange` edge rule still publishes to MQTT when the broker is up —
  the webhook is the belt, discovery is the braces.
- `prev` is RAM-only: after a reboot the current door state posts once,
  then the script latches.
