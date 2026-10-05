#pragma once

#include <Arduino.h>

namespace harixos {

struct AppSettings {
  bool bannerEnabled = true;
  String timezone = "UTC";
  String wifiSSID = "";
  String wifiPassword = "";
  bool autoUpdateCheck = true;
   String powerProfile = "full";
  int cpufreq = 80;
  bool mqttEnabled = false;
  String mqttHost = "";
  uint16_t mqttPort = 1883;
  String mqttUser = "";
  String mqttPass = "";
  String mqttPrefix = "";      // empty -> resolved to "harixos/<chipid>" at load
  uint32_t mqttInterval = 60;  // seconds; 0 = off
  bool mqttDiscover = true;
};

AppSettings loadSettings();
bool saveSettings(const AppSettings &settings);
void printSettings(const AppSettings &settings, Print &out);

extern AppSettings shellSettings;

}  // namespace harixos
