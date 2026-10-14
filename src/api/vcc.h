#pragma once

#include <Arduino.h>

namespace harixos {
namespace api {
namespace vcc {

// ponytail: cached sentinel check. The ESP8266 has a single ADC and the core
// defaults to TOUT mode (vdd33_const=33), so ESP.getVcc() returns 0xFFFF
// unless the firmware is compiled with ADC_MODE(ADC_VCC) — which breaks
// analogRead(A0). With A0 in use (default), this is always "unavailable";
// the call still works if someone builds with ADC_MODE(ADC_VCC).
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
