#pragma once

#include <Arduino.h>

#include "api_types.h"

namespace harixos {
namespace api {

// SSD1306 128x64 OLED over I2C (text-only, default address 0x3C).
class OledAPI {
public:
  // Shared `oled <action> ...` parser used by the shell and the script
  // engine. Informational output goes to `out`; failures come back as
  // ApiResult so scripts report [ERROR] instead of silently succeeding.
  static ApiResult runCommand(const String& args, Stream& out);
};

}  // namespace api
}  // namespace harixos
