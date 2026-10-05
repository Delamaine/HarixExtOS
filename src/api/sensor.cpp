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

ApiResult SensorAPI::runCommand(const String &args, Stream &out) {
  String rest = args;
  rest.trim();
  if (rest.length() == 0) {
    return ApiResult(API_INVALID_ARGUMENT,
                     "Usage: sensor ping [trigger] [echo] | sensor read [echo] | "
                     "sensor init <trigger> <echo> | sensor list");
  }

  int sep = rest.indexOf(' ');
  String action = sep < 0 ? rest : rest.substring(0, sep);
  String tail = sep < 0 ? String("") : rest.substring(sep + 1);
  tail.trim();

  if (action.equalsIgnoreCase("init")) {
    if (tail.length() == 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: sensor init <trigger_pin> <echo_pin>");
    }
    int sep2 = tail.indexOf(' ');
    if (sep2 < 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: sensor init <trigger_pin> <echo_pin>");
    }
    uint8_t trigger = tail.substring(0, sep2).toInt();
    tail = tail.substring(sep2 + 1);
    tail.trim();
    uint8_t echo = tail.toInt();
    int result = init(trigger, echo);
    if (result < 0) {
      return ApiResult(API_ERROR, "Sensor pool full.");
    }
    out.printf("Sensor initialized: trigger=%d, echo=%d (instance %d)\r\n",
               trigger, echo, result);
    return ApiResult(API_OK, "");
  }

  if (action.equalsIgnoreCase("ping")) {
    uint8_t trigger = 4;
    uint8_t echo = 5;
    if (tail.length() > 0) {
      int sep2 = tail.indexOf(' ');
      if (sep2 >= 0) {
        trigger = tail.substring(0, sep2).toInt();
        tail = tail.substring(sep2 + 1);
        tail.trim();
        echo = tail.toInt();
      } else {
        trigger = tail.toInt();
        echo = tail.toInt();
      }
    }
    SensorAPI *sensor = findByTriggerPin(trigger);
    if (!sensor && init(trigger, echo) >= 0) {
      sensor = findByTriggerPin(trigger);
    }
    if (!sensor) {
      return ApiResult(
          API_ERROR, "Sensor not found. Initialize with: sensor init <trigger> <echo>");
    }
    int mm = sensor->readDistanceMm();
    if (mm > 0) {
      out.printf("Distance: %d mm (%.2f cm, %.3f m)\r\n", mm,
                 sensor->readDistanceCm(), sensor->readDistanceM());
    } else {
      out.println(F("No object detected or timeout."));
    }
    return ApiResult(API_OK, "");
  }

  if (action.equalsIgnoreCase("read")) {
    uint8_t echoPin = 5;
    if (tail.length() > 0) {
      echoPin = tail.toInt();
    }
    SensorAPI *sensor = findByEchoPin(echoPin);
    if (!sensor) {
      return ApiResult(API_ERROR, "Sensor not found.");
    }
    int mm = sensor->readDistanceMm();
    if (mm > 0) {
      out.printf("Distance: %d mm\r\n", mm);
      out.printf("Object detected: %s\r\n", sensor->hasObject() ? "yes" : "no");
    } else {
      out.println(F("No object detected or timeout."));
    }
    return ApiResult(API_OK, "");
  }

  if (action.equalsIgnoreCase("list")) {
    listAll(out);
    return ApiResult(API_OK, "");
  }

  return ApiResult(API_INVALID_ARGUMENT,
                   "Unknown sensor action. Use: ping, read, init, list");
}

}  // namespace api
}  // namespace harixos
