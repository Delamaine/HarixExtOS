#pragma once

#include <Arduino.h>

#include "api_types.h"

namespace harixos {
namespace api {

enum class PowerProfile {
  Full,
  Balanced,
  Powersave,
  Light,
  Minimal,
  Off
};

enum class CpuFreq {
  MHz40 = 40,
  MHz80 = 80
};

PowerProfile stringToPowerProfile(const String &profile);
String powerProfileToString(PowerProfile profile);

CpuFreq stringToCpuFreq(const String &freq);
String cpuFreqToString(CpuFreq freq);

void applyPowerProfile(PowerProfile profile);
bool setCpuFrequency(CpuFreq freq);
void enterDeepSleep(uint64_t wakeTimeUs);

// Shared powerprofile handler — the shell delegates here too (main.cpp
// handlePowerProfile), and scripts can call it directly.
ApiResult handlePowerCommand(const String &args, Stream &output);

}  // namespace api
}  // namespace harixos
