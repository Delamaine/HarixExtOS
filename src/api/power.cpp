#include "power.h"

#include <Arduino.h>
#include <string.h>
#include <user_interface.h>

#include "../apps/settings/settings.h"

namespace harixos {
namespace api {
namespace {

// Map profile names to enum
const char* profileNames[] = {
  "full",
  "balanced",
  "powersave",
  "light",
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
  } else if (profile.equalsIgnoreCase("light")) {
    return PowerProfile::Light;
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
    case PowerProfile::Light: return "light";
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
      // Modem sleep (same as Balanced; CPU freq set separately via cpufreq cmd)
      wifi_set_sleep_type(MODEM_SLEEP_T);
      break;

    case PowerProfile::Light:
      // CPU pauses between DTIM beacons; WiFi stays associated and MQTT
      // keeps working (incoming frames wake the chip). Do NOT use while
      // holding servos — pulses glitch while the CPU is asleep.
      wifi_set_sleep_type(LIGHT_SLEEP_T);
      break;
      
    case PowerProfile::Minimal:
      // Modem sleep (CPU freq lowering to 40MHz is done by the caller)
      wifi_set_sleep_type(MODEM_SLEEP_T);
      break;
      
    case PowerProfile::Off:
      // No modem sleep (WiFi state unchanged here)
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

ApiResult handlePowerCommand(const String &args, Stream &output) {
  String rest = args;
  rest.trim();
  int sep = rest.indexOf(' ');
  String action = sep < 0 ? rest : rest.substring(0, sep);
  String tail = sep < 0 ? String("") : rest.substring(sep + 1);
  tail.trim();
  action.toLowerCase();

  if (action.length() == 0) {
    return ApiResult(API_OK, "Power profile: " + shellSettings.powerProfile);
  }

  if (action == "status") {
    output.printf("Power profile: %s\r\n", shellSettings.powerProfile.c_str());
    output.printf("CPU Freq: %d MHz\r\n", shellSettings.cpufreq);
    return ApiResult(API_OK, "");
  }

  if (action == "apply") {
    applyPowerProfile(stringToPowerProfile(shellSettings.powerProfile));
    setCpuFrequency(stringToCpuFreq(String(shellSettings.cpufreq)));
    return ApiResult(API_OK,
                     "Power profile applied (" + shellSettings.powerProfile + ").");
  }

  String desired = (action == "set") ? tail : action;
  if (desired.length() == 0) {
    return ApiResult(API_INVALID_ARGUMENT,
                     "Usage: powerprofile <full|balanced|powersave|light|minimal|off> | set <profile> | apply | status");
  }

  PowerProfile profile = stringToPowerProfile(desired);
  shellSettings.powerProfile = powerProfileToString(profile);
  applyPowerProfile(profile);
  setCpuFrequency(stringToCpuFreq(String(shellSettings.cpufreq)));
  saveSettings(shellSettings);
  return ApiResult(API_OK, "Power profile set to " + shellSettings.powerProfile + ".");
}

}  // namespace api
}  // namespace harixos
