# Sensor Registry + HA Sensor Entities Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the single-purpose ultrasonic `sensor` command into a named, persisted sensor registry (ultrasonic, DHT22, DS18B20 daisy-chain, BME280) that publishes retained MQTT readings and Home Assistant `sensor` entities, readable from shell, scripts, and cron.

**Architecture:** Mirror the shipped relay subsystem. `sensor_logic.{h,cpp}` is pure, host-testable logic (registry helpers, name/type parsing, unit/mapping, persistence lines, and a discovery JSON builder). `sensor.cpp` owns the pool, hardware backends, `runCommand`, legacy aliases, and bootload from LittleFS. `mqtt_service` extends the existing `publishHADiscovery()` loop and reconnect republish, both already gated by the `discoveryPublished`/`invalidateDiscovery()` flags from the relay work.

**Tech Stack:** ESP8266 Arduino core (PIO `nodemcuv2`), `beegee-tokyo/DHTesp`, `PaulStoffregen/OneWire`, `milesburton/DallasTemperature` (device envs only; native ignores them), Unity tests on `python -m platformio test -e native`, LittleFS persistence, manual JSON concatenation (no JSON lib).

**Spec:** `Documentation/specs/sensors-entity-discovery.md` — the plan argues from the spec; read both.

## Global Constraints

- Registry: `kMaxSensors = 8`, name 1-15 chars `[A-Za-z0-9_-]` — reuse `relayValidName` (mentally in `src/kernel/iot/relay_logic.h`), never duplicate it.
- Types exactly: `SensorType` enum `ultrasonic=0, dht22, ds18b20, bme280`. Quantities `SensorQty`: `distance=0, temperature=1, humidity=2, pressure=3`, `kSensorQtyCount=4`, order fixed.
- `sensorHasQuantity`: ultrasonic→distance; dht22→temperature,humidity; ds18b20→temperature; bme280→temperature,humidity,pressure.
- Units/`device_class` exactly: distance→`"cm"`/`"distance"`; temperature→`"°C"`/`"temperature"`; humidity→`"%"`/`"humidity"`; pressure→`"hPa"`/`"pressure"`.
- Persist `/sensors.conf`: one line per sensor `name|typeName|a|b` (e.g. `garage|dht22|4|0`, `probe1|ds18b20|5|2`, `lab|bme280|0|0`, `door|ultrasonic|14|12`). Load skips malformed lines with a `Serial` warning; missing file = empty registry.
- State topics `<prefix>/sensor/<name>/<q>` retained, payload `%.1f` (e.g. `21.4`).
- Discovery: topic `homeassistant/sensor/<hp>/<name>_<q>/config`; payload built by the pure builder — `name` field `<hp>_<name>_<q>`, `uniq_id` `<hp>_sensor_<name>_<q>`, device block `"dev":{"ids":"<hp>"}`. `<hp>` = `haPrefix()` (sanitized prefix, as in the relay/switch discovery), `<prefix>` = raw `mqttPrefix` for `state_topic`.
- DHT22 rate-limit ≥2 s between reads (`ERROR: read too soon`); DS18B20 read by device index `b` after `requestTemperatures()`; BME280 tries address 0x76 then 0x77, compensation math per BME280 datasheet §9.1.3, read failure reports `ERROR: no BME280 on I2C`.
- Pinned pins (ultrasonic/dht22/ds18b20) register only via `GpioAPI::isAvailablePin` (already public); BME280 takes no pin.
- Failed reads leave the last-good cache and the previously retained MQTT value untouched.
- `sensor register`/`sensor unregister` call `invalidateDiscovery()`; `mqtt_service` republishes cached sensor readings on every reconnect (inside the `availabilityPending` block).
- Legacy aliases stay working and are deprecated: `init <t> <e>` → register ultrasonic named `hc<echo>`; `ping`/`read [echo]`/`list` resolve by pin against registered sensors (bare defaults trigger=4, echo=5).
- `lib_deps`: add `beegee-tokyo/DHTesp@^1.6`, `PaulStoffregen/OneWire@^2.3`, `milesburton/DallasTemperature@^3.11` to `[env]`; add `lib_ignore = DHTesp, OneWire, DallasTemperature` to `[env:native]` so the pure test build stays Arduino-free.
- Build/flash: `python -m platformio run -e nodemcuv2`. Host tests: `python -m platformio test -e native`.

## Review Focus

Inputs/conditions the spec implies that no single happy-path test covers; each line names the test that pins it, in the task owning the code.

1. **Registration garbage** (unknown type, invalid name, duplicate name, duplicate pin) — expected: rejected with a usage message, registry unchanged, no crash → Task 1 unit tests (`sensorParseType`, `sensorValidName`, `sensorFindIndex`, `sensorHasPin`) + Task 3 device step `sensor register nonsense D5 x`.
2. **Corrupt `/sensors.conf` line** — expected: that line skipped with a warning, valid lines still load → Task 1 `test_parse_line_rejects_malformed`.
3. **DHT22 re-read faster than the rate limit** — expected: `ERROR: read too soon`, cache and broker value unchanged → Task 3 hardware step: two back-to-back `sensor read`.
4. **`sensor publish <unknown-name>` / `sensor read <unknown-name>`** — expected: `ERROR: sensor not found`, no crash → Task 3 device step.
5. **Discovery JSON per quantity** must match the HA contract exactly (topic, `uniq_id`, `unit_of_measurement`, `device_class`, `dev.ids`) — expected: byte-identical to the pinned builder output → Task 2 native test `test_discovery_payload_field_exact`.

---

### Task 1: `sensor_logic` core (pure, host-tested)

**Files:**
- Create: `src/kernel/iot/sensor_logic.h`, `src/kernel/iot/sensor_logic.cpp`
- Test: `test/test_sensor/test_sensor.cpp`
- Modify: `platformio.ini:63` (`[env:native] build_src_filter` — add `+<kernel/iot/sensor_logic.cpp>`)

**Interfaces:**
- Consumes: `relayValidName` from `src/kernel/iot/relay_logic.h` (already on the native build).
- Produces (Task 2 extends with the discovery builder; Task 3 uses all of these):
```cpp
namespace harixos { namespace iot {
enum SensorType : uint8_t { kSensorUltrasonic = 0, kSensorDht22, kSensorDs18b20, kSensorBme280, kSensorTypeCount };
enum SensorQty : uint8_t { kQtyDistance = 0, kQtyTemperature, kQtyHumidity, kQtyPressure, kSensorQtyCount };

struct SensorDef {
  char name[16];          // NUL-terminated, 1-15 chars
  uint8_t type;           // SensorType value
  uint8_t a;              // ultrasonic: trigger | dht22/ds18b20: data pin
  uint8_t b;              // ultrasonic: echo | ds18b20: device index | else 0
  float temperature, humidity, pressure, distance;  // last-good cache; NaN = never read
};

bool    sensorValidName(const char* s);                 // delegates to relayValidName
bool    sensorParseType(const char* s, uint8_t* out);   // "ultrasonic"|"dht22"|"ds18b20"|"bme280"
const char* sensorTypeName(uint8_t type);
int     sensorFindIndex(const SensorDef* pool, int count, const char* name);  // -1 if absent
bool    sensorHasPin(const SensorDef* pool, int count, uint8_t a, uint8_t b, int skip);
void    sensorSetReading(SensorDef& d, uint8_t qty, float v);   // NaN clears
bool    sensorHasQuantity(uint8_t type, uint8_t qty);
const char* sensorQtyName(uint8_t qty);
const char* sensorUnit(uint8_t type, uint8_t qty);
const char* sensorDeviceClass(uint8_t type, uint8_t qty);
void    sensorFormatValue(float v, char* out, size_t n);        // "%.1f"
void    sensorPersistLine(const SensorDef& d, char* out, size_t n); // "name|typeName|a|b"
bool    sensorParseLine(const char* line, SensorDef* out);      // false on any malformed field
}}
```
`sensor_logic` has no Arduino includes and uses only `<cstring>`/`<cstdio>`/`<stdint.h>`.

- [ ] **Step 1: Write the failing tests** (`test/test_sensor/test_sensor.cpp`)

```cpp
#include <unity.h>
#include <cstring>
#include "kernel/iot/sensor_logic.h"
using namespace harixos::iot;

static void test_valid_name(void) {
  TEST_ASSERT_TRUE(sensorValidName("garage"));
  TEST_ASSERT_FALSE(sensorValidName(""));
  TEST_ASSERT_FALSE(sensorValidName("has space"));
  TEST_ASSERT_FALSE(sensorValidName("bad.name!"));
  char longName[17]; memset(longName, 'a', 16); longName[16] = '\0';
  TEST_ASSERT_FALSE(sensorValidName(longName));
}

static void test_parse_type(void) {
  uint8_t t = 0xFF;
  TEST_ASSERT_TRUE(sensorParseType("ultrasonic", &t)); TEST_ASSERT_EQUAL_UINT(kSensorUltrasonic, t);
  TEST_ASSERT_TRUE(sensorParseType("dht22", &t));     TEST_ASSERT_EQUAL_UINT(kSensorDht22, t);
  TEST_ASSERT_TRUE(sensorParseType("ds18b20", &t));   TEST_ASSERT_EQUAL_UINT(kSensorDs18b20, t);
  TEST_ASSERT_TRUE(sensorParseType("bme280", &t));    TEST_ASSERT_EQUAL_UINT(kSensorBme280, t);
  TEST_ASSERT_FALSE(sensorParseType("temprature", &t));
  TEST_ASSERT_FALSE(sensorParseType("", &t));
  TEST_ASSERT_EQUAL_STRING("dht22", sensorTypeName(kSensorDht22));
}

static void test_find_and_pin_collision(void) {
  SensorDef pool[2] = {};
  pool[0].type = kSensorUltrasonic; pool[0].a = 14; pool[0].b = 12; strcpy(pool[0].name, "door");
  pool[1].type = kSensorDht22;      pool[1].a = 4;  pool[1].b = 0;  strcpy(pool[1].name, "hall");
  TEST_ASSERT_EQUAL_INT(0, sensorFindIndex(pool, 2, "door"));
  TEST_ASSERT_EQUAL_INT(1, sensorFindIndex(pool, 2, "hall"));
  TEST_ASSERT_EQUAL_INT(-1, sensorFindIndex(pool, 2, "nope"));
  TEST_ASSERT_TRUE(sensorHasPin(pool, 2, 14, 12, -1));
  TEST_ASSERT_TRUE(sensorHasPin(pool, 2, 4, 0, -1));
  TEST_ASSERT_FALSE(sensorHasPin(pool, 2, 4, 0, 1));   // skip self
  TEST_ASSERT_FALSE(sensorHasPin(pool, 2, 9, 9, -1));
}

static void test_quantities_units_classes(void) {
  uint8_t t;
  sensorParseType("dht22", &t);
  TEST_ASSERT_TRUE(sensorHasQuantity(t, kQtyTemperature));
  TEST_ASSERT_TRUE(sensorHasQuantity(t, kQtyHumidity));
  TEST_ASSERT_FALSE(sensorHasQuantity(t, kQtyPressure));
  sensorParseType("ultrasonic", &t);
  TEST_ASSERT_TRUE(sensorHasQuantity(t, kQtyDistance));
  TEST_ASSERT_FALSE(sensorHasQuantity(t, kQtyTemperature));
  TEST_ASSERT_EQUAL_STRING("temperature", sensorQtyName(kQtyTemperature));
  TEST_ASSERT_EQUAL_STRING("°C", sensorUnit(t, kQtyTemperature));
  TEST_ASSERT_EQUAL_STRING("temperature", sensorDeviceClass(t, kQtyTemperature));
  TEST_ASSERT_EQUAL_STRING("cm", sensorUnit(t, kQtyDistance));
  TEST_ASSERT_EQUAL_STRING("hPa", sensorUnit(t, kQtyPressure));
}

static void test_set_and_format_reading(void) {
  SensorDef d = {};
  sensorSetReading(d, kQtyTemperature, 21.25);
  TEST_ASSERT_EQUAL_FLOAT(21.25, d.temperature);
  sensorSetReading(d, kQtyTemperature, NAN);
  TEST_ASSERT_TRUE(isnan(d.temperature));
  char buf[16];
  sensorFormatValue(21.26, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("21.3", buf);
  sensorFormatValue(21.24, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("21.2", buf);
}

static void test_persist_line_roundtrip(void) {
  SensorDef d = {};
  d.type = kSensorDs18b20; d.a = 5; d.b = 2; strcpy(d.name, "probe1");
  char line[64]; sensorPersistLine(d, line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING("probe1|ds18b20|5|2", line);
  SensorDef out = {};
  TEST_ASSERT_TRUE(sensorParseLine(line, &out));
  TEST_ASSERT_EQUAL_STRING("probe1", out.name);
  TEST_ASSERT_EQUAL_UINT(kSensorDs18b20, out.type);
  TEST_ASSERT_EQUAL_UINT(5, out.a);
  TEST_ASSERT_EQUAL_UINT(2, out.b);
}

static void test_parse_line_rejects_malformed(void) {
  SensorDef out = {};
  TEST_ASSERT_FALSE(sensorParseLine("", &out));
  TEST_ASSERT_FALSE(sensorParseLine("garage|dht22|4", &out));        // too few fields
  TEST_ASSERT_FALSE(sensorParseLine("|dht22|4|0", &out));            // empty name
  TEST_ASSERT_FALSE(sensorParseLine("bad name|dht22|4|0", &out));    // invalid name
  TEST_ASSERT_FALSE(sensorParseLine("garage|nonsense|4|0", &out));   // unknown type
  TEST_ASSERT_FALSE(sensorParseLine("garage|dht22|0|0", &out));      // pin 0 = unset
  TEST_ASSERT_FALSE(sensorParseLine("garage|bme280|4|0", &out));     // bme280 takes no pin
}
```
`#include <cmath>` for `isnan`. Put `sensorParseLine` malformed checks where the type requires a pin: `a == 0` invalid only for ultrasonic/dht22/ds18b20, and `a != 0` invalid for `bme280`.

- [ ] **Step 2: Wire the native build and run, expect failure**

Edit `platformio.ini` `[env:native]` `build_src_filter`: append `+<kernel/iot/sensor_logic.cpp>`.

Run: `python -m platformio test -e native`
Expected: `test_sensor [FAILED]` — unresolved `sensorValidName` etc. (header not built yet).

- [ ] **Step 3: Implement `src/kernel/iot/sensor_logic.h` and `sensor_logic.cpp`**

Signature block above, exactly. `sensorValidName` = `relayValidName(s)`. Quantities matrix per `sensorHasQuantity`. `sensorParseLine`: split on `|`, require exactly 4 fields, then `sensorValidName`/`sensorParseType`/pin rules. `sensorParseType`/`sensorTypeName` map only the four names above. Persist writes `d.name | sensorTypeName(d.type,..) | d.a | d.b`.

- [ ] **Step 4: Run the native suite, expect it passes**

Run: `python -m platformio test -e native`
Expected: `test_sensor [PASSED]`, all six tests green, existing suites unaffected.

- [ ] **Step 5: Commit**

```bash
git add src/kernel/iot/sensor_logic.h src/kernel/iot/sensor_logic.cpp test/test_sensor/test_sensor.cpp platformio.ini
git commit -m "feat(sensor): pure sensor_logic registry core with native tests"
```

### Task 2: HA discovery JSON builder (pure, host-tested)

**Files:**
- Modify: `src/kernel/iot/sensor_logic.h`, `src/kernel/iot/sensor_logic.cpp`
- Test: `test/test_sensor/test_sensor.cpp`

**Interfaces:**
- Consumes: Task 1 types/helpers.
- Produces (Task 4 calls it from `mqtt_service.cpp`; Task 3 does not need it):
```cpp
// Build the HA sensor config payload (no JSON lib, String-free).
// hp  = sanitized prefix (haPrefix()); prefix = raw mqttPrefix
// (used for state_topic only). Returns false if it doesn't fit in out.
bool sensorDiscoveryPayload(const char* hp, const char* prefix,
                            const char* name, uint8_t type, uint8_t qty,
                            char* out, size_t max);
```
Exact output (order fixed):
`{"name":"<hp>_<name>_<q>","state_topic":"<prefix>/sensor/<name>/<q>","unit_of_measurement":"<unit>","device_class":"<class>","uniq_id":"<hp>_sensor_<name>_<q>","dev":{"ids":"<hp>"}}`
Buffer size: 256 is always enough for the validated names.

- [ ] **Step 1: Write the failing test**

```cpp
static void test_discovery_payload_field_exact(void) {
  char out[256];
  TEST_ASSERT_TRUE(sensorDiscoveryPayload("haps", "harixos/x", "garage", kSensorDht22, kQtyTemperature, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING(
    "{\"name\":\"haps_garage_temperature\",\"state_topic\":\"harixos/x/sensor/garage/temperature\","
    "\"unit_of_measurement\":\"°C\",\"device_class\":\"temperature\","
    "\"uniq_id\":\"haps_sensor_garage_temperature\",\"dev\":{\"ids\":\"haps\"}}", out);
  char small[8];
  TEST_ASSERT_FALSE(sensorDiscoveryPayload("haps", "harixos/x", "garage", kSensorDht22, kQtyTemperature, small, sizeof(small)));
}
```
Add `test_discovery_payload_bme_pressure` asserting unit `hPa` and q name `pressure`, and `test_discovery_payload_ultrasonic_distance` asserting `device_class` `distance` and unit `cm`.

- [ ] **Step 2: Run test, expect failure**

Run: `python -m platformio test -e native`
Expected: `test_sensor [FAILED]` — `sensorDiscoveryPayload` undefined.

- [ ] **Step 3: Implement `sensorDiscoveryPayload` in `sensor_logic.cpp`**

`snprintf` only (no Arduino, no `String`). Return `false` when `snprintf` returns a value `>= max` or negative. Quantity/unit/class via `sensorQtyName`/`sensorUnit`/`sensorDeviceClass`.

- [ ] **Step 4: Run test, expect pass**

Run: `python -m platformio test -e native`
Expected: `test_sensor [PASSED]`.

- [ ] **Step 5: Commit**

```bash
git add src/kernel/iot/sensor_logic.h src/kernel/iot/sensor_logic.cpp test/test_sensor/test_sensor.cpp
git commit -m "feat(sensor): pure HA discovery JSON builder with exact-field tests"
```

### Task 3: `SensorAPI` Arduino layer — pool, backends, commands, legacy aliases

**Files:**
- Rewrite: `src/api/sensor.h`, `src/api/sensor.cpp`
- Modify: `platformio.ini:15` (`[env] lib_deps`), `platformio.ini:62` (`[env:native]` add `lib_ignore`)

**Interfaces:**
- Consumes: Task 1 `sensor_logic`; `GpioAPI::isAvailablePin` (`src/api/gpio_api.h`, already public); `harixos::iot::invalidateDiscovery()` (`mqtt_service.h`); `harixos::iot::isConnected()` and `harixos::iot::publishRaw(topic, payload, retain)` (`mqtt_service.h`, as `relay.cpp` uses them).
- Produces (Task 4 consumes these from `mqtt_service.cpp`):
```cpp
namespace harixos { namespace api {
// existing class SensorAPI grows statics; keep kMaxSensors = 8.
extern const char* kSensorsPath;              // "/sensors.conf"
void beginSensor();                           // load /sensors.conf at boot
void publishSensorStates();                   // publish cached readings (reconnect republish)
String collectSensorNames();                  // "a,b,..." comma-joined, mirrors collectRelayNames
bool  sensorTypeOf(const char* name, uint8_t* type);  // false if not registered
static ApiResult runCommand(const String& args, Stream& out);
}}
```
`runCommand` subcommands:
```
sensor register <ultrasonic <t> <e> | dht22 <pin> | ds18b20 <pin> [index] | bme280> <name>
sensor unregister <name>
sensor list                      (bare `sensor` also lists)
sensor read <name>
sensor publish [name]
```
Legacy aliases unchanged in shape: `init <t> <e>`, `ping [trigger] [echo]`, `read [echo]`, `list` — resolve by pin against registered ultrasonic sensors.

- [ ] **Step 1: Add the three libs + native lib_ignore, build**

Edit `platformio.ini`:
```ini
; [env] section
lib_deps =
  knolleary/PubSubClient@^2.8
  beegee-tokyo/DHTesp@^1.6
  PaulStoffregen/OneWire@^2.3
  milesburton/DallasTemperature@^3.11
; [env:native] section — add:
lib_ignore = DHTesp, OneWire, DallasTemperature
```
(Adjust the exact registry versions to whatever `python -m platformio pkg install` resolves if one of these ids no longer registers; the three libs themselves are pinned.)

Run: `python -m platformio run -e nodemcuv2`
Expected: SUCCESS — proves the libs resolve and compile on-device. Then run `python -m platformio test -e native` → still green (libs ignored there).

- [ ] **Step 2: Rewrite `src/api/sensor.h`**

Declaration block above. Keep `kMaxSensors = 8` and the legacy `SensorConfig`/finder signatures that legacy `ping`/`read` need (`findByEchoPin`, `findByTriggerPin`, `init`, `listAll`) as thin compat methods over the registry — or remove them if `runCommand` no longer calls them, and keep the header lean. Decide in the rewrite: the legacy aliases must stay functional, the bodies may change.

- [ ] **Step 3: Rewrite `src/api/sensor.cpp` — registry + backends**

- Pool: `static SensorDef pool_[kMaxSensors]`; `static bool inited_[kMaxSensors]`; `static uint32_t lastDhtReadMs_[kMaxSensors]` (DHT rate-limit, in ms).
- `beginSensor()`: open `/sensors.conf`, read line by line, `sensorParseLine`; valid → fill pool; invalid → `Serial.printf("sensor: skipping bad line: %s\r\n", line)`.
- `readUltrasonic(SensorDef&)`: existing HC-SR04 pulse-loop logic; sets `distance`.
- `readDht(SensorDef&, int idx)`: `DHTesp` instance; `if (lastDhtReadMs_[idx] && millis() - lastDhtReadMs_[idx] < 2000UL) return false` with caller printing `ERROR: read too soon`; else `dht.setup(a, DHTesp::DHT22)`, `getTemperature()`, `getHumidity()`; only commit readings when `dht.getStatus() == DHTesp::DHTesp_OK`; update `lastDhtReadMs_[idx]`.
- `readDs18b20(SensorDef&)`: `OneWire` on `a`, `DallasTemperature`; `if (deviceCount(b) <= b)` fail; `requestTemperatures()`; `float t = getTempCByIndex(b); if (t <= -126.0f) fail;` commit `temperature`.
- `readBme280(SensorDef&)`: Wire register reads; probe chip-id (`0xD0`) at 0x76 then 0x77 storing the working address and calibration in a static probe result; read 0x88/0xE1 calibration + 0xF7..0xFE data; compensation per datasheet §9.1.3; commit temperature+humidity+pressure. No chip at either address → fail with `ERROR: no BME280 on I2C`.
- `runCommand` parser: strip the first token as action; `register` has the per-type token layout from the spec (pin-rich types validate `GpioAPI::isAvailablePin`, `sensorValidName`, `sensorFindIndex == -1`, `sensorHasPin == false`, pool not full) then `beginSensor`-style save to `/sensors.conf` and `invalidateDiscovery()`; save failure → `ApiResult(API_ERROR, "sensor registered but save failed")`. `unregister <name>` removes, reindexes nothing (leave hole), saves, `invalidateDiscovery()`.
- Legacy `init` maps to a synthetic name `hc<echo>` (printf into the 16-byte name), `ping`/`read` find by trigger/echo pin.
- `sensor read <name>` prints each quantity with its unit from `sensorQtyName`/`sensorUnit`; unknown name → `ApiResult(API_ERROR, "sensor not found")`. Bare `sensor`/`sensor list` prints name/type/pins/cached values.
- `publishSensorStates()`: for each inited sensor with a non-NaN quantity, `publishRaw(prefix + "/sensor/" + name + "/" + qName, formattedValue, true)` guarded by `isConnected()`. `sensor publish [name]` reads (all or one), then calls `publishSensorStates()`; per-quantity failures are skipped (previous retained value stays).

- [ ] **Step 4: Build, expect success**

Run: `python -m platformio run -e nodemcuv2`
Expected: SUCCESS — no Arduino/compile errors, libs resolve.

- [ ] **Step 5: Device-only verification (hardware present)**

Register each type and exercise commands over COM3 serial:
```
sensor register dht22 4 hall         → registered, listed after power-cycling
sensor register dht22 4 hall         → ERROR: duplicate name/pin
sensor register nonsense D5 x        → usage error (Review Focus 1)
sensor read hall                      → temperature/humidity
sensor read hall (again immediately)  → ERROR: read too soon (Review Focus 3)
sensor publish                        → publishes both quantities
sensor publish nowhere                → ERROR: sensor not found (Review Focus 4)
sensor unregister hall
```
If no DHT22/DS18B20/BME280 is available, mark `readDht`/`readDs18b20`/`readBme280` as build-verified only and leave the serial steps TODO on hardware.

- [ ] **Step 6: Run native suite (regression), expect pass**

Run: `python -m platformio test -e native`
Expected: all suites green (`sensor_logic` unchanged).

- [ ] **Step 7: Commit**

```bash
git add src/api/sensor.h src/api/sensor.cpp platformio.ini
git commit -m "feat(sensor): named sensor pool with DHT22/DS18B20/BME280/ultrasonic backends"
```

### Task 4: MQTT discovery + reconnect republish

**Files:**
- Modify: `src/kernel/iot/mqtt_service.cpp`
- Test: none (device path; discovery payload itself is Task 2's native test)

**Interfaces:**
- Consumes: Task 2 `sensorDiscoveryPayload`, Task 3 `collectSensorNames`/`sensorTypeOf`/`publishSensorStates`.
- Produces: nothing new (extends `publishHADiscovery()`, reconnect block).

- [ ] **Step 1: Extend `publishHADiscovery()`**

After the relay switch block (ends near `mqtt_service.cpp:380`), add the sensor loop:
```cpp
// Sensor configs: one per registered sensor per readable quantity.
String snames = harixos::api::collectSensorNames();
size_t start = 0;
while (start < snames.length()) {
  size_t comma = snames.indexOf(',', start);
  String name = (comma == (size_t)-1) ? snames.substring(start) : snames.substring(start, comma);
  name.trim();
  if (name.length() > 0) {
    uint8_t stype;
    if (harixos::api::sensorTypeOf(name.c_str(), &stype)) {
      for (uint8_t q = kQtyDistance; q < kSensorQtyCount; ++q) {
        if (!sensorHasQuantity(stype, q)) continue;
        String cfgTopic = "homeassistant/sensor/";
        cfgTopic += hp; cfgTopic += "/"; cfgTopic += name; cfgTopic += "_";
        cfgTopic += sensorQtyName(q); cfgTopic += "/config";
        char pl[256];
        if (sensorDiscoveryPayload(hp.c_str(), prefix.c_str(), name.c_str(), stype, q, pl, sizeof(pl))) {
          publishRaw(cfgTopic, pl, true);
        }
      }
    }
  }
  if (comma == (size_t)-1) break;
  start = comma + 1;
}
```
Keep the `discoveryPublished = true;` gate as-is (sensor loop runs before it inside the same call).

- [ ] **Step 2: Reconnect republish — cached readings**

In the `if (availabilityPending) {...}` block (already calls `publishRelayStates()` — `mqtt_service.cpp` ~line 421), add right after it:
```cpp
harixos::api::publishSensorStates();
```

- [ ] **Step 3: Build, expect success**

Run: `python -m platformio run -e nodemcuv2`
Expected: SUCCESS — types and functions resolve (both files include the right headers: `mqtt_service.cpp` needs `#include "api/sensor.h"` and `#include "kernel/iot/sensor_logic.h"`).

- [ ] **Step 4: Serial verification (hardware present)**

With MQTT connected to a broker, run `sensor register dht22 4 hall`, `sensor read hall`, then subscribe:
`mosquitto_sub -t 'homeassistant/sensor/+/hall_temperature/config' -v` → the config JSON appears (matches the Task 2 pinned payload), and `harixos/.../sensor/hall/temperature` gets the retained reading. Restart the device → retained reading re-published after connect.

- [ ] **Step 5: Commit**

```bash
git add src/kernel/iot/mqtt_service.cpp
git commit -m "feat(sensor): HA discovery per quantity + reconnect republish of sensor states"
```

### Task 5: Shell/script help + documentation

**Files:**
- Modify: `src/main.cpp` (help index `sensor ...` line ~1891; `help sensor` topic block ~1988-1993), `src/api/script_engine.cpp` (`sensor ...` help echo line ~800), `Documentation/Commands.md`, `Documentation/ha-mqtt.md`, `SCRIPT-REFERENCE.md`

**Interfaces:**
- Consumes: Task 3 command surface.

- [ ] **Step 1: Update `main.cpp` help**

`help` index line:
`  sensor ...            Sensors: register/list/read/publish (HC-SR04, DHT22, DS18B20, BME280); legacy init/ping/read/list`
`help sensor` topic lists every subcommand of `register` per type (ultrasonic `<t> <e>`, dht22 `<pin>`, ds18b20 `<pin> [index]`, bme280) plus `unregister <name>`, `list`, `read <name>`, `publish [name]`, and one line: `Legacy init/ping/read/list remain as deprecated aliases (ultrasonic only).`

- [ ] **Step 2: Update `script_engine.cpp` help echo line** (`~line 800`) to the same one-line summary as the `main.cpp` index.

- [ ] **Step 3: Update `Documentation/Commands.md`**

Replace the existing `Hardware drivers` → Ultrasonic sensor block with the full `sensor` command section: all `register` forms, `unregister/list/read/publish`, `sensor publish` cron example (`schedule add 0 */5 * * * * sensor publish`), the legacy-alias/deprecated note, and `/sensors.conf` persistence.

- [ ] **Step 4: Update `Documentation/ha-mqtt.md`**

Add a "Sensors" section next to Relay/onchange: topic map (`<prefix>/sensor/<name>/<q>`), discovery config example YAML (a copy of the Task 2 pinned JSON), the cron `sensor publish` pattern, reconnect republish note, and the failure/skip semantics (failed read keeps the last retained value).

- [ ] **Step 5: Update `SCRIPT-REFERENCE.md`**

Extend the `sensor` script keyword block with `register` syntax + `publish`, and add `sensor` detail to the IoT keyword list line if it lists them.

- [ ] **Step 6: Full green gates**

Run: `python -m platformio run -e nodemcuv2` → SUCCESS, and `python -m platformio test -e native` → all suites PASSED (incl. `test_sensor`).

- [ ] **Step 7: Commit**

```bash
git add src/main.cpp src/api/script_engine.cpp Documentation/Commands.md Documentation/ha-mqtt.md SCRIPT-REFERENCE.md
git commit -m "docs(sensor): sensor help text, Commands/ha-mqtt/SCRIPT-REFERENCE updates"
```