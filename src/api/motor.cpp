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

}  // namespace api
}  // namespace harixos
