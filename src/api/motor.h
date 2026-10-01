#pragma once

#include <Arduino.h>

namespace harixos {
namespace api {

// L293D Motor Shield API for ESP8266 (NodeMCU Motor Shield)
// Controls 2 DC motors via PWM + GPIO direction pins
class MotorAPI {
public:
  static const int kMaxMotors = 2;

  // Speed range (PWM duty cycle 0-255)
  static const uint8_t kMinSpeed = 0;
  static const uint8_t kMaxSpeed = 100;

  // PWM settings
  static const int kPwmFrequency = 5000;  // 5kHz for DC motors
  static const int kPwmResolution = 8;    // 8-bit resolution

  // Direction states
  enum Direction {
    FORWARD = 0,
    REVERSE = 1,
    STOPPED = 2,
    BRAKED = 3
  };

  struct MotorConfig {
    uint8_t index;          // Motor index (0 or 1)
    uint8_t namePin;        // D5(14) for M1, D7(13) for M2
    uint8_t speedPin;       // D6(12) for M1, D1(5) for M2
    uint8_t pwmChannel;     // ledc channel
    int acceleration;       // Steps per second for accel/decel
    uint8_t lastSpeed;      // Last set speed (0-100%)
    Direction lastDir;      // Last set direction
  };

  // Initialize a motor with given pins
  static int init(uint8_t index, uint8_t namePin, uint8_t speedPin);

  // Find motor by index
  static MotorAPI* findByIndex(uint8_t index);

  // Find motor by name (M1, M2, 0, 1)
  static MotorAPI* findByName(const String &name);

  // List all initialized motors
  static void listAll(Stream &out);

  // Basic control
  void forward();
  void reverse();
  void stop();
  void brake();

  // Speed control (0-100%)
  void setSpeed(uint8_t speed);
  uint8_t getSpeed() const;

  // Direction control
  void setDirection(Direction dir);
  Direction getDirection() const;

  // Combined speed + direction
  void setSpeedDirection(uint8_t speed, Direction dir);

  // Advanced: acceleration/deceleration
  void accelerate(uint8_t targetSpeed, uint32_t durationMs = 1000);
  void decelerate(uint8_t targetSpeed, uint32_t durationMs = 1000);

  // Timing: run for specified duration then stop
  void runFor(uint8_t speed, Direction dir, uint32_t durationMs);

  // Get motor configuration
  const MotorConfig& config() const { return config_; }

  // Check if motor is initialized
  bool initialized() const { return initialized_; }

private:
  MotorAPI();

  MotorConfig config_;
  bool initialized_;

  // Static pool of motor instances
  static MotorAPI pool_[kMaxMotors];
  static bool inited_[kMaxMotors];
  static bool pwmConfigSet_;

  // Calculate PWM duty cycle from speed percentage
  uint8_t speedToDuty(uint8_t speed) const;

  // Apply PWM and direction pins
  void applyState();
};

}  // namespace api
}  // namespace harixos
