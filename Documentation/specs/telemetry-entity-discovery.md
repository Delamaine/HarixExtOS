# Spec: Device-Derived Telemetry + Entity Discovery

**Status:** Draft · **Tier:** 1 · **#1**
**Grounding:** `mqtt_service.cpp:195-374`, `onchange.h`, `servo.h`, `motor.h`, `settings.h:21-22`

## Objective
Extend MQTT telemetry from device-liveness only to the actuator/sensor state the
device actually controls, and publish Home Assistant discovery for the `onchange`
GPIO entities that already publish to `<prefix>/gpio/<pin>`.

## Current state (do not re-implement)
- `buildTelemetryPayload()` (`mqtt_service.cpp:197`) publishes
  `{"heap","uptime","rssi","adc"}` to `<prefix>/telemetry` every `mqttInterval` s.
- `publishHADiscovery()` (`mqtt_service.cpp:213`) publishes one HA `sensor`
  (reads `value_json.heap`) and one HA `switch` (`mqtt_enabled`).
- Telemetry tick in `update()` (`mqtt_service.cpp:353`) is guarded by heap
  (`kMinHeapForTelemetry`), interval, and `s_scriptActive`.
- `onchange` publishes `<prefix>/gpio/<pin>` = `on`/`off` on edges (issue #1 fix)
  but Home Assistant has no discovered entity for those topics.

## Scope

**In**
1. Add cached actuator state to the telemetry payload: servo angle + motor speed.
2. Publish HA `binary_sensor` discovery for every active `onchange` rule pin.
3. Keep all existing liveness fields and guards intact.

**Out**
- Auto-polling the HC-SR04 (blocking read — see Constraints).
- Per-sensor state topics / command topics (belongs to the relay spec).

## Design

### 1. Telemetry payload extension
`buildTelemetryPayload()` already assembles JSON with `String`. Add two fields:

```
"servo":<angle>,"motor":<speed>
```

- `servo` = `ServoAPI::getServoAngle(pin)` for the attached servo, or `-1` if none.
- `motor` = `MotorAPI::getSpeed(index)` for the running motor, or `-1` if none.
- Both accessors return a cached last-value — **non-blocking**, safe to include
  every tick.
- Existing HA `value_template: {{ value_json.heap }}` keeps working; new fields
  are additive and ignored by the current discovery payload.

### 2. Discovery for onchange entities
`publishHADiscovery()` gains a loop over active rules. For each pin:

- Topic: `homeassistant/binary_sensor/<prefix>/<pin>/config`
- Payload: `{"name":"<prefix> <pin>","state_topic":"<prefix>/gpio/<pin>",
  "payload_on":"on","payload_off":"off","uniq_id":"<hp>_<pin>",
  "device":{"ids":"<hp>"}}`
- Iterate via a new accessor so discovery stays decoupled from rule storage:

```cpp
// onchange.h — collect pins of active rules into out (up to maxCount). Returns count.
uint8_t collectActiveRulePins(uint8_t* out, uint8_t maxCount);
```

`publishHADiscovery()` is called once after first connect (guarded by
`discoveryPublished`), so discovery messages are published once and retained.

## Integration points
- `mqtt_service.cpp`: extend `buildTelemetryPayload()`; add the rule loop in
  `publishHADiscovery()`.
- `onchange.h/.cpp`: add `collectActiveRulePins()` (iterate the active rules
  array, copy `pin` where `active`).
- `settings.h`: no new fields — reuses `mqttInterval` (cadence) and
  `mqttDiscover` (toggle). No code change needed.

## Hardware constraints
- `kMaxOnchangeRules = 8` → at most 8 discovery configs. Well within limits.
- Servo/motor cached reads are non-blocking → safe on the telemetry cadence.
- HC-SR04 distance requires a ~50-100 ms blocking trig/echo read → deliberately
  excluded from auto-telemetry; publish on-demand instead.
- Payload stays small; adding 2 small integer fields is negligible vs the
  existing 8 KB heap guard.

## Testing
- `buildTelemetryPayload` lives in `mqtt_service.cpp` (excluded from native
  build). To make the payload testable, extract the field-building into a small
  helper that takes the field values as parameters (e.g. `buildTelemetryJson(heap,
  uptime, rssi, adc, servo, motor)`) — unit-test that natively.
- `collectActiveRulePins` is pure logic over the rules array → unit-test natively
  against a seeded rule set.

## Risks / known limits
- Discovery topics can get long for high pin numbers; still within MQTT limits.
- If a rule is removed, its discovery config lingers in HA until HA reloads
  (retained discovery is a known HA caveat; acceptable for this scope).

## Effort
Small. ~15 lines in `buildTelemetryPayload`, ~20 lines for the discovery loop,
~10 lines for the accessor. One new test file for the two extracted helpers.
