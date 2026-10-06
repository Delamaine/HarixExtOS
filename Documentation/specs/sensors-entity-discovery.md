# Spec: Sensor Registry + HA Sensor Entities

**Status:** Draft · **Tier:** 1 · **#4**
**Grounding:** `relay_logic.h`/`relay.cpp` (registry pattern), `sensor.h`/`sensor.cpp`
(legacy HC-SR04), `mqtt_service.cpp` (discovery + reconnect republish),
`onchange.cpp` (persistence pattern), LittleFS usage in `schedule.cfg`

## Objective

Add a named, persisted sensor registry covering DHT22, DS18B20 (daisy-chain),
BME280/BMP280, and the existing HC-SR04 ultrasonic sensor, with MQTT state
topics and Home Assistant `sensor` discovery per measured quantity. Readings
flow via a cron-friendly `sensor publish` and are re-published on every MQTT
reconnect so HA never goes stale.

The registry mirrors the relay pattern exactly (name-validated, persisted,
`invalidateDiscovery()` on register/unregister), so the delta reuses shipped
machinery.

## Current state

- `sensor` command = HC-SR04 ultrasonic only: `sensor init <trigger> <echo>`,
  `sensor ping|read [echo]`, `sensor list`. Pin-addressed, unnamed, **not
  persisted, not published to MQTT** (`SensorAPI::runCommand`).
- `sensor` is already a script keyword (`script_engine.cpp:590`).
- Relay subsystem ships the target pattern: `sensor_logic`-style registry,
  `/relays.conf` persistence, retained MQTT state, per-entity HA discovery,
  reconnect republish + `invalidateDiscovery()`.
- No DHT/1-Wire/BME280 drivers vendored; only `PubSubClient` in `lib_deps`.

## Scope

**In**
1. Named sensor registry, cap 8, types `ultrasonic | dht22 | ds18b20 | bme280`,
   persisted to `/sensors.conf` (bad lines skipped at load).
2. `sensor register <...> / unregister / list / read <name> / publish [name]`.
3. Legacy `sensor init|ping|read|list` kept as working aliases resolving to the
   registry (ultrasonic only), documented as deprecated.
4. MQTT state topics `<prefix>/sensor/<name>/<quantity>` (retained), HA
   `sensor` discovery per quantity, `invalidateDiscovery()` on register/unregister.
5. Reconnect republish of cached readings (same hook as relays).
6. Last-good cache per sensor; failed reads keep the cache and report an error.
7. New native `test_sensor` suite for the registry/discovery logic.

**Out**
- Built-in publish interval setting (cron `sensor publish` covers cadence).
- Analog/"dimming" sensor concepts beyond distance.
- HA device grouping beyond the existing shared "harixos" device.
- Non-blocking/ISR reads: DHT/DS18B20 reads are blocking in the main loop,
  consistent with `sensor ping` today.

## Design

### Core registry — `kernel/iot/sensor_logic.{h,cpp}` (native-testable)

```cpp
enum class SensorType : uint8_t { ultrasonic = 0, dht22, ds18b20, bme280, none = 0xFF };

struct SensorDef {
  char name[16];          // [A-Za-z0-9_-], 1-15 chars (reused relay name rule)
  uint8_t type;           // SensorType value cast
  uint8_t a;              // ultrasonic: trigger | dht22/ds18b20: data pin
  uint8_t b;              // ultrasonic: echo | ds18b20: device index (daisy-chain) | else 0
  float temperature;      // cached last-good, °C (NaN = never read)
  float humidity;         // cached last-good, %
  float pressure;         // cached last-good, hPa
  float distance;         // cached last-good, cm
  bool hasValue(...);     // per-quantity validity
};
```

Functions (mirror `relay_logic`):
- `bool sensorValidName(const char* name)` — same rule as `relayValidName`;
  implement it as a thin call to that function (both live on the native build,
  no duplication).
- `bool sensorParseType(const char* s, uint8_t* out)` — `ultrasonic|dht22|
  ds18b20|bme280`.
- `int sensorFindIndex(const SensorDef* pool, int count, const char* name)` /
  `sensorHasPin(const SensorDef* pool, int count, uint8_t a, uint8_t b, int skip)`.
- `void sensorSetReading(SensorDef& s, int qtyIndex, float value)` and
  `const char* sensorQtyName(int)` (`distance|temperature|humidity|pressure`).
- `const char* sensorUnit(uint8_t type, int qty)` / `device_class` for HA
  (`cm`, `°C`, `%`, `hPa`).
- Persistence: `sensorPersistLine(const SensorDef&, char* out, size_t)` →
  `name|type|a|b`; `sensorParseLine(const char* line, SensorDef* out)` returns
  false on malformed (skip at load).
- HA payload/topic builders: `sensorDiscoveryTopic(const String& prefix,
  const SensorDef&, int qty, String& out)` and `sensorDiscoveryPayload(...)`,
  matching the existing `homeassistant/sensor/<prefix>_<name>_<q>/config`
  style with `uniq_id`/`name` derived from the MQTT prefix (relay/switch
  convention). Device block reuses the shared "harixos" ids.

### Arduino layer — `api/sensor.{h,cpp}`

- Pool `kMaxSensors = 8` of `SensorDef`, `inited[]` flags (relay pattern).
- Backends (device-only, thin):
  - ultrasonic: existing `readDistanceCm()` per def (trigger/echo= a/b).
  - dht22: `DHTesp` on pin `a`; rate-limit reads (reject if <2 s since last);
    temperature+humidity, CRC handled by lib.
  - ds18b20: `OneWire` + `DallasTemperature` on pin `a`, device index `b`
    (bus scan → `getAddress` by index); temperature.
  - bme280: hand-rolled minimal I2C (Wire) register access; try 0x76 then
    0x77; temperature+humidity+pressure. `i2c begin` from the existing command
    sets up Wire; if no bus init, report `ERROR: I2C not initialized`.
- `readSensor(SensorDef&)`: fills quantities, sets cache via `sensorSetReading`,
  returns success flag; failed reads return `ERROR: read <name> failed` and
  leave the cache untouched.
- `runCommand(args, out)` extended:
  - `register <type> <args...> <name>` (registration persists, validates pin via
    `GpioAPI::isAvailablePin` for pinned types, `invalidateDiscovery()`).
  - `unregister <name>` (removes, persists, `invalidateDiscovery()`).
  - `list` (prints each name, type, pins, last-good values), `read <name>`.
  - `publish [name]` (below).
  - Legacy aliases preserved: `init <t> <e>` → register ultrasonic with an
    auto name `hc<echo>`; `ping [t|e pin]` / `read [echo]` / `list` resolve by
    pin against registered ultrasonic sensors, with the legacy bare-form
    defaults. Aliases are documented as deprecated.
- `beginSensor()` (from main.cpp boot): load `/sensors.conf` into the pool,
  validate, skip bad lines with a warning.

### MQTT publish — `kernel/iot/mqtt_service.{h,cpp}`

- `void publishSensorReadings()` (public, like `publishRelayStates`): for each
  registered sensor with a valid cached quantity, publish retained
  `<prefix>/sensor/<name>/<q>` with a decimal float payload; skip unread ones.
- `sensor publish [name]`: read now (all or one), then `publishSensorReadings()`.
  A failed quantity is skipped and its previous retained value stays.
- Reconnect hook: inside the existing `availabilityPending` block, after
  `publishRelayStates()`, call `publishSensorReadings()`.
- Discovery: extend `publishHADiscovery()` to emit one `sensor` config per
  registered sensor per readable quantity (ultrasonic→distance, dht22→
  temperature+humidity, ds18b20→temperature, bme280→temperature+humidity+
  pressure). Uses the existing `discoveryPublished` gate +
  `invalidateDiscovery()` on register/unregister.

### Shell/script surface

- `sensor` stays the command keyword (shell + script + cron). `sensor publish`
  is the cron-friendly form:
  `schedule add 0 */5 * * * * sensor publish`
- `main.cpp` help topic `sensor` and the `help` index line updated; `script_engine`
  help echo line updated.

## Integration points

- `api/gpio_api.h` (`isAvailablePin`, already public) for pin-validating dht22/
  ds18b20/ultrasonic registers.
- `kernel/iot/relay_logic.h` — reuse `relayValidName` (or promote to a shared
  name helper if it reads better; no new module just for a 6-line rule).
- `kernel/iot/mqtt_service.h/.cpp` — add `publishSensorReadings()`; extend
  `publishHADiscovery()`; reconnect republish.
- `main.cpp` — `beginSensor()` at boot, `sensor` help topic, dispatch unchanged.
- `platformio.ini` — `env:lib_deps += DHTesp@^1.6, PaulStoffregen/OneWire@^2.3,
  milesburton/DallasTemperature@^3.11` (device envs; native env does not compile
  backends); native `build_src_filter += +<kernel/iot/sensor_logic.cpp>`.
- Docs: `Commands.md`, `ha-mqtt.md`, `SCRIPT-REFERENCE.md`.

## Failure semantics

- Registration rejects duplicate name, duplicate pin, unknown type, invalid
  name; save failure after register/unregister surfaces as `API_ERROR`.
- DHT read <2 s after last → `ERROR: read too soon` (rate limit honored).
- No device on 1-Wire bus at configured index → read failed, cache kept.
- I2C not initialized for bme280 → `ERROR: I2C not initialized`.
- Persistent file load: malformed lines warned and skipped; file absent = empty
  registry.

## Tests — `test/test_sensor/test_sensor.cpp` (native, unity)

1. `sensorValidName` accept/reject.
2. `sensorParseType` for all four + garbage.
3. Register duplicate name / duplicate pin / pool-full rejection.
4. `sensorPersistLine` round-trip + `sensorParseLine` on bad line → false.
5. `sensorSetReading`/`sensorQtyName` ordering stable.
6. Discovery topic/payload fields per type+quantity (state_topic, unit,
   device_class, uniq_id) — characterization of the real builder.
7. State value formatting (decimal float, retained payload text).
8. `sensorFindIndex`/`sensorHasPin` semantics.

## Docs

- `Commands.md`: `sensor register` syntax per type, `unregister/list/read/
  publish`, legacy-alias note, `/sensors.conf`.
- `ha-mqtt.md`: new "Sensors" section — topic map, discovery example YAML,
  cron `sensor publish` example, reconnect republish note.
- `SCRIPT-REFERENCE.md`: extend the `sensor` keyword block, IoT keyword line.