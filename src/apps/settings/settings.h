#pragma once

#include <Arduino.h>

namespace harixos {

struct AppSettings {
  bool bannerEnabled = true;
  String timezone = "UTC";
  String wifiSSID = "";
  String wifiPassword = "";
  bool autoUpdateCheck = true;
  String powerProfile = "balanced";
  int cpufreq = 80;
};

AppSettings loadSettings();
bool saveSettings(const AppSettings &settings);
void printSettings(const AppSettings &settings, Print &out);

extern AppSettings shellSettings;

}  // namespace harixos
