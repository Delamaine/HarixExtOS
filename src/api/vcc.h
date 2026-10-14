#pragma once

#include <Arduino.h>

namespace harixos {
namespace api {
namespace vcc {

// ponytail: one-shot boot capture. The ESP8266 has a single ADC: the first
// analogRead(A0) (adc command, telemetry) switches it out of VCC mode and
// ESP.getVcc() returns 0xFFFF forever after. Capture before any A0 read;
// 0 means "unavailable".
inline uint16_t& cached() {
  static uint16_t value = 0;
  return value;
}

inline void capture() {
  if (cached() == 0) {
    const uint16_t v = ESP.getVcc();
    if (v != 0 && v != 0xFFFF) cached() = v;
  }
}

}  // namespace vcc
}  // namespace api
}  // namespace harixos
