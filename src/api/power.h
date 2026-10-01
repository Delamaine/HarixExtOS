#pragma once

#include <Arduino.h>

namespace harixos {
namespace api {

enum class PowerProfile {
  Full,
  Balanced,
  Powersave,
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
bool isSerialConnected();

}  // namespace api
}  // namespace harixos
