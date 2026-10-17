#ifndef HARIXOS_SENSOR_LOGIC_H
#define HARIXOS_SENSOR_LOGIC_H

#include <stdint.h>
#include <cstddef>

namespace harixos { namespace iot {

// Native-testable sensor core (no Arduino dependency).
//
// The Arduino layer owns the pool, persistence and the hardware backends;
// this file holds only the pure registry/formatting logic so it can be unit
// tested without a board.

enum SensorType : uint8_t {
  kSensorUltrasonic = 0,
  kSensorDht22,
  kSensorDs18b20,
  kSensorBme280,
  kSensorSoil,
  kSensorLdr,
  kSensorBh1750,
  kSensorAht20,
  kSensorTypeCount
};

enum SensorQty : uint8_t {
  kQtyDistance = 0,
  kQtyTemperature,
  kQtyHumidity,
  kQtyPressure,
  kQtyMoisture,
  kQtyIlluminance,
  kSensorQtyCount
};

// One registered sensor. `a`/`b` meaning depends on type:
//   ultrasonic: a = trigger, b = echo
//   dht22:      a = data pin
//   ds18b20:    a = data pin, b = device index on the 1-Wire bus (daisy-chain)
//   bme280:     no pins (I2C address probed at read time)
//   soil/ldr:   a = 17 (A0, the only analog-capable pin)
//   bh1750:     no pins (I2C 0x23)
//   aht20:      no pins (I2C 0x38)
struct SensorDef {
  char name[16];  // NUL-terminated, 1-15 chars [A-Za-z0-9_-]
  uint8_t type;   // SensorType value
  uint8_t a;
  uint8_t b;
  float temperature;  // last-good cache, NaN = never read
  float humidity;
  float pressure;
  float distance;
  float moisture;     // %
  float illuminance;  // % (ldr) or lx (bh1750)
};

// Names land in MQTT topics and HA discovery ids; reuse the relay rule.
bool sensorValidName(const char* s);

// Parse a type name. Returns false on anything except the four names.
bool sensorParseType(const char* s, uint8_t* out);

// Canonical type name; returns "" for invalid type values.
const char* sensorTypeName(uint8_t type);

// Index of `name` in the pool, or -1.
int sensorFindIndex(const SensorDef* pool, int count, const char* name);

// True if a sensor already uses pins (a,b), ignoring `skip` index (-1 = none).
bool sensorHasPin(const SensorDef* pool, int count, uint8_t a, uint8_t b, int skip);

// Store a reading for quantity `qty`; NaN clears it.
void sensorSetReading(SensorDef& d, uint8_t qty, float v);

// Whether `type` produces quantity `qty`.
bool sensorHasQuantity(uint8_t type, uint8_t qty);

// "distance" | "temperature" | "humidity" | "pressure" | "moisture" |
// "illuminance"; "" for invalid.
const char* sensorQtyName(uint8_t qty);

// Parse a quantity name (inverse of sensorQtyName). Returns false otherwise.
bool sensorParseQty(const char* s, uint8_t* out);

// "cm" | "°C" | "%" | "hPa" | "lx"; "" for invalid.
const char* sensorUnit(uint8_t type, uint8_t qty);

// HA device_class: "distance" | "temperature" | "humidity" | "pressure" |
// "moisture" | "illuminance"; "" when no class fits (ldr's % is not a valid
// illuminance unit in HA, so ldr gets none).
const char* sensorDeviceClass(uint8_t type, uint8_t qty);

// Format a reading payload as "%.1f".
void sensorFormatValue(float v, char* out, size_t n);

// Serialize "name|typeName|a|b" for persistence.
void sensorPersistLine(const SensorDef& d, char* out, size_t n);

// Parse a persisted line back into `out`. False on empty line, wrong field
// count, invalid name, unknown type, or a pin value that contradicts the type.
bool sensorParseLine(const char* line, SensorDef* out);

// AHT20 CRC-8: poly 0x31, init 0xFF, no xorout, no reflection (datasheet
// §5.4.4). Covers the status byte plus the 5 data bytes.
uint8_t ahtCrc8(const uint8_t* data, size_t n);

// Decode one 7-byte AHT20 measurement frame (status, 5 data, CRC). False on
// busy status or CRC mismatch; otherwise RH = raw20*100/2^20 %,
// T = raw20*200/2^20 - 50 °C.
bool ahtDecodeFrame(const uint8_t frame[7], float* temperature, float* humidity);

// Build the HA sensor config payload (no JSON lib, String-free).
// hp  = sanitized prefix (haPrefix()); prefix = raw mqttPrefix
// (used for state_topic only). Returns false if it doesn't fit in out.
bool sensorDiscoveryPayload(const char* hp, const char* prefix,
                            const char* name, uint8_t type, uint8_t qty,
                            char* out, size_t max);

}}

#endif  // HARIXOS_SENSOR_LOGIC_H