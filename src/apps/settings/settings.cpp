#include "settings.h"

#include "../../kernel/filesystem/filesystem.h"

namespace harixos {
namespace {

const char *kSettingsPath = "/harixos/settings.cfg";

String resolveMqttPrefix(const String &prefix) {
  if (prefix.length() > 0) {
    return prefix;
  }
  return String("harixos/") + String(ESP.getChipId(), HEX);
}

String resolveHostname(const String &hostname) {
  if (hostname.length() > 0) {
    return hostname;
  }
  return String("harixos-") + String(ESP.getChipId(), HEX);
}

}  // namespace

AppSettings shellSettings;

AppSettings loadSettings() {
  AppSettings settings;
  if (!exists(kSettingsPath)) {
    settings.mqttPrefix = resolveMqttPrefix(settings.mqttPrefix);
    settings.hostname = resolveHostname(settings.hostname);
    return settings;
  }

  Serial.println(F("Loading system settings..."));
  String content = readText(kSettingsPath);
  int bannerPos = content.indexOf("banner=");
  if (bannerPos >= 0) {
    int endLine = content.indexOf('\n', bannerPos);
    String bannerVal = endLine == -1 ? content.substring(bannerPos + 7) : content.substring(bannerPos + 7, endLine);
    bannerVal.trim();
    if (bannerVal.equalsIgnoreCase("off")) {
      settings.bannerEnabled = false;
    }
  }

  int tzPos = content.indexOf("timezone=");
  if (tzPos >= 0) {
    int endLine = content.indexOf('\n', tzPos);
    String tzVal = endLine == -1 ? content.substring(tzPos + 9) : content.substring(tzPos + 9, endLine);
    tzVal.trim();
    if (tzVal.length() > 0) {
      settings.timezone = tzVal;
    }
  }

  int ssidPos = content.indexOf("wifiSSID=");
  if (ssidPos >= 0) {
    int endLine = content.indexOf('\n', ssidPos);
    settings.wifiSSID = endLine == -1 ? content.substring(ssidPos + 9) : content.substring(ssidPos + 9, endLine);
    settings.wifiSSID.trim();
  }

  int passPos = content.indexOf("wifiPassword=");
  if (passPos >= 0) {
    int endLine = content.indexOf('\n', passPos);
    settings.wifiPassword = endLine == -1 ? content.substring(passPos + 13) : content.substring(passPos + 13, endLine);
    settings.wifiPassword.trim();
  }
  
  int autoUpPos = content.indexOf("auto_update=");
  if (autoUpPos >= 0) {
    int endLine = content.indexOf('\n', autoUpPos);
    String autoUpVal = endLine == -1 ? content.substring(autoUpPos + 12) : content.substring(autoUpPos + 12, endLine);
    autoUpVal.trim();
    if (autoUpVal.equalsIgnoreCase("on")) {
      settings.autoUpdateCheck = true;
    } else if (autoUpVal.equalsIgnoreCase("off")) {
      settings.autoUpdateCheck = false;
    }
  }

  int ppPos = content.indexOf("powerprofile=");
  if (ppPos >= 0) {
    int endLine = content.indexOf('\n', ppPos);
    String ppVal = endLine == -1 ? content.substring(ppPos + 13) : content.substring(ppPos + 13, endLine);
    ppVal.trim();
    if (ppVal.length() > 0) {
      settings.powerProfile = ppVal;
    }
  }

  int cfPos = content.indexOf("cpufreq=");
  if (cfPos >= 0) {
    int endLine = content.indexOf('\n', cfPos);
    String cfVal = endLine == -1 ? content.substring(cfPos + 8) : content.substring(cfPos + 8, endLine);
    cfVal.trim();
    if (cfVal.length() > 0) {
      settings.cpufreq = cfVal.toInt();
    }
  }

  int mePos = content.indexOf("mqtt_enabled=");
  if (mePos >= 0) {
    int endLine = content.indexOf('\n', mePos);
    String meVal = endLine == -1 ? content.substring(mePos + 13) : content.substring(mePos + 13, endLine);
    meVal.trim();
    if (meVal.equalsIgnoreCase("on")) {
      settings.mqttEnabled = true;
    } else if (meVal.equalsIgnoreCase("off")) {
      settings.mqttEnabled = false;
    }
  }

  int mhPos = content.indexOf("mqtt_host=");
  if (mhPos >= 0) {
    int endLine = content.indexOf('\n', mhPos);
    settings.mqttHost = endLine == -1 ? content.substring(mhPos + 10) : content.substring(mhPos + 10, endLine);
    settings.mqttHost.trim();
  }

  int mpoPos = content.indexOf("mqtt_port=");
  if (mpoPos >= 0) {
    int endLine = content.indexOf('\n', mpoPos);
    String mpoVal = endLine == -1 ? content.substring(mpoPos + 10) : content.substring(mpoPos + 10, endLine);
    mpoVal.trim();
    long port = mpoVal.toInt();
    if (port > 0) {
      settings.mqttPort = (uint16_t)port;
    }
  }

  int muPos = content.indexOf("mqtt_user=");
  if (muPos >= 0) {
    int endLine = content.indexOf('\n', muPos);
    settings.mqttUser = endLine == -1 ? content.substring(muPos + 10) : content.substring(muPos + 10, endLine);
    settings.mqttUser.trim();
  }

  int mpasPos = content.indexOf("mqtt_pass=");
  if (mpasPos >= 0) {
    int endLine = content.indexOf('\n', mpasPos);
    settings.mqttPass = endLine == -1 ? content.substring(mpasPos + 10) : content.substring(mpasPos + 10, endLine);
    settings.mqttPass.trim();
  }

  int mprePos = content.indexOf("mqtt_prefix=");
  if (mprePos >= 0) {
    int endLine = content.indexOf('\n', mprePos);
    settings.mqttPrefix = endLine == -1 ? content.substring(mprePos + 12) : content.substring(mprePos + 12, endLine);
    settings.mqttPrefix.trim();
  }

  int miPos = content.indexOf("mqtt_interval=");
  if (miPos >= 0) {
    int endLine = content.indexOf('\n', miPos);
    String miVal = endLine == -1 ? content.substring(miPos + 14) : content.substring(miPos + 14, endLine);
    miVal.trim();
    long interval = miVal.toInt();
    if (interval >= 0) {
      settings.mqttInterval = (uint32_t)interval;
    }
  }

  int mdPos = content.indexOf("mqtt_discover=");
  if (mdPos >= 0) {
    int endLine = content.indexOf('\n', mdPos);
    String mdVal = endLine == -1 ? content.substring(mdPos + 14) : content.substring(mdPos + 14, endLine);
    mdVal.trim();
    if (mdVal.equalsIgnoreCase("on")) {
      settings.mqttDiscover = true;
    } else if (mdVal.equalsIgnoreCase("off")) {
      settings.mqttDiscover = false;
    }
  }

  int hnPos = content.indexOf("hostname=");
  if (hnPos >= 0) {
    int endLine = content.indexOf('\n', hnPos);
    String hnVal = endLine == -1 ? content.substring(hnPos + 9) : content.substring(hnPos + 9, endLine);
    hnVal.trim();
    if (hnVal.length() > 0) {
      settings.hostname = hnVal;
    }
  }

  settings.mqttPrefix = resolveMqttPrefix(settings.mqttPrefix);
  settings.hostname = resolveHostname(settings.hostname);

  return settings;
}

bool saveSettings(const AppSettings &settings) {
  String content = String("banner=") + (settings.bannerEnabled ? "on" : "off") + "\n";
  content += String("timezone=") + settings.timezone + "\n";
  content += String("wifiSSID=") + settings.wifiSSID + "\n";
  content += String("wifiPassword=") + settings.wifiPassword + "\n";
  content += String("auto_update=") + (settings.autoUpdateCheck ? "on" : "off") + "\n";
  content += String("powerprofile=") + settings.powerProfile + "\n";
  content += String("cpufreq=") + String(settings.cpufreq) + "\n";
  content += String("mqtt_enabled=") + (settings.mqttEnabled ? "on" : "off") + "\n";
  content += String("mqtt_host=") + settings.mqttHost + "\n";
  content += String("mqtt_port=") + String(settings.mqttPort) + "\n";
  content += String("mqtt_user=") + settings.mqttUser + "\n";
  content += String("mqtt_pass=") + settings.mqttPass + "\n";
  content += String("mqtt_prefix=") + settings.mqttPrefix + "\n";
  content += String("mqtt_interval=") + String(settings.mqttInterval) + "\n";
  content += String("mqtt_discover=") + (settings.mqttDiscover ? "on" : "off") + "\n";
  content += String("hostname=") + settings.hostname + "\n";
  bool ok = writeText(kSettingsPath, content, false);
  if (ok) {
    Serial.println(F("System settings saved successfully."));
  }
  return ok;
}

void printSettings(const AppSettings &settings, Print &out) {
  out.println(F("Settings"));
  out.printf("  Boot banner: %s\n", settings.bannerEnabled ? "ON" : "OFF");
  out.printf("  Timezone: %s\r\n", settings.timezone.c_str());
  out.printf("  WiFi SSID: %s\r\n", settings.wifiSSID.length() > 0 ? settings.wifiSSID.c_str() : "(not set)");
  out.printf("  Auto Update Check: %s\n", settings.autoUpdateCheck ? "ON" : "OFF");
  out.printf("  Power Profile: %s\r\n", settings.powerProfile.c_str());
  out.printf("  CPU Freq: %d MHz\r\n", settings.cpufreq);
  out.printf("  MQTT: %s  %s:%u  prefix=%s  interval=%us  discover=%s\r\n",
             settings.mqttEnabled ? "on" : "off",
             settings.mqttHost.length() > 0 ? settings.mqttHost.c_str() : "(not set)",
             (unsigned)settings.mqttPort,
             settings.mqttPrefix.c_str(),
             (unsigned)settings.mqttInterval,
             settings.mqttDiscover ? "on" : "off");
  out.printf("  Hostname: %s\r\n", settings.hostname.c_str());
  out.printf("  Config file: %s\n", kSettingsPath);
}

}  // namespace harixos
