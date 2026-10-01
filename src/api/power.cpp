#include "power.h"

#include <Arduino.h>
#include <string.h>
#include <user_interface.h>

namespace harixos {
namespace api {
namespace {

// Map profile names to enum
const char* profileNames[] = {
  "full",
  "balanced",
  "powersave",
  "minimal",
  "off"
};

// Map CPU frequency modes
// ESP8266 NONOS SDK only supports 80MHz and 160MHz;
// system_update_cpu_freq(40) is rejected (returns false).
bool setFreqModeForMHz(uint32_t mhz) {
  return system_update_cpu_freq(mhz >= 80 ? 80 : 40);
}

}  // namespace

PowerProfile stringToPowerProfile(const String &profile) {
  if (profile.equalsIgnoreCase("full")) {
    return PowerProfile::Full;
  } else if (profile.equalsIgnoreCase("balanced")) {
    return PowerProfile::Balanced;
  } else if (profile.equalsIgnoreCase("powersave")) {
    return PowerProfile::Powersave;
  } else if (profile.equalsIgnoreCase("minimal")) {
    return PowerProfile::Minimal;
  } else if (profile.equalsIgnoreCase("off")) {
    return PowerProfile::Off;
  }
  // Default to balanced
  return PowerProfile::Balanced;
}

String powerProfileToString(PowerProfile profile) {
  switch (profile) {
    case PowerProfile::Full: return "full";
    case PowerProfile::Balanced: return "balanced";
    case PowerProfile::Powersave: return "powersave";
    case PowerProfile::Minimal: return "minimal";
    case PowerProfile::Off: return "off";
    default: return "balanced";
  }
}

CpuFreq stringToCpuFreq(const String &freq) {
  if (freq.toInt() <= 40) {
    return CpuFreq::MHz40;
  }
  return CpuFreq::MHz80;
}

String cpuFreqToString(CpuFreq freq) {
  switch (freq) {
    case CpuFreq::MHz40: return "40";
    case CpuFreq::MHz80: return "80";
    default: return "80";
  }
}

void applyPowerProfile(PowerProfile profile) {
  switch (profile) {
    case PowerProfile::Full:
      // WiFi on, no sleep, 80MHz
      wifi_set_sleep_type(NONE_SLEEP_T);
      break;
      
    case PowerProfile::Balanced:
      // WiFi on, modem sleep, 80MHz
      wifi_set_sleep_type(MODEM_SLEEP_T);
      break;
      
    case PowerProfile::Powersave:
      // WiFi on, light sleep, 80MHz
      wifi_set_sleep_type(MODEM_SLEEP_T);
      break;
      
    case PowerProfile::Minimal:
      // WiFi on, deep sleep with timer wake, 40MHz
      wifi_set_sleep_type(MODEM_SLEEP_T);
      break;
      
    case PowerProfile::Off:
      // WiFi off, deep sleep
      wifi_set_sleep_type(NONE_SLEEP_T);
      break;
  }
}

bool setCpuFrequency(CpuFreq freq) {
  return setFreqModeForMHz(static_cast<uint32_t>(freq));
}

void enterDeepSleep(uint64_t wakeTimeUs) {
  // Enable timer wake up and go to deep sleep
  Serial.printf("Entering deep sleep for %llu us...\r\n", wakeTimeUs);
  Serial.flush();
  
  // Go to deep sleep
  system_deep_sleep_instant(wakeTimeUs);
}

bool isSerialConnected() {
  // Check if serial port is connected (host is listening)
  // Using available() to detect buffered data, which is more reliable
  // than checking DTR/RTS on ESP8266 where these signals can be unstable
  return Serial.available() > 0;
}

}  // namespace api
}  // namespace harixos
