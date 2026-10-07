# Home Assistant MQTT Integration

## Overview

HarixOS runs an MQTT service (PubSubClient) that integrates with Home
Assistant (HA) through the standard **MQTT Discovery** protocol. The device
publishes telemetry, accepts script-dispatcher commands on
`<prefix>/shell/in`, and registers a retained availability topic so HA knows
when the ESP8266 is reachable.

## Broker Setup

### Mosquitto (Docker)

```bash
docker run -d --name harix-mosq \
  -p 1883:1883 \
  -v /etc/mosquitto/config:/etc/mosquitto/mosquitto.conf \
  eclipse-mosquitto:2
```

Minimal `mosquitto.conf`:

```conf
listener 1883
allow_anonymous true
```

Verify:

```bash
mosquitto_sub -t '#' -v    # see all topics
mosquitto_pub -t test -m hello
```

### Configuration

MQTT settings are `mqtt_*` keys in `/harixos/settings.cfg`. There is **no**
`settings mqtt_*` subcommand — edit the file (e.g. `notepad
/harixos/settings.cfg`) and run `settings reload`.

| Key | Default | Description |
|---|---|---|
| `mqtt_enabled` | `off` | Connect at boot and keep the service running |
| `mqtt_host` | *(empty)* | Broker IP / hostname; empty = refuse to connect |
| `mqtt_port` | `1883` | Broker port |
| `mqtt_user` | *(empty)* | Broker username |
| `mqtt_pass` | *(empty)* | Broker password (shown as `***` in `mqtt status`) |
| `mqtt_prefix` | `harixos/<chipid>` | Topic prefix, resolved at load (e.g. `harixos/a590a8`) |
| `mqtt_interval` | `60` | Telemetry interval in seconds (`0` = off) |
| `mqtt_discover` | `on` | Publish HA discovery messages on first connect |

Example `settings.cfg` block:

```ini
mqtt_enabled=on
mqtt_host=192.168.20.5
mqtt_port=1883
mqtt_prefix=harixos/a590a8
mqtt_interval=30
mqtt_discover=on
```

Then:

```bash
settings reload
mqtt start        # also persists mqtt_enabled=on
mqtt status
```

## Topic Map

```
<prefix>/telemetry            ← device publishes JSON telemetry
<prefix>/shell/in             ← HA or external client sends commands
<prefix>/shell/out            ← device publishes command replies
<prefix>/availability         ← LWT: "online" / "offline" (retained)
<prefix>/mqtt_enabled         ← HA switch state, retained "on"/"off"
<prefix>/gpio/<pin>           ← onchange edge state, retained "on"/"off"
<prefix>/relay/<name>/state   ← relay latched state, retained "on"/"off"
homeassistant/sensor/<hp>/telemetry/config    ← HA sensor discovery (retained)
homeassistant/switch/<hp>/mqtt_enabled/config ← HA switch discovery (retained)
homeassistant/binary_sensor/<hp>/gpio/<pin>/config   ← HA discovery (retained)
homeassistant/switch/<hp>/relay/<name>/config      ← HA discovery (retained)
```

`<prefix>` is `mqtt_prefix` (default `harixos/<chipid>`). In discovery
topics and `uniq_id`/`name` fields, `<hp>` is that prefix with `/` replaced
by `_` — e.g. `harixos_a590a8`.

### Shell Command Protocol

- **Direction:** `<prefix>/shell/in` (subscribe)
- **Reply:** `<prefix>/shell/out` (publish)
- **Format:** line-based, one command per payload
- **Max length:** 79 bytes (≥ 80 → `ERROR: line too long`)
- **Empty payload:** `ERROR: empty line`

Reply rules for each command:

| Case | Reply payload |
|---|---|
| Command printed output | that output, verbatim |
| No output, success | `ok` |
| Error | `ERROR: <message>` |

Large replies are truncated at 4000 bytes with a `…[truncated]` marker and
published in chunks of ≤ 900 bytes.

```bash
# From HA or mosquitto_pub:
mosquitto_pub -t harixos/a590a8/shell/in -m "gpio 2 on"
mosquitto_pub -t harixos/a590a8/shell/in -m "wifi status"
mosquitto_pub -t harixos/a590a8/shell/in -m "heap"
```

`shell/in` runs the **script dispatcher** (the keyword list at the top of
`SCRIPT-REFERENCE.md`), not the interactive shell. In particular:

- Available: `print`, `gpio`, `wifi`, `delay`, `system`, `heap`, `uptime`,
  `chip`, `info`, `adc`, `calc`, filesystem commands, `settings`, `time`,
  `reboot`, `run <path>`, `post`, `mqtt`, `onchange`, `relay`, `servo`,
  `sensor`, `motor`, `set`, `vars`, `help`, `#` comments, `while`/`if` control flow.
- **Not** available: `about`, `pull`, `update`, `serve`, `i2c`, `notepad`,
  `schedule`, `powerprofile`, `cpufreq`, `fs`, `cls`, `reset`,
  `run list|install|uninstall`.
- `gpio` uses the script form (`gpio <pin> on|off|read|toggle|mode|pulse`),
  not the shell form (`gpio write <pin> …`).

Anyone who can publish to `shell/in` controls the device — treat broker
credentials as device ownership and keep the broker on a trusted LAN.

### Shell In — retain flag

**Do NOT set `retain` for `shell/in`.** PubSubClient cannot see the retain
flag on messages it receives, so a retained command sitting in the broker
will be re-delivered every time the device subscribes — causing the same
command to re-execute.

## Home Assistant Discovery

Published once after the first successful connect when `mqtt_discover=on`.

### Telemetry Sensor

HA creates a sensor that shows device heap memory:

```yaml
homeassistant/sensor/harixos_a590a8/telemetry/config:
  payload: >
    {
      "name": "harixos/a590a8 Telemetry",
      "state_topic": "harixos/a590a8/telemetry",
      "value_template": "{{ value_json.heap }}",
      "unit_of_measurement": "bytes",
      "uniq_id": "harixos_a590a8_telemetry",
      "name": "harixos_a590a8_telemetry",
      "dev": { "ids": "harixos_a590a8" }
    }
```

Telemetry payload (published every `mqtt_interval` seconds):

```json
{"heap":47120,"uptime":3600,"rssi":-52,"adc":512}
```

| Field | Description |
|---|---|
| `heap` | Free heap bytes |
| `uptime` | Seconds since boot |
| `rssi` | WiFi signal strength (dBm) |
| `adc` | Analog read of A0 (0–1023) |

Telemetry is skipped (and `telemetry-drop` incremented) when free heap is
below 8 KB, when a blocking script owns the CPU (one tick catches up after
it finishes), or when `mqtt_interval=0`.

### MQTT Enabled Switch

HA creates a switch to toggle the MQTT service. It publishes
`mqtt start` / `mqtt stop` to `shell/in`; both are also accepted as
`mqtt on` / `mqtt off`. The device publishes its retained state to
`<prefix>/mqtt_enabled` on connect and on every start/stop.

```yaml
homeassistant/switch/harixos_a590a8/mqtt_enabled/config:
  payload: >
    {
      "name": "harixos/a590a8 MQTT",
      "state_topic": "harixos/a590a8/mqtt_enabled",
      "value_template": "{{ value }}",
      "command_topic": "harixos/a590a8/shell/in",
      "payload_on": "mqtt on",
      "payload_off": "mqtt off",
       "uniq_id": "harixos_a590a8_mqtt_enabled",
       "name": "harixos_a590a8_mqtt_enabled",
       "dev": { "ids": "harixos_a590a8" }
     }
```

### GPIO Edge Sensors (onchange)

For every registered `onchange` rule, HA creates a `binary_sensor` that
reflects the last debounced edge. State is published to
`<prefix>/gpio/<pin>` (retained `on`/`off`).

```yaml
homeassistant/binary_sensor/harixos_a590a8/gpio/4/config:
  payload: >
    {
      "name": "harixos_a590a8_4",
      "state_topic": "harixos/a590a8/gpio/4",
      "device_class": "motion",
      "uniq_id": "harixos_a590a8_gpio_4",
      "dev": { "ids": "harixos_a590a8" }
    }
```

### Relay Switches

For every registered relay, HA creates a `switch`. The command topic is
`<prefix>/shell/in` with the payload `relay set <name> on` /
`relay set <name> off`; the state topic is `<prefix>/relay/<name>/state`
(retained `on`/`off`).

```yaml
homeassistant/switch/harixos_a590a8/relay/myrelay/config:
  payload: >
    {
      "name": "harixos_a590a8_myrelay",
      "state_topic": "harixos/a590a8/relay/myrelay/state",
      "command_topic": "harixos/a590a8/shell/in",
      "payload_on": "relay set myrelay on",
      "payload_off": "relay set myrelay off",
      "uniq_id": "harixos_a590a8_relay_myrelay",
      "dev": { "ids": "harixos_a590a8" }
    }
```

## Commands via `shell/in`

```bash
# GPIO (script syntax)
gpio 2 on
gpio 2 off
gpio 2 read
gpio 2 mode output
gpio 2 pulse 5

# WiFi
wifi status
wifi scan
wifi connect 'SSID' 'pass'

# System
heap
uptime
chip
info
adc
reboot

# Filesystem
pwd
ls
cat /harixos/settings.cfg

# IoT
mqtt status
mqtt start
mqtt stop
mqtt pub test/hello world
onchange add 4 rising
onchange remove 4
onchange list

# Relay (latched switch)
relay set <name> on
relay set <name> off
relay list

# Servo / sensor / motor
servo attach 4
servo write 4 90
sensor register ultrasonic 4 5 door
sensor publish door
motor init m1
motor forward m1

# HTTP webhook
post https://webhook.example.com/data {"key":"value"} application/json
```

## `post` Command (HTTP Webhook)

Available in the shell, scripts, scheduled tasks, and via `shell/in`.

```bash
post <url> <body> [content-type]
```

| Argument | Required | Default |
|---|---|---|
| `url` | yes | — |
| `body` | yes | — |
| `content-type` | no | `application/json` |

```bash
post https://api.example.com/event {"type":"gpio","pin":2}
post http://localhost:8080/log hello text/plain
```

## `onchange` Rules (GPIO edges)

```bash
onchange add <pin> <rising|falling|both>   # pin: 4 or D2 style
onchange remove <pin>
onchange list
```

| Mode | Trigger |
|---|---|
| `rising` | LOW → HIGH |
| `falling` | HIGH → LOW |
| `both` | Either edge |

Behaviour of the current implementation:

- `onchange add` sets the pin to `INPUT` and registers the rule; only pin
  and edge mode are persisted (the pin mode itself is re-applied at boot).
- Rules are saved to `/onchange.rules` on every add/remove and reloaded at
  boot. Maximum 8 concurrent rules.
- Adding a rule while MQTT is connected re-publishes HA discovery, so a new
  edge pin appears in HA without a reboot.
- Rules are evaluated by polling in the main loop with a debounce state
  machine (not an ISR).
- On a debounced edge the device publishes the new state to
  `<mqttPrefix>/gpio/<pin>` with payload `on` or `off` (only while MQTT is
  connected; the publish is skipped otherwise). `onchange list` shows what
  is armed.

```bash
# Arm, inspect, disarm
onchange add D2 rising
onchange list
onchange remove D2
```

## Relay Commands (latched switches)

```bash
relay add <pin> <name> [on|off]  # register a relay (initial level optional)
relay set <name> on|off          # latch on/off; level holds until changed
relay toggle <name>              # invert the current level
relay status <name>              # show current level
relay list                       # show registered relays (bare `relay` lists)
```

Behaviour of the current implementation:

- Relays are **latched switches**: once set, a relay stays in that state
  across power loss until it is changed again.
- Registration persists to `/relays.conf` (maximum 4 relays).
- On a level change the device publishes the retained state to
  `<prefix>/relay/<name>/state` with payload `on` or `off`. States are also
  re-published on every (re)connect, so HA never shows a stale relay after a
  reboot or a toggle that happened while the broker was unreachable.
- Relay names are 1-15 chars of `[A-Za-z0-9_-]` — they flow verbatim into
  MQTT topics and HA entity ids.
- HA `switch` discovery is published when MQTT connects **and re-published
  whenever a relay is registered while connected** (`relay add` on a running
  device appears in HA without a reboot). Removed relays linger in HA until
  reload — retained discovery is not actively cleaned up.

```bash
# Register, latch on, toggle, then off
relay add D6 garage
relay set garage on
relay toggle garage
relay list
relay set garage off
```

## Sensor Commands (named registry)

```bash
sensor register ultrasonic <trigger> <echo> <name>   # HC-SR04
sensor register dht22 <pin> <name>                   # DHT22
sensor register ds18b20 <pin> [index] <name>         # DS18B20 (daisy-chain index)
sensor register bme280 <name>                        # BME280/BMP280 (I2C; probed 0x76/0x77)
sensor unregister <name>
sensor list                                          # bare `sensor` lists too
sensor read <name>                                   # read now, print all quantities
sensor publish [name]                                # read (all or one) then publish state
```

State topics, per measured quantity, retained:

| Quantity | Topic suffix | Unit | `device_class` |
|---|---|---|---|
| distance | `<prefix>/sensor/<name>/distance` | cm | `distance` |
| temperature | `<prefix>/sensor/<name>/temperature` | °C | `temperature` |
| humidity | `<prefix>/sensor/<name>/humidity` | % | `humidity` |
| pressure | `<prefix>/sensor/<name>/pressure` | hPa | `pressure` |

Per-quantity coverage: ultrasonic → distance; DHT22 → temperature + humidity;
DS18B20 → temperature; BME280 → temperature + humidity + pressure. On a BMP280
(chip id 0x58, temperature + pressure only) the humidity entity is still
registered but stays `unknown` in HA until real BME280 silicon is attached.

Behaviour of the current implementation:

- Sensors are registered by name (1-15 chars `[A-Za-z0-9_-]`), validated via
  `GpioAPI::isAvailablePin` for pinned types, persisted to `/sensors.conf`
  (maximum 8). Malformed lines are skipped with a warning at boot.
- `sensor publish` reads all registered sensors (or one) and publishes the
  cached readings retained; a failed quantity keeps its previous retained
  value. Use it as the cron cadence:
  `schedule add 0 */5 * * * * sensor publish`
- Readings and discovery are re-published on every MQTT (re)connect, so HA
  gets last-good state after a reboot or an outage. DHT22 reads are
  rate-limited to one per 2 s (`sensor read` too soon → `ERROR: read too soon`).
- HA `sensor` discovery (one config per sensor per quantity) is published on
  connect and re-published when a sensor is registered/unregistered while
  connected — same lifecycle as relays/onchange.
- Discovery config example for a DHT22 named `hall` (prefix `harixos/a590a8`):

```json
{"name":"harixos_a590a8_hall_temperature","state_topic":"harixos/a590a8/sensor/hall/temperature","unit_of_measurement":"°C","device_class":"temperature","uniq_id":"harixos_a590a8_sensor_hall_temperature","dev":{"ids":"harixos_a590a8"}}
```

- Legacy `sensor init <t> <e> | ping [t] [e] | read [echo] | list` aliases
  still work but are deprecated (ultrasonic only, pin-addressed).

## Example HA Integration YAML

```yaml
# configuration.yaml — Home Assistant
mqtt:
  sensor:
    - name: "HarixOS Telemetry"
      state_topic: "harixos/a590a8/telemetry"
      value_template: "{{ value_json.heap }}"
      unit_of_measurement: bytes
  switch:
    - name: "HarixOS MQTT"
      state_topic: "harixos/a590a8/mqtt_enabled"
      command_topic: "harixos/a590a8/shell/in"
      payload_on: "mqtt on"
      payload_off: "mqtt off"
      value_template: "{{ value }}"
```

Discovery normally makes manual YAML unnecessary; the block above is for
brokers where `mqtt_discover=off`.

## Behaviour Notes

- During a blocking `.hx` script, MQTT `update()` skips telemetry ticks
  (exactly one fires after script completion).
- `shell/in` lines are queued (capacity 4) and drained through
  `ScriptEngine::executeCommand` with a capture stream, so every command
  gets exactly one published reply. Drops show up as `queue-drop` in
  `mqtt status`.
- Commands received over MQTT work inside `while` loops and scheduled tasks.
- `mqtt pub` targets the topic **verbatim** — no prefix is added.

## Troubleshooting

| Symptom | Check |
|---|---|
| No discovery in HA | Verify `mqtt_discover=on` and that a first connect happened |
| Commands re-execute | Ensure `shell/in` does NOT use retain (PubSubClient limitation) |
| No telemetry | Check `mqtt_interval`, heap ≥ 8 KB, script not blocking |
| Connect failures | Verify broker reachable, heap ≥ 10 KB, `mqtt_host` set |
| Queue drop | `queue-drop` counter in `mqtt status` |
| `mqtt: no host configured` | `mqtt_host` is empty in `/harixos/settings.cfg` |

## Quick Test

```bash
# On the device:
mqtt start
mqtt status

# From another terminal:
mosquitto_sub -t 'harixos/a590a8/shell/out' -v
mosquitto_pub -t 'harixos/a590a8/shell/in' -m "heap"
# → Heap free: 47120 bytes
#   (no output → "ok"; failure → "ERROR: <reason>")

# Check availability + switch state:
mosquitto_sub -t 'harixos/a590a8/availability' -v
mosquitto_sub -t 'harixos/a590a8/mqtt_enabled' -v

# Check HA discovery:
mosquitto_sub -t 'homeassistant/sensor/#' -v
mosquitto_sub -t 'homeassistant/switch/#' -v
mosquitto_sub -t 'homeassistant/binary_sensor/#' -v
```
