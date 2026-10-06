# Spec: Relay / Latched Switch

**Status:** Draft · **Tier:** 1 · **#2**
**Grounding:** `gpio_api.h`, `onchange.cpp` (issue #1 pattern), `mqtt_service.h`,
`settings.h`, LittleFS usage in `onchange.cpp`

## Objective
Add a relay abstraction — a latched, persisted GPIO output with MQTT reporting
and HA discovery — for the most common ESP8266 HA actuator (lights, pumps,
appliances). Reuse the gpio write path and the issue #1 reporting pattern so the
delta is small.

## Current state
- `GpioAPI::write(pin, level)` drives a digital output.
- `onchange` (issue #1) already shows the target pattern: report a state change
  to `<prefix>/gpio/<pin>` = `on`/`off`, guarded by `isConnected()`, persisted to
  `/onchange.rules` via LittleFS, driven by shell commands, executed through the
  script engine.
- No latched/persisted actuator exists today.

## Scope

**In**
1. A small relay pool: pin, name, last level, persisted to flash.
2. `relay set <name|index> on|off` + `relay list` / `relay status`.
3. Publish `<prefix>/relay/<name>/state` on change; command topic
   `<prefix>/relay/<name>/command` for HA/remote control.
4. HA `switch` discovery per relay.
5. State survives reboot (loaded on boot).

**Out**
- Dimming / PWM relays (out of scope; `gpio` PWM already covers that).
- Feedback sensing from the physical switch (optional follow-up via `onchange`).

## Design

### Core (testable, native build)
Mirror the `onchange_logic` split: put state machine in `relay_logic.cpp/.h`,
keep Arduino/shell glue in `relay.cpp`.

```cpp
// relay_logic.h
struct RelayState { uint8_t pin; bool level; bool dirty; };
// Apply a desired level; returns whether state changed.
bool relaySetLevel(RelayState& r, bool level);
// Compute MQTT state payload ("on"/"off").
void relayStatePayload(bool level, char* out, size_t n);
```

Unit-test `relaySetLevel` (change detection → `dirty`) and payload formatting
natively.

### Arduino layer (`relay.cpp`)
- Pool `kMaxRelays` (e.g. 4). Entry: `pin`, `name[16]`, `level`, `dirty`.
- `initRelay(pin, name)` validates pin availability (reuse `GpioAPI`'s
  `isAvailablePin` logic — add a public wrapper if needed), sets mode OUTPUT,
  writes initial level, persists.
- Persistence: `/relays.json` via LittleFS, same pattern as
  `saveOnchangeRules`/`loadOnchangeRules`.
- `handleSet(name, level)`: apply, publish state on change, mark dirty.
- `handleCommand(topic, payload)`: match `<prefix>/relay/<name>/command`,
  translate `on`/`off`, call `handleSet`. Called from the MQTT inbound path.
- `beginRelay()`: load from flash, restore outputs, register command handler.

### MQTT topics
- State: `<prefix>/relay/<name>/state` (published on change, guarded by
  `isConnected()` — same guard as the issue #1 fix).
- Command: `<prefix>/relay/<name>/command` (accepted, not published).

### Discovery
In `publishHADiscovery()` (shared with spec #1), add per-relay `switch` config:

- Topic: `homeassistant/switch/<prefix>/<name>/config`
- Payload: `{"name":"<name>","state_topic":"<prefix>/relay/<name>/state",
  "command_topic":"<prefix>/relay/<name>/command",
  "payload_on":"on","payload_off":"off","uniq_id":"<hp>_<name>",
  "device":{"ids":"<hp>"}}`

## Integration points
- `gpio_api.h`: `write`, plus a pin-availability check (wrap the private
  `isAvailablePin` or add `bool isUsablePin(uint8_t)`).
- `mqtt_service.h`: `publishRaw`, `isConnected` — reused as-is.
- `onchange.cpp`: copy the LittleFS save/load pattern (do not duplicate the
  logic — reuse a shared helper if one exists, else mirror it).
- `main.cpp`: call `beginRelay()` next to `beginOnchange()`; register
  `handleCommand` with the inbound queue.

## Hardware constraints
- **Flash pins 6–11 unusable** — `initRelay` must reject them (already enforced
  by pin-availability logic).
- Relays are outputs with a cached level → state publish is non-blocking.
- Persistence writes on `set` are low-frequency; acceptable flash wear. Note it.
- Pool of 4 relays is small vs 80 KB RAM.
- A physical pushbutton can toggle a relay via `onchange` (optional follow-up) —
  no new interrupt path required.

## Testing
- `relay_logic` (change detection + payload) → native unit test.
- `handleSet` wiring (publish + dirty + persist) is Arduino-layer; smoke-test via
  the existing native test harness if it can drive the API without full MQTT.

## Risks / known limits
- Topic/name length: names capped to ~16 chars to bound payload/flash.
- Retained discovery lingers in HA until reload (same caveat as spec #1).
- If two relays share a pin, `initRelay` must reject the duplicate.

## Effort
Small. `relay_logic` (~40 lines, testable), `relay.cpp` (~120 lines glue),
discovery loop (~20 lines shared with spec #1), shell commands (~30 lines).
