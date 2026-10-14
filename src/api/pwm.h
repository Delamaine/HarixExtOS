#pragma once

#include <Arduino.h>

namespace harixos {
namespace api {

// ESP8266 analogWriteFreq()/analogWriteResolution() are global: one setting
// applies to every analogWrite() pin, so motor and servo PWM must share a
// single configuration, set once in setup(). 50 Hz is the only value both
// device classes tolerate: servos need a 20 ms frame; motors run at 50 Hz
// but buzz audibly at low duty.
// ponytail: one global frequency; per-channel PWM needs a custom driver —
// add only if a build ever needs motors and servos at different rates.
constexpr int kPwmFrequency = 50;    // Hz
constexpr int kPwmResolution = 8;    // bits

inline void configureGlobalPwm() {
  analogWriteFreq(kPwmFrequency);
  analogWriteResolution(kPwmResolution);
}

}  // namespace api
}  // namespace harixos
