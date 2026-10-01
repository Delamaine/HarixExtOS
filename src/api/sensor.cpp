#include "sensor.h"

namespace harixos {
namespace api {

// Static pool initialization
SensorAPI SensorAPI::pool_[SensorAPI::kMaxSensors];
bool SensorAPI::inited_[SensorAPI::kMaxSensors] = {false};

SensorAPI::SensorAPI() : initialized_(false) {
  config_.triggerPin = 0;
  config_.echoPin = 0;
  config_.timeoutUs = kDefaultTimeoutUs;
  config_.calibrationOffsetMm = 0;
}

int SensorAPI::init(uint8_t triggerPin, uint8_t echoPin, uint16_t timeoutUs) {
  for (int i = 0; i < kMaxSensors; ++i) {
    if (!inited_[i]) {
      pool_[i].config_.triggerPin = triggerPin;
      pool_[i].config_.echoPin = echoPin;
      pool_[i].config_.timeoutUs = timeoutUs;
      pool_[i].config_.calibrationOffsetMm = 0;
      pool_[i].initialized_ = true;
      inited_[i] = true;

      pinMode(triggerPin, OUTPUT);
      pinMode(echoPin, INPUT);

      // Ensure trigger is LOW before first use
      digitalWrite(triggerPin, LOW);

      return i;
    }
  }
  return -1;  // Pool full
}

SensorAPI* SensorAPI::findByEchoPin(uint8_t echoPin) {
  for (int i = 0; i < kMaxSensors; ++i) {
    if (inited_[i] && pool_[i].config_.echoPin == echoPin) {
      return &pool_[i];
    }
  }
  return nullptr;
}

SensorAPI* SensorAPI::findByTriggerPin(uint8_t triggerPin) {
  for (int i = 0; i < kMaxSensors; ++i) {
    if (inited_[i] && pool_[i].config_.triggerPin == triggerPin) {
      return &pool_[i];
    }
  }
  return nullptr;
}

void SensorAPI::listAll(Stream &out) {
  bool found = false;
  for (int i = 0; i < kMaxSensors; ++i) {
    if (inited_[i]) {
      out.printf("Sensor %d: trigger=GPIO%d, echo=GPIO%d", i, pool_[i].config_.triggerPin, pool_[i].config_.echoPin);
      if (pool_[i].initialized_) {
        int mm = pool_[i].readDistanceMm();
        if (mm > 0) {
          out.printf(", distance=%d mm", mm);
        } else {
          out.print(", no object");
        }
      } else {
        out.print(", idle");
      }
      out.println();
      found = true;
    }
  }
  if (!found) {
    out.println("No sensors initialized.");
  }
}

int SensorAPI::readDistanceMm() {
  if (!initialized_) return -1;

  // Trigger pulse: 10us HIGH
  digitalWrite(config_.triggerPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(config_.triggerPin, LOW);

  // Read echo pulse
  uint16_t pulse = pulseIn(config_.echoPin, HIGH, config_.timeoutUs);
  if (pulse == 0) {
    return -1;  // No object or timeout
  }

  // Distance = (pulse * speed_of_sound) / 2
  // Speed of sound = 343m/s = 0.0343cm/us
  // Distance (mm) = pulse (us) * 0.0343 * 10 / 2 = pulse * 0.1715
  int distanceMm = (pulse * 1715) / 10000 + config_.calibrationOffsetMm;
  return distanceMm;
}

float SensorAPI::readDistanceCm() {
  int mm = readDistanceMm();
  if (mm < 0) return -1.0;
  return mm / 10.0;
}

float SensorAPI::readDistanceM() {
  int mm = readDistanceMm();
  if (mm < 0) return -1.0;
  return mm / 1000.0;
}

bool SensorAPI::hasObject(int maxDistanceMm) {
  int mm = readDistanceMm();
  return (mm > 0 && mm <= maxDistanceMm);
}

}  // namespace api
}  // namespace harixos
