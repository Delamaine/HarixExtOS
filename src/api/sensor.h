#pragma once

#include <Arduino.h>

namespace harixos {
namespace api {

// HC-SR04 ultrasonic sensor API
class SensorAPI {
public:
  // Maximum number of sensor instances in the pool
  static const int kMaxSensors = 2;

  // Maximum distance in mm for timeout calculation
  static const int kMaxDistanceMm = 5000;

  // Default timeout in microseconds (30m round trip at 343m/s = ~17400us, use 30000 for margin)
  static const uint16_t kDefaultTimeoutUs = 30000;

  struct SensorConfig {
    uint8_t triggerPin;
    uint8_t echoPin;
    uint16_t timeoutUs;
    int calibrationOffsetMm;  // Positive = add to reading, negative = subtract
  };

  // Initialize a sensor with given trigger/echo pins
  // Returns 0 on success, -1 if pool is full
  static int init(uint8_t triggerPin, uint8_t echoPin, uint16_t timeoutUs = kDefaultTimeoutUs);

  // Find sensor by echo pin
  static SensorAPI* findByEchoPin(uint8_t echoPin);

  // Find sensor by trigger pin
  static SensorAPI* findByTriggerPin(uint8_t triggerPin);

  // List all initialized sensors
  static void listAll(Stream &out);

  // Read distance in mm (blocking)
  int readDistanceMm();

  // Read distance in cm (blocking)
  float readDistanceCm();

  // Read distance in meters (blocking)
  float readDistanceM();

  // Check if sensor detected an object within range
  bool hasObject(int maxDistanceMm = 4000);

  // Get sensor configuration
  const SensorConfig& config() const { return config_; }

  // Get echo pin
  uint8_t echoPin() const { return config_.echoPin; }

  // Get trigger pin
  uint8_t triggerPin() const { return config_.triggerPin; }

private:
  SensorAPI();

  SensorConfig config_;
  bool initialized_;

  // Static pool of sensor instances
  static SensorAPI pool_[kMaxSensors];
  static bool inited_[kMaxSensors];
};

}  // namespace api
}  // namespace harixos
