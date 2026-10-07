#pragma once

#include <Arduino.h>

#include "api_types.h"
#include "kernel/iot/sensor_logic.h"

namespace harixos {
namespace api {

// Named sensor registry (ultrasonic, DHT22, DS18B20 daisy-chain, BME280).
// The pure registry/formatting rules live in kernel/iot/sensor_logic (native
// tested); this class owns the pool, persistence, hardware backends, MQTT
// state publish and the legacy pin-addressed aliases.
class SensorAPI {
public:
  static const int kMaxSensors = 8;

  // Shared `sensor <action> ...` parser used by the shell and the script
  // engine. Informational output goes to `out`; failures come back as
  // ApiResult so scripts report [ERROR] instead of silently succeeding.
  static ApiResult runCommand(const String& args, Stream& out);
};

// Persisted registry file (device).
extern const char* kSensorsPath;

// Load /sensors.conf into the pool at boot. Malformed lines are warned and
// skipped; a missing file is an empty registry.
void beginSensor();

// Publish retained cached readings for every read quantity (MQTT reconnect
// hook, mirror of publishRelayStates). Skipped quantities keep their old
// retained value. No-op when MQTT is down.
void publishSensorStates();

// Comma-joined registered sensor names for HA discovery (mirror of
// collectRelayNames).
String collectSensorNames();

// Look up a registered sensor's type by name. Returns false if not registered.
bool sensorTypeOf(const char* name, uint8_t* type);

}  // namespace api
}  // namespace harixos
