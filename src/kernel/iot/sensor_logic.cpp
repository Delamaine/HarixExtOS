#include "sensor_logic.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "relay_logic.h"

namespace harixos {
namespace iot {

bool sensorValidName(const char* s) {
  return relayValidName(s);
}

bool sensorParseType(const char* s, uint8_t* out) {
  static const char* kNames[kSensorTypeCount] = {
      "ultrasonic", "dht22", "ds18b20", "bme280"};
  for (uint8_t i = 0; i < kSensorTypeCount; ++i) {
    if (strcmp(s, kNames[i]) == 0) {
      *out = i;
      return true;
    }
  }
  return false;
}

const char* sensorTypeName(uint8_t type) {
  static const char* kNames[kSensorTypeCount] = {
      "ultrasonic", "dht22", "ds18b20", "bme280"};
  return type < kSensorTypeCount ? kNames[type] : "";
}

int sensorFindIndex(const SensorDef* pool, int count, const char* name) {
  for (int i = 0; i < count; ++i) {
    if (strcmp(pool[i].name, name) == 0) return i;
  }
  return -1;
}

bool sensorHasPin(const SensorDef* pool, int count, uint8_t a, uint8_t b, int skip) {
  for (int i = 0; i < count; ++i) {
    if (i == skip) continue;
    if (pool[i].a == a && pool[i].b == b) return true;
  }
  return false;
}

void sensorSetReading(SensorDef& d, uint8_t qty, float v) {
  switch (qty) {
    case kQtyDistance:     d.distance = v; break;
    case kQtyTemperature:  d.temperature = v; break;
    case kQtyHumidity:     d.humidity = v; break;
    case kQtyPressure:     d.pressure = v; break;
    default: break;
  }
}

bool sensorHasQuantity(uint8_t type, uint8_t qty) {
  switch (type) {
    case kSensorUltrasonic: return qty == kQtyDistance;
    case kSensorDht22:      return qty == kQtyTemperature || qty == kQtyHumidity;
    case kSensorDs18b20:    return qty == kQtyTemperature;
    case kSensorBme280:
      return qty == kQtyTemperature || qty == kQtyHumidity || qty == kQtyPressure;
    default: return false;
  }
}

const char* sensorQtyName(uint8_t qty) {
  static const char* kNames[kSensorQtyCount] = {
      "distance", "temperature", "humidity", "pressure"};
  return qty < kSensorQtyCount ? kNames[qty] : "";
}

const char* sensorUnit(uint8_t type, uint8_t qty) {
  (void)type;
  switch (qty) {
    case kQtyDistance:     return "cm";
    case kQtyTemperature:  return "\xC2\xB0" "C";
    case kQtyHumidity:     return "%";
    case kQtyPressure:     return "hPa";
    default: return "";
  }
}

const char* sensorDeviceClass(uint8_t type, uint8_t qty) {
  (void)type;
  switch (qty) {
    case kQtyDistance:     return "distance";
    case kQtyTemperature:  return "temperature";
    case kQtyHumidity:     return "humidity";
    case kQtyPressure:     return "pressure";
    default: return "";
  }
}

void sensorFormatValue(float v, char* out, size_t n) {
  snprintf(out, n, "%.1f", v);
}

void sensorPersistLine(const SensorDef& d, char* out, size_t n) {
  snprintf(out, n, "%s|%s|%d|%d", d.name, sensorTypeName(d.type), d.a, d.b);
}

bool sensorParseLine(const char* line, SensorDef* out) {
  if (line == nullptr || *line == '\0') return false;

  // Split into exactly 4 '|'-separated fields.
  const char* f1 = line;
  const char* sep1 = strchr(f1, '|');
  if (!sep1) return false;
  const char* f2 = sep1 + 1;
  const char* sep2 = strchr(f2, '|');
  if (!sep2) return false;
  const char* f3 = sep2 + 1;
  const char* sep3 = strchr(f3, '|');
  if (!sep3) return false;
  const char* f4 = sep3 + 1;
  if (strchr(f4, '|')) return false;  // too many fields

  // Name: copy bounded then validate contents.
  char name[16];
  size_t nameLen = (size_t)(sep1 - f1);
  if (nameLen >= sizeof(name)) return false;
  memcpy(name, f1, nameLen);
  name[nameLen] = '\0';
  if (!sensorValidName(name)) return false;

  char typeName[16];
  size_t typeLen = (size_t)(sep2 - f2);
  if (typeLen >= sizeof(typeName)) return false;
  memcpy(typeName, f2, typeLen);
  typeName[typeLen] = '\0';

  uint8_t type;
  if (!sensorParseType(typeName, &type)) return false;

  uint8_t a = (uint8_t)atoi(f3);
  uint8_t b = (uint8_t)atoi(f4);

  // Pin consistency: pinned types require a != 0 (0 = unset); bme280 takes none.
  if (type == kSensorBme280) {
    if (a != 0) return false;
  } else {
    if (a == 0) return false;
  }

  memset(out, 0, sizeof(*out));
  strcpy(out->name, name);
  out->type = type;
  out->a = a;
  out->b = b;
  out->temperature = NAN;
  out->humidity = NAN;
  out->pressure = NAN;
  out->distance = NAN;
  return true;
}

}}  // namespace harixos::iot