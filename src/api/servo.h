#pragma once

#include <Arduino.h>

namespace harixos {
namespace api {

// SG90 servo motor API using ESP8266 ledc PWM
class ServoAPI {
public:
  // Maximum number of servo instances in the pool
  static const int kMaxServos = 4;

  // SG90 specifications
  static const uint8_t kMinAngle = 0;
  static const uint8_t kMaxAngle = 180;

  // PWM settings for SG90 (50Hz)
  static const int kPwmFrequency = 50;       // Hz
  static const int kPwmResolution = 8;       // bits
  // Duty cycle range for SG90 (typical values)
  // 0.5ms pulse = 0 degrees, 2.5ms pulse = 180 degrees
  // At 50Hz, period = 20ms, duty = pulse/period * 1024 (10-bit)
  static const int kMinDutyCycle = 13;       // ~0.5ms at 50Hz (10-bit)
  static const int kMaxDutyCycle = 61;       // ~2.5ms at 50Hz (10-bit)

  struct ServoConfig {
    uint8_t pin;
    uint8_t channel;    // ledc channel (0-7 on ESP8266)
    uint8_t minDuty;
    uint8_t maxDuty;
    int calibrationOffset;  // Degrees offset
  };

  // Attach a servo to a pin (uses first available ledc channel)
  // Returns 0 on success, -1 if pool is full
  static int attach(uint8_t pin);

  // Detach a servo from its pin
  static int detach(uint8_t pin);

  // Find servo by pin
  static ServoAPI* findByPin(uint8_t pin);

  // List all attached servos
  static void listAll(Stream &out);

  // Write angle (0-180 degrees)
  void writeAngle(uint8_t angle);

  // Write pulse width in microseconds (0.5ms - 2.5ms)
  void writeMicroseconds(int us);

  // Read current angle (approximate, tracks last written value)
  uint8_t readAngle() const;

  // Set servo to position (calibrated)
  void writeCalibrated(uint8_t angle);

  // Get servo configuration
  const ServoConfig& config() const { return config_; }

  // Get pin
  uint8_t pin() const { return config_.pin; }

  // Check if servo is attached
  bool attached() const { return attached_; }

private:
  ServoAPI();

  ServoConfig config_;
  bool attached_;
  uint8_t lastAngle_;

  // Static pool of servo instances
  static ServoAPI pool_[kMaxServos];
  static bool inited_[kMaxServos];
  static bool pwmFreqSet_;

  // Calculate duty cycle for angle
  uint8_t angleToDuty(uint8_t angle);
};

}  // namespace api
}  // namespace harixos
