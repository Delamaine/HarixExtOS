#pragma once

#include <stdint.h>

namespace harixos {
namespace api {
namespace pins {

// ESP8266 usable pins: 0,2,4,5,12,13,14,15,16.
// GPIO6-11 are wired to the flash chip; GPIO1/3 are UART TX/RX.
// Single source of truth for every pin guard in the codebase.
inline bool isAvailable(uint8_t pin) {
  if (pin > 16) return false;
  if (pin >= 6 && pin <= 11) return false;
  if (pin == 1 || pin == 3) return false;
  return true;
}

}  // namespace pins
}  // namespace api
}  // namespace harixos
