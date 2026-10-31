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

// Parses a pin token: "4", "GPIO4", "gpio4". Rejects anything else.
// String.toInt() maps garbage to 0, which silently targeted boot pin GPIO0
// ("gpio mode GPIO4 out" configured GPIO0) - hence the strict parse here.
inline bool parse(const char *s, uint8_t *out) {
  if (!s || !*s) return false;
  if ((s[0] == 'G' || s[0] == 'g') && (s[1] == 'P' || s[1] == 'p') &&
      (s[2] == 'I' || s[2] == 'i') && (s[3] == 'O' || s[3] == 'o')) {
    s += 4;
    if (!*s) return false;
  }
  if (*s < '0' || *s > '9') return false;
  unsigned v = 0;
  for (const char *p = s; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10 + (unsigned)(*p - '0');
    if (v > 255) return false;
  }
  *out = (uint8_t)v;
  return true;
}

}  // namespace pins
}  // namespace api
}  // namespace harixos
