# two-device

Device A publishes a sensor value to MQTT; a tiny broker-side forwarder turns that
value into a command on device B's `shell/in`, which sets B's relay pin. This is how
"device listens to a topic and sets a pin from the received value" is done end to
end: the devices only ever listen on their own `shell/in`.

No `.hx` scripts are needed on either device — the built-in sensor bridge, the
scheduler and `shell/in` commands cover the whole flow. The "script" of this example
is the broker-side forwarder below.

## Wiring (NodeMCU v2)

```
Device A:  3V3 ────[ LDR ]──── A0 ────[ 10k ]──── GND      source value
Device B:  GND ────[ relay module IN ]──────────── D1 (GPIO5)  followed relay
```

## Setup

Device A (source):

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
sensor register ldr A0 level
schedule add 0 * * * * * sensor publish level
```

Device B (follower):

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
relay add 5 follow
schedule add */30 * * * * * mqtt pub harixos/bridge/heartbeat ok
```

Set distinct prefixes so the two devices don't collide: `mqtt settings` / prefix docs.
Replace `harixos` above with B's prefix.

## The forwarder (run next to the broker)

```sh
#!/bin/sh
# turn A's light level into on/off commands for B
mosquitto_sub -t 'harixos/A/sensor/level' | while read v; do
  if [ "$v" -le 150 ]; then c="relay set follow on"; else c="relay set follow off"; fi
  mosquitto_pub -t 'harixos/B/shell/in' -m "$c"
done
```

In Home Assistant, the same logic is an automation: trigger on A's
`sensor/level` state change, action publishes `relay set follow on|off` to B's
`shell/in` topic.

## What the devices publish

| Topic | Content |
|---|---|
| `<prefix>/sensor/level` (A) | light telemetry, every 60 s via schedule |
| `<prefix>/relay/follow/state` (B) | relay state after every change |
| `harixos/bridge/heartbeat` (B) | static `ok` heartbeat, every 30 s via schedule |
| `<prefix>/availability` (both) | `online` / `offline` |

## Monitoring

```sh
mosquitto_sub -t 'harixos/A/sensor/level' -t 'harixos/B/relay/follow/state'
```

## Notes

- Scripts cannot subscribe to arbitrary MQTT topics — inbound is `shell/in` only.
  Cross-device value flow therefore goes through a broker-side automation.
- `mqtt pub` payloads are static text; dynamic values ride the built-in
  sensor/relay/onchange bridges.
- Cron needs a valid system time; it syncs automatically via NTP on Wi-Fi connect.
