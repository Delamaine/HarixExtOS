#include "servo.h"
#include <Arduino.h>

namespace harixos {
namespace api {

// Static pool initialization
ServoAPI ServoAPI::pool_[ServoAPI::kMaxServos];
bool ServoAPI::inited_[ServoAPI::kMaxServos] = {false};
bool ServoAPI::pwmFreqSet_ = false;

ServoAPI::ServoAPI() : attached_(false), lastAngle_(90) {
  config_.pin = 0;
  config_.channel = 0;
  config_.minDuty = kMinDutyCycle;
  config_.maxDuty = kMaxDutyCycle;
  config_.calibrationOffset = 0;
}

int ServoAPI::attach(uint8_t pin) {
  for (int i = 0; i < kMaxServos; ++i) {
    if (!inited_[i]) {
      pool_[i].config_.pin = pin;
      pool_[i].config_.channel = i;
      pool_[i].config_.minDuty = kMinDutyCycle;
      pool_[i].config_.maxDuty = kMaxDutyCycle;
      pool_[i].config_.calibrationOffset = 0;
      pool_[i].attached_ = true;
      pool_[i].lastAngle_ = 90;
      inited_[i] = true;

      // Set PWM frequency once
      if (!pwmFreqSet_) {
        analogWriteFreq(kPwmFrequency);
        analogWriteResolution(kPwmResolution);
        pwmFreqSet_ = true;
      }

      // Set initial duty to 0
      analogWrite(pin, 0);

      return i;
    }
  }
  return -1;
}

int ServoAPI::detach(uint8_t pin) {
  for (int i = 0; i < kMaxServos; ++i) {
    if (inited_[i] && pool_[i].config_.pin == pin) {
      analogWrite(pin, 0);
      pool_[i].attached_ = false;
      pool_[i].lastAngle_ = 0;
      return i;
    }
  }
  return -1;
}

ServoAPI* ServoAPI::findByPin(uint8_t pin) {
  for (int i = 0; i < kMaxServos; ++i) {
    if (inited_[i] && pool_[i].config_.pin == pin) {
      return &pool_[i];
    }
  }
  return nullptr;
}

void ServoAPI::listAll(Stream &out) {
  bool found = false;
  for (int i = 0; i < kMaxServos; ++i) {
    if (inited_[i]) {
      out.printf("Servo %d: GPIO%d, channel=%d, %s, angle=%d degrees", i, pool_[i].config_.pin, pool_[i].config_.channel, pool_[i].attached_ ? "attached" : "detached", pool_[i].lastAngle_);
      if (pool_[i].config_.calibrationOffset != 0) {
        out.printf(", cal_offset=%d", pool_[i].config_.calibrationOffset);
      }
      out.println();
      found = true;
    }
  }
  if (!found) {
    out.println("No servos attached.");
  }
}

uint8_t ServoAPI::angleToDuty(uint8_t angle) {
  return (uint8_t)((angle * (config_.maxDuty - config_.minDuty)) / kMaxAngle + config_.minDuty);
}

void ServoAPI::writeAngle(uint8_t angle) {
  if (!attached_) return;

  if (angle > kMaxAngle) angle = kMaxAngle;

  lastAngle_ = angle;
  uint8_t duty = angleToDuty(angle);
  analogWrite(config_.pin, duty);
}

void ServoAPI::writeMicroseconds(int us) {
  if (!attached_) return;

  if (us < 500) us = 500;
  if (us > 2500) us = 2500;

  int duty = (us * 255) / 20000;
  if (duty > 255) duty = 255;

  lastAngle_ = (uint8_t)((duty * 180) / 255);
  analogWrite(config_.pin, duty);
}

uint8_t ServoAPI::readAngle() const {
  return lastAngle_;
}

void ServoAPI::writeCalibrated(uint8_t angle) {
  if (!attached_) return;

  int calibrated = angle + config_.calibrationOffset;
  if (calibrated < kMinAngle) calibrated = kMinAngle;
  if (calibrated > kMaxAngle) calibrated = kMaxAngle;

  writeAngle((uint8_t)calibrated);
}

ApiResult ServoAPI::runCommand(const String &args, Stream &out) {
  String rest = args;
  rest.trim();
  if (rest.length() == 0) {
    return ApiResult(API_INVALID_ARGUMENT,
                     "Usage: servo write <pin> <angle> | servo attach <pin> | "
                     "servo detach <pin> | servo read <pin> | servo list");
  }

  int sep = rest.indexOf(' ');
  String action = sep < 0 ? rest : rest.substring(0, sep);
  String tail = sep < 0 ? String("") : rest.substring(sep + 1);
  tail.trim();

  if (action.equalsIgnoreCase("attach")) {
    if (tail.length() == 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: servo attach <pin>");
    }
    uint8_t pin = tail.toInt();
    int result = attach(pin);
    if (result < 0) {
      return ApiResult(API_ERROR, "Servo pool full.");
    }
    out.printf("Servo attached to GPIO%d (instance %d)\r\n", pin, result);
    return ApiResult(API_OK, "");
  }

  if (action.equalsIgnoreCase("detach")) {
    if (tail.length() == 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: servo detach <pin>");
    }
    uint8_t pin = tail.toInt();
    if (detach(pin) < 0) {
      return ApiResult(API_ERROR, "Servo not found.");
    }
    out.printf("Servo detached from GPIO%d\r\n", pin);
    return ApiResult(API_OK, "");
  }

  if (action.equalsIgnoreCase("write")) {
    if (tail.length() == 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: servo write <pin> <angle>");
    }
    int sep2 = tail.indexOf(' ');
    uint8_t pin = (sep2 < 0) ? tail.toInt() : tail.substring(0, sep2).toInt();
    uint8_t angle = (sep2 < 0) ? 90 : tail.substring(sep2 + 1).toInt();
    if (angle > 180) angle = 180;

    ServoAPI *servo = findByPin(pin);
    if (!servo && attach(pin) >= 0) {
      servo = findByPin(pin);
    }
    if (!servo) {
      return ApiResult(API_ERROR, "Servo not found.");
    }
    servo->writeAngle(angle);
    out.printf("Servo GPIO%d -> %d degrees\r\n", pin, angle);
    return ApiResult(API_OK, "");
  }

  if (action.equalsIgnoreCase("read")) {
    if (tail.length() == 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: servo read <pin>");
    }
    uint8_t pin = tail.toInt();
    ServoAPI *servo = findByPin(pin);
    if (!servo) {
      return ApiResult(API_ERROR, "Servo not found.");
    }
    out.printf("Servo GPIO%d: %d degrees\r\n", pin, servo->readAngle());
    return ApiResult(API_OK, "");
  }

  if (action.equalsIgnoreCase("list")) {
    listAll(out);
    return ApiResult(API_OK, "");
  }

  return ApiResult(API_INVALID_ARGUMENT,
                   "Unknown servo action. Use: attach, detach, write, read, list");
}

}  // namespace api
}  // namespace harixos
