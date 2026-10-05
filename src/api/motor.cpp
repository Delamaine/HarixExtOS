#include "motor.h"
#include <Arduino.h>

namespace harixos {
namespace api {

// Static pool initialization
MotorAPI MotorAPI::pool_[MotorAPI::kMaxMotors];
bool MotorAPI::inited_[MotorAPI::kMaxMotors] = {false};
bool MotorAPI::pwmConfigSet_ = false;

MotorAPI::MotorAPI() : initialized_(false) {
  config_.index = 0;
  config_.namePin = 0;
  config_.speedPin = 0;
  config_.pwmChannel = 0;
  config_.acceleration = 100;
  config_.lastSpeed = 0;
  config_.lastDir = STOPPED;
}

int MotorAPI::init(uint8_t index, uint8_t namePin, uint8_t speedPin) {
  for (int i = 0; i < kMaxMotors; ++i) {
    if (!inited_[i]) {
      pool_[i].config_.index = index;
      pool_[i].config_.namePin = namePin;
      pool_[i].config_.speedPin = speedPin;
      pool_[i].config_.pwmChannel = i;
      pool_[i].config_.acceleration = 100;
      pool_[i].config_.lastSpeed = 0;
      pool_[i].config_.lastDir = STOPPED;
      pool_[i].initialized_ = true;
      inited_[i] = true;

      // Configure PWM once
      if (!pwmConfigSet_) {
        analogWriteFreq(kPwmFrequency);
        analogWriteResolution(kPwmResolution);
        pwmConfigSet_ = true;
      }

      // Set pins
      pinMode(namePin, OUTPUT);
      pinMode(speedPin, OUTPUT);

      // Start stopped
      digitalWrite(namePin, LOW);
      analogWrite(speedPin, 0);

      return i;
    }
  }
  return -1;
}

MotorAPI* MotorAPI::findByIndex(uint8_t index) {
  for (int i = 0; i < kMaxMotors; ++i) {
    if (inited_[i] && pool_[i].config_.index == index) {
      return &pool_[i];
    }
  }
  return nullptr;
}

MotorAPI* MotorAPI::findByName(const String &name) {
  if (name == F("m1") || name == F("0")) return findByIndex(0);
  if (name == F("m2") || name == F("1")) return findByIndex(1);
  return nullptr;
}

void MotorAPI::listAll(Stream &out) {
  bool found = false;
  for (int i = 0; i < kMaxMotors; ++i) {
    if (inited_[i]) {
      out.printf("Motor %d (%s): name=GPIO%d, speed=GPIO%d, speed=%d%%, direction=%s",
                 pool_[i].config_.index,
                 pool_[i].config_.index == 0 ? "M1" : "M2",
                 pool_[i].config_.namePin,
                 pool_[i].config_.speedPin,
                 pool_[i].config_.lastSpeed,
                 pool_[i].config_.lastDir == FORWARD ? "forward" :
                 pool_[i].config_.lastDir == REVERSE ? "reverse" :
                 pool_[i].config_.lastDir == BRAKED ? "braked" : "stopped");
      if (pool_[i].config_.acceleration != 100) {
        out.printf(", accel=%d steps/s", pool_[i].config_.acceleration);
      }
      out.println();
      found = true;
    }
  }
  if (!found) {
    out.println("No motors initialized.");
  }
}

uint8_t MotorAPI::speedToDuty(uint8_t speed) const {
  return (uint8_t)((speed * 255) / kMaxSpeed);
}

void MotorAPI::applyState() {
  uint8_t duty = speedToDuty(config_.lastSpeed);
  analogWrite(config_.speedPin, duty);

  switch (config_.lastDir) {
    case FORWARD:
      digitalWrite(config_.namePin, HIGH);
      break;
    case REVERSE:
      digitalWrite(config_.namePin, LOW);
      break;
    case STOPPED:
      digitalWrite(config_.namePin, LOW);
      break;
    case BRAKED:
      digitalWrite(config_.namePin, HIGH);
      break;
  }
}

void MotorAPI::forward() {
  config_.lastDir = FORWARD;
  applyState();
}

void MotorAPI::reverse() {
  config_.lastDir = REVERSE;
  applyState();
}

void MotorAPI::stop() {
  config_.lastDir = STOPPED;
  applyState();
}

void MotorAPI::brake() {
  config_.lastDir = BRAKED;
  applyState();
}

void MotorAPI::setSpeed(uint8_t speed) {
  if (speed > kMaxSpeed) speed = kMaxSpeed;
  config_.lastSpeed = speed;
  applyState();
}

uint8_t MotorAPI::getSpeed() const {
  return config_.lastSpeed;
}

void MotorAPI::setDirection(Direction dir) {
  config_.lastDir = dir;
  applyState();
}

MotorAPI::Direction MotorAPI::getDirection() const {
  return config_.lastDir;
}

void MotorAPI::setSpeedDirection(uint8_t speed, Direction dir) {
  if (speed > kMaxSpeed) speed = kMaxSpeed;
  config_.lastSpeed = speed;
  config_.lastDir = dir;
  applyState();
}

void MotorAPI::accelerate(uint8_t targetSpeed, uint32_t durationMs) {
  if (targetSpeed > kMaxSpeed) targetSpeed = kMaxSpeed;
  uint8_t startSpeed = config_.lastSpeed;
  int steps = targetSpeed - startSpeed;
  if (steps < 0) steps = -steps;

  uint32_t delayPerStep = (steps > 0) ? durationMs / steps : 1;
  uint8_t current = startSpeed;

  for (int i = 0; i < steps; ++i) {
    if (startSpeed < targetSpeed) {
      current = startSpeed + i + 1;
    } else {
      current = startSpeed - i - 1;
    }
    config_.lastSpeed = current;
    applyState();
    delay(delayPerStep);
  }

  config_.lastSpeed = targetSpeed;
  applyState();
}

void MotorAPI::decelerate(uint8_t targetSpeed, uint32_t durationMs) {
  accelerate(targetSpeed, durationMs);
}

void MotorAPI::runFor(uint8_t speed, Direction dir, uint32_t durationMs) {
  setSpeedDirection(speed, dir);
  delay(durationMs);
  stop();
}

namespace {

// Resolve the `[m1|m2]` selector used by motor actions.
// Returns -1 for "all motors", 0/1 for m1/m2, or -2 when the selector
// names a motor that does not exist.
int resolveMotorTarget(const String &tail) {
  if (tail.length() == 0) return -1;
  if (tail == F("m1") || tail == F("0")) return 0;
  if (tail == F("m2") || tail == F("1")) return 1;
  return -2;
}

const char *kMotorNames[2] = {"M1", "M2"};

}  // namespace

ApiResult MotorAPI::runCommand(const String &args, Stream &out) {
  String rest = args;
  rest.trim();
  if (rest.length() == 0) {
    return ApiResult(API_INVALID_ARGUMENT,
                     "Usage: motor init|forward|reverse|stop|brake|speed|list");
  }

  int sep = rest.indexOf(' ');
  String action = sep < 0 ? rest : rest.substring(0, sep);
  String tail = sep < 0 ? String("") : rest.substring(sep + 1);
  tail.trim();

  if (action.equalsIgnoreCase("init")) {
    if (tail.length() == 0) {
      String usage = "Usage: motor init [m1|m2] [name_pin] [speed_pin]\n";
      usage += "  M1: name=GPIO14(D5), speed=GPIO12(D6)\n";
      usage += "  M2: name=GPIO13(D7), speed=GPIO5(D1)";
      return ApiResult(API_INVALID_ARGUMENT, usage);
    }
    String motorName = tail;
    uint8_t namePin, speedPin;

    if (motorName == F("m1") || motorName == F("0")) {
      namePin = 14; speedPin = 12;
    } else if (motorName == F("m2") || motorName == F("1")) {
      namePin = 13; speedPin = 5;
    } else {
      namePin = tail.toInt();
      sep = tail.indexOf(' ');
      speedPin = (sep >= 0) ? tail.substring(sep + 1).toInt() : 12;
    }

    uint8_t index = (motorName == F("m1") || motorName == F("0")) ? 0 : 1;
    if (init(index, namePin, speedPin) < 0) {
      return ApiResult(API_ERROR, "Motor pool full.");
    }
    out.printf("Motor %s initialized (GPIO%d name, GPIO%d speed)\r\n",
               motorName.c_str(), namePin, speedPin);
    return ApiResult(API_OK, "");
  }

  if (action.equalsIgnoreCase("forward") || action.equalsIgnoreCase("reverse") ||
      action.equalsIgnoreCase("stop") || action.equalsIgnoreCase("brake")) {
    bool all = tail.length() == 0;
    int target = resolveMotorTarget(tail);
    if (target == -2) {
      return ApiResult(API_INVALID_ARGUMENT, "Motor must be m1 or m2.");
    }

    MotorAPI *motors[2] = {nullptr, nullptr};
    if (all) {
      for (int i = 0; i < 2; ++i) motors[i] = findByIndex(i);
      if (!motors[0] && !motors[1]) {
        return ApiResult(API_ERROR, "No motors initialized. Use: motor init m1|m2");
      }
    } else {
      motors[target] = findByIndex(target);
      if (!motors[target]) {
        return ApiResult(API_ERROR,
                         String("Motor ") + kMotorNames[target] + " not initialized.");
      }
    }

    const char *verb = nullptr;
    const char *done = nullptr;
    if (action.equalsIgnoreCase("forward")) {
      verb = "forward."; done = "All motors forward.";
    } else if (action.equalsIgnoreCase("reverse")) {
      verb = "reverse."; done = "All motors reverse.";
    } else if (action.equalsIgnoreCase("stop")) {
      verb = "stopped."; done = "All motors stopped.";
    } else {
      verb = "braked."; done = "All motors braked.";
    }

    for (int i = 0; i < 2; ++i) {
      if (!motors[i]) continue;
      if (action.equalsIgnoreCase("forward")) motors[i]->forward();
      else if (action.equalsIgnoreCase("reverse")) motors[i]->reverse();
      else if (action.equalsIgnoreCase("stop")) motors[i]->stop();
      else motors[i]->brake();
      if (!all) out.printf("Motor %s %s\r\n", kMotorNames[i], verb);
    }
    if (all) out.println(done);
    return ApiResult(API_OK, "");
  }

  if (action.equalsIgnoreCase("speed") || action.equalsIgnoreCase("spd")) {
    if (tail.length() == 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: motor speed <0-100> [m1|m2]");
    }
    int sep2 = tail.indexOf(' ');
    uint8_t speed = tail.toInt();
    String motorName = (sep2 >= 0) ? tail.substring(sep2 + 1) : String("");
    motorName.trim();

    bool all = motorName.length() == 0;
    int target = resolveMotorTarget(motorName);
    if (target == -2) {
      return ApiResult(API_INVALID_ARGUMENT, "Motor must be m1 or m2.");
    }

    if (all) {
      MotorAPI *found = nullptr;
      for (int i = 0; i < 2; ++i) {
        MotorAPI *m = findByIndex(i);
        if (m) {
          m->setSpeed(speed);
          found = m;
        }
      }
      if (!found) {
        return ApiResult(API_ERROR, "No motors initialized. Use: motor init m1|m2");
      }
      out.printf("All motors speed: %d%%\r\n", speed);
      return ApiResult(API_OK, "");
    }

    MotorAPI *motor = findByIndex(target);
    if (!motor) {
      return ApiResult(API_ERROR,
                       String("Motor ") + kMotorNames[target] + " not initialized.");
    }
    motor->setSpeed(speed);
    out.printf("Motor %s speed: %d%%\r\n", kMotorNames[target], speed);
    return ApiResult(API_OK, "");
  }

  if (action.equalsIgnoreCase("list")) {
    listAll(out);
    return ApiResult(API_OK, "");
  }

  return ApiResult(API_INVALID_ARGUMENT,
                   "Unknown motor action. Use: init, forward, reverse, stop, brake, speed, list");
}

}  // namespace api
}  // namespace harixos
