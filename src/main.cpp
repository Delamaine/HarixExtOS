#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Wire.h>
#include <FS.h>
#include <LittleFS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

#define HARIXOS_VERSION_MAJOR 1
#define HARIXOS_VERSION_MINOR 0
#define HARIXOS_VERSION_PATCH 0

#define HARIXOS_VERSION_STRING "1.0.0"
#define HARIXOS_BUILD_DATE __DATE__
#define HARIXOS_BUILD_TIME __TIME__

// Version display
#define HARIXOS_NAME "HarixOS"
#define HARIXOS_DESCRIPTION "An Open-Source Operating System For ESP Microcontrollers"
#define HARIXOS_AUTHOR "Haris"
#include "apps/notepad/notepad.h"
#include "apps/settings/settings.h"
#include "kernel/filesystem/filesystem.h"
#include "kernel/iot/mqtt_service.h"
#include "kernel/iot/onchange.h"
#include "kernel/iot/relay.h"
#include "utils/http/http_downloader.h"
#include "api/app_manager.h"
#include "api/expr.h"
#include "api/vars.h"
#include "api/value_tokens.h"
#include "api/tz_mapping.h"
#include "api/script_engine.h"
#include "kernel/scheduler/scheduler.h"
#include "kernel/cpu_handler/cpu_handler.h"
#include "api/power.h"
#include "api/sensor.h"
#include "api/servo.h"
#include "api/motor.h"

// shellSettings now lives in the settings module so wifi_api.cpp can reach it;
// main.cpp uses it unqualified ~29 times, so import just that one name rather
// than the whole namespace.
using harixos::shellSettings;
// currentWorkingDirectory moved to the filesystem module so the script engine
// resolves relative paths against the same value; same single-name import.
using harixos::currentWorkingDirectory;

namespace {

constexpr size_t kMaxLineLength = 160;
constexpr size_t kMaxTokens = 12;
constexpr size_t kMaxAppInstallBytes = 4096;
constexpr uint8_t kDefaultApChannel = 1;

String inputLine;
bool promptVisible = false;

// Simple HTTP file server
ESP8266WebServer *httpServer = nullptr;
String httpServeFile;
String httpServeDir;
uint16_t httpServePort = 80;
bool httpServerRunning = false;

struct TokenizedLine {
  String tokens[kMaxTokens];
  size_t count = 0;
};

String toLowerCopy(String value) {
  value.toLowerCase();
  return value;
}

String trimCopy(String value) {
  value.trim();
  return value;
}

String bytesToHuman(uint32_t bytes) {
  const char *suffixes[] = {"B", "KB", "MB"};
  float value = static_cast<float>(bytes);
  uint8_t suffix = 0;
  while (value >= 1024.0f && suffix < 2) {
    value /= 1024.0f;
    ++suffix;
  }

  String text = String(value, value < 10.0f && suffix > 0 ? 2 : 0);
  text += ' ';
  text += suffixes[suffix];
  return text;
}

String uptimeToHuman(unsigned long ms) {
  unsigned long seconds = ms / 1000UL;
  unsigned long days = seconds / 86400UL;
  seconds %= 86400UL;
  unsigned long hours = seconds / 3600UL;
  seconds %= 3600UL;
  unsigned long minutes = seconds / 60UL;
  seconds %= 60UL;

  String out;
  if (days > 0) {
    out += String(days);
    out += "d ";
  }
  if (days > 0 || hours > 0) {
    out += String(hours);
    out += "h ";
  }
  if (days > 0 || hours > 0 || minutes > 0) {
    out += String(minutes);
    out += "m ";
  }
  out += String(seconds);
  out += 's';
  return out;
}

String flashModeToString(FlashMode_t mode) {
  switch (mode) {
    case FM_QIO: return F("QIO");
    case FM_QOUT: return F("QOUT");
    case FM_DIO: return F("DIO");
    case FM_DOUT: return F("DOUT");
    case FM_UNKNOWN:
    default: return F("UNKNOWN");
  }
}

String wifiModeToString(WiFiMode_t mode) {
  switch (mode) {
    case WIFI_OFF: return F("OFF");
    case WIFI_STA: return F("STA");
    case WIFI_AP: return F("AP");
    case WIFI_AP_STA: return F("AP+STA");
    default: return F("UNKNOWN");
  }
}

String wifiStatusToString(wl_status_t status) {
  switch (status) {
    case WL_NO_SHIELD: return F("NO_SHIELD");
    case WL_IDLE_STATUS: return F("IDLE");
    case WL_NO_SSID_AVAIL: return F("NO_SSID");
    case WL_SCAN_COMPLETED: return F("SCAN_DONE");
    case WL_CONNECTED: return F("CONNECTED");
    case WL_CONNECT_FAILED: return F("CONNECT_FAILED");
    case WL_CONNECTION_LOST: return F("CONNECTION_LOST");
    case WL_DISCONNECTED: return F("DISCONNECTED");
    default: return F("UNKNOWN");
  }
}

String resetReasonToString() {
  return ESP.getResetReason();
}

bool isAvailableGpio(uint8_t pin) {
  // ESP8266 GPIO6-11 are connected to flash; GPIO1/3 are UART TX/RX.
  if (pin > 16) {
    return false;
  }
  if (pin >= 6 && pin <= 11) {
    return false;
  }
  if (pin == 1 || pin == 3) {
    return false;
  }
  return true;
}

bool isBootStrapPin(uint8_t pin) {
  return pin == 0 || pin == 2 || pin == 15;
}

bool isUnsafeBootLevel(uint8_t pin, int level) {
  // ESP8266 boot straps: GPIO0=HIGH, GPIO2=HIGH, GPIO15=LOW.
  if (pin == 0 || pin == 2) {
    return level == LOW;
  }
  if (pin == 15) {
    return level == HIGH;
  }
  return false;
}

void prepareBootStrapPinsForReset() {
  // Do nothing - let bootloader handle boot straps.
  // Any GPIO manipulation can trigger watchdog on ESP-01.
}

void printPrompt() {
  Serial.print(F("HarixOS> "));
  promptVisible = true;
}

TokenizedLine tokenize(const String &line) {
  TokenizedLine result;
  const size_t length = line.length();
  size_t index = 0;

  while (index < length && result.count < kMaxTokens) {
    while (index < length && isspace(static_cast<unsigned char>(line[index]))) {
      ++index;
    }
    if (index >= length) {
      break;
    }

    String token;
    char quoteChar = 0;
    if (line[index] == '"' || line[index] == '\'') {
      quoteChar = line[index++];
      while (index < length && line[index] != quoteChar) {
        token += line[index++];
      }
      if (index < length && line[index] == quoteChar) {
        ++index;
      }
    } else {
      while (index < length && !isspace(static_cast<unsigned char>(line[index]))) {
        token += line[index++];
      }
    }

    if (token.length() > 0) {
      result.tokens[result.count++] = token;
    }
  }

  return result;
}

bool parsePin(const String &value, uint8_t &pin) {
  if (value.length() == 0) {
    return false;
  }

  long parsed = value.toInt();
  if (parsed < 0 || parsed > 255) {
    return false;
  }

  pin = static_cast<uint8_t>(parsed);
  return true;
}

void printSystemInfo() {
  Serial.println(F("System"));
  Serial.printf("  Chip ID: 0x%08X\r\n", ESP.getChipId());
  Serial.printf("  Core version: %s\r\n", ESP.getCoreVersion().c_str());
  Serial.printf("  SDK version: %s\r\n", ESP.getSdkVersion());
  Serial.printf("  CPU frequency: %u MHz\r\n", ESP.getCpuFreqMHz());
  Serial.printf("  Reset reason: %s\r\n", resetReasonToString().c_str());
  Serial.printf("  Uptime: %s\r\n", uptimeToHuman(millis()).c_str());
  Serial.printf("  Heap free: %u bytes\r\n", ESP.getFreeHeap());
  Serial.printf("  Sketch used: %s\r\n", bytesToHuman(ESP.getSketchSize()).c_str());
  Serial.printf("  Free sketch space: %s\r\n", bytesToHuman(ESP.getFreeSketchSpace()).c_str());
  Serial.printf("  Flash size: %s\r\n", bytesToHuman(ESP.getFlashChipRealSize()).c_str());
  Serial.printf("  Flash mode: %s\r\n", flashModeToString(ESP.getFlashChipMode()).c_str());
  Serial.printf("  Flash speed: %lu MHz\r\n", ESP.getFlashChipSpeed() / 1000000UL);
}

void printWifiStatus() {
  Serial.println(F("WiFi"));
  Serial.printf("  Mode: %s\r\n", wifiModeToString(WiFi.getMode()).c_str());
  Serial.printf("  Status: %s\r\n", wifiStatusToString(WiFi.status()).c_str());
  Serial.printf("  Station SSID: %s\r\n", WiFi.SSID().c_str());
  Serial.printf("  Station IP: %s\r\n", WiFi.localIP().toString().c_str());
  Serial.printf("  Gateway: %s\r\n", WiFi.gatewayIP().toString().c_str());
  Serial.printf("  DNS: %s\r\n", WiFi.dnsIP().toString().c_str());
  Serial.printf("  Subnet: %s\r\n", WiFi.subnetMask().toString().c_str());
  Serial.printf("  RSSI: %d dBm\r\n", WiFi.isConnected() ? WiFi.RSSI() : 0);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  Serial.printf("  Station MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  if (WiFi.softAPmacAddress(mac)) {
    Serial.printf("  AP MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
  Serial.printf("  AP IP: %s\r\n", WiFi.softAPIP().toString().c_str());
}

void printGpioHelp() {
  Serial.println(F("GPIO"));
  Serial.println(F("  ESP8266 usable pins: GPIO0,2,4,5,12,13,14,15,16"));
  Serial.println(F("  Reserved: GPIO6-11 (flash), GPIO1/3 (serial RX/TX)"));
  Serial.println(F("  Boot pins: avoid forcing GPIO0/2/15 to invalid levels during reset"));
}

void printGpioPins() {
  Serial.println(F("GPIO"));
  Serial.printf("  Chip ID: 0x%08X (ESP8266 family)\r\n", ESP.getChipId());

  // ESP-01 has only GPIO0 and GPIO2 exposed.
  // Other modules have more pins available.
  uint32_t flashSize = ESP.getFlashChipRealSize();
  bool isEsp01 = (flashSize == 1048576); // 1MB = ESP-01

  if (isEsp01) {
    Serial.println(F("  Board: ESP-01 (1MB flash)"));
    Serial.println(F("  Exposed pins: GPIO0, GPIO2"));
    Serial.println(F("  Reserved: GPIO1/3 (serial RX/TX), GPIO6-11 (flash), others not exposed"));
    Serial.println(F("  Boot straps: GPIO0=HIGH, GPIO2=HIGH (critical for normal boot)"));
    Serial.println(F("  Pin levels:"));
    const uint8_t esp01_pins[] = {0, 2};
    for (uint8_t i = 0; i < sizeof(esp01_pins); ++i) {
      uint8_t pin = esp01_pins[i];
      pinMode(pin, INPUT);
      int level = digitalRead(pin);
      Serial.printf("    GPIO%u = %d [boot: must be HIGH]\r\n", pin, level);
    }
  } else {
    Serial.println(F("  Board: ESP8266 standard (4MB flash typical)"));
    Serial.println(F("  Usable pins: GPIO0, GPIO2, GPIO4, GPIO5, GPIO12, GPIO13, GPIO14, GPIO15, GPIO16"));
    Serial.println(F("  Reserved: GPIO6-11 (flash), GPIO1/3 (serial RX/TX)"));
    Serial.println(F("  Boot straps: GPIO0=HIGH, GPIO2=HIGH, GPIO15=LOW (critical for normal boot)"));
    Serial.println(F("  Pin levels:"));
    const uint8_t std_pins[] = {0, 2, 4, 5, 12, 13, 14, 15, 16};
    for (uint8_t i = 0; i < sizeof(std_pins); ++i) {
      uint8_t pin = std_pins[i];
      pinMode(pin, INPUT);
      int level = digitalRead(pin);
      const char *bootstrap = "";
      if (pin == 0 || pin == 2) bootstrap = " [boot: must be HIGH]";
      else if (pin == 15) bootstrap = " [boot: must be LOW]";
      Serial.printf("    GPIO%u = %d%s\r\n", pin, level, bootstrap);
    }
  }
}

void handleHelp(const TokenizedLine &cmd);
void handleWifi(const TokenizedLine &cmd);
void handleGpio(const TokenizedLine &cmd);
void handleI2c(const TokenizedLine &cmd);
void handleFs(const TokenizedLine &cmd);
void handlePwd();
void handleCd(const TokenizedLine &cmd);
void handleLs(const TokenizedLine &cmd);
void handleMkdir(const TokenizedLine &cmd);
void handleTouch(const TokenizedLine &cmd);
void handleRm(const TokenizedLine &cmd);
void handleCp(const TokenizedLine &cmd);
void handleMv(const TokenizedLine &cmd);
void handleCat(const TokenizedLine &cmd);
void handleWrite(const TokenizedLine &cmd);
void handleAppend(const TokenizedLine &cmd);
void handleNotepad(const TokenizedLine &cmd);
void handlePost(const TokenizedLine &cmd);
void handleMqtt(const String &line);
  void handleOnchange(const String &line);
  void handleRelay(const String &line);
void handleSettings(const TokenizedLine &cmd);
void handleServe(const TokenizedLine &cmd);
void handleHttpStop();
void handlePull(const TokenizedLine &cmd);
void handleUpdate(const TokenizedLine &cmd);
void tryAutoWifi();
bool readLineBlocking(String &line);

void handleInfo() {
  printSystemInfo();
  printWifiStatus();
  printGpioHelp();
}

void handleAboutInfo() {
  Serial.println();
  Serial.println(F("========================================"));
  Serial.printf(" %s v%s\r\n", HARIXOS_NAME, HARIXOS_VERSION_STRING);
  Serial.println(F("========================================"));
  Serial.println();
  Serial.printf(" %s\r\n", HARIXOS_DESCRIPTION);
  Serial.println();
  Serial.println(F("  More Features Are In Development! So Stay Tuned :) Also Give A Star On GitHub!"));
  Serial.println();
  Serial.println(F("Documentation:"));
  Serial.println(F("  Type 'help' for command reference"));
  Serial.println(F("  Type 'run list' to see installed apps"));
  Serial.println();
  Serial.println(F("License:"));
  Serial.println(F("  Licensed under GNU General Public License v3.0"));
  Serial.println(F("  Copyright (C) 2026 Haris"));
  Serial.println(F("  Full License: https://github.com/Haris16-code/HarixOS/blob/main/LICENSE"));
  Serial.println();
  Serial.printf(" Built: %s %s\r\n", HARIXOS_BUILD_DATE, HARIXOS_BUILD_TIME);
  Serial.println(F("========================================"));
  Serial.println();
}

void handleReset() {
  Serial.println(F("Rebooting..."));
  Serial.flush();
  delay(100);
  prepareBootStrapPinsForReset();
  ESP.restart();
}

void handleUptime() {
  Serial.printf("Uptime: %s\r\n", uptimeToHuman(millis()).c_str());
}

void handleHeap() {
  Serial.printf("Heap free: %u bytes\r\n", ESP.getFreeHeap());
  Serial.println(F("Heap fragmentation is not exposed directly by the core."));
}

void handleChip() {
  Serial.printf("Chip ID: 0x%08X\r\n", ESP.getChipId());
  Serial.printf("CPU frequency: %u MHz\r\n", ESP.getCpuFreqMHz());
  Serial.printf("Reset reason: %s\r\n", resetReasonToString().c_str());
  Serial.printf("Flash size: %s\r\n", bytesToHuman(ESP.getFlashChipRealSize()).c_str());
  Serial.printf("Flash mode: %s\r\n", flashModeToString(ESP.getFlashChipMode()).c_str());
}

void handleAdc() {
  int raw = analogRead(A0);
  Serial.printf("ADC A0: %d / 1023\r\n", raw);
  Serial.println(F("Note: A0 scaling depends on the ESP8266 board design."));
}

// --- Filesystem helpers (cwd-aware LittleFS shell) ---
void handlePwd() {
  Serial.println(currentWorkingDirectory);
}

void handleCd(const TokenizedLine &cmd) {
  if (cmd.count < 2) {
    handlePwd();
    return;
  }

  String target = harixos::resolvePath(currentWorkingDirectory, cmd.tokens[1]);
  if (!harixos::exists(target)) {
    Serial.println(F("cd: no such file or directory"));
    return;
  }
  if (!harixos::isDirectory(target)) {
    Serial.println(F("cd: not a directory"));
    return;
  }

  currentWorkingDirectory = target;
}

void handleLs(const TokenizedLine &cmd) {
  bool recursive = false;
  String path = currentWorkingDirectory;

  if (cmd.count >= 2) {
    if (cmd.tokens[1] == F("-R") || cmd.tokens[1] == F("-r")) {
      recursive = true;
      if (cmd.count >= 3) {
        path = harixos::resolvePath(currentWorkingDirectory, cmd.tokens[2]);
      }
    } else {
      path = harixos::resolvePath(currentWorkingDirectory, cmd.tokens[1]);
      if (cmd.count >= 3 && (cmd.tokens[2] == F("-R") || cmd.tokens[2] == F("-r"))) {
        recursive = true;
      }
    }
  }

  harixos::listDirectory(path, Serial, recursive);
}

void handleMkdir(const TokenizedLine &cmd) {
  if (cmd.count < 2) {
    Serial.println(F("Usage: mkdir <path>"));
    return;
  }

  String path = harixos::resolvePath(currentWorkingDirectory, cmd.tokens[1]);
  if (harixos::makeDirectory(path)) {
    Serial.printf("Created directory: %s\n", path.c_str());
  } else {
    Serial.println(F("mkdir: failed to create directory"));
  }
}

void handleTouch(const TokenizedLine &cmd) {
  if (cmd.count < 2) {
    Serial.println(F("Usage: touch <path>"));
    return;
  }

  String path = harixos::resolvePath(currentWorkingDirectory, cmd.tokens[1]);
  if (harixos::touch(path)) {
    Serial.printf("Touched: %s\n", path.c_str());
  } else {
    Serial.println(F("touch: failed"));
  }
}

void handleRm(const TokenizedLine &cmd) {
  if (cmd.count < 2) {
    Serial.println(F("Usage: rm <path>"));
    return;
  }

  bool recursive = false;
  String pathArg;
  if (cmd.tokens[1] == F("-r") || cmd.tokens[1] == F("-R")) {
    recursive = true;
    if (cmd.count < 3) {
      Serial.println(F("Usage: rm -r <path>"));
      return;
    }
    pathArg = cmd.tokens[2];
  } else {
    pathArg = cmd.tokens[1];
  }

  String path = harixos::resolvePath(currentWorkingDirectory, pathArg);
  if (!harixos::exists(path)) {
    Serial.println(F("rm: path not found"));
    return;
  }

  if (harixos::removePath(path)) {
    Serial.printf("Removed: %s%s\n", path.c_str(), recursive ? " (recursive)" : "");
  } else {
    Serial.println(F("rm: failed"));
  }
}

void handleCp(const TokenizedLine &cmd) {
  if (cmd.count < 3) {
    Serial.println(F("Usage: cp <source> <destination>"));
    return;
  }

  String source = harixos::resolvePath(currentWorkingDirectory, cmd.tokens[1]);
  String destination = harixos::resolvePath(currentWorkingDirectory, cmd.tokens[2]);
  if (harixos::copyFile(source, destination)) {
    Serial.printf("Copied %s -> %s\n", source.c_str(), destination.c_str());
  } else {
    Serial.println(F("cp: failed"));
  }
}

void handleMv(const TokenizedLine &cmd) {
  if (cmd.count < 3) {
    Serial.println(F("Usage: mv <source> <destination>"));
    return;
  }

  String source = harixos::resolvePath(currentWorkingDirectory, cmd.tokens[1]);
  String destination = harixos::resolvePath(currentWorkingDirectory, cmd.tokens[2]);
  if (harixos::movePath(source, destination)) {
    Serial.printf("Moved %s -> %s\n", source.c_str(), destination.c_str());
  } else {
    Serial.println(F("mv: failed"));
  }
}

void handleCat(const TokenizedLine &cmd) {
  if (cmd.count < 2) {
    Serial.println(F("Usage: cat <path>"));
    return;
  }

  String path = harixos::resolvePath(currentWorkingDirectory, cmd.tokens[1]);
  String content = harixos::readText(path);
  if (content.length() == 0 && !harixos::exists(path)) {
    Serial.println(F("cat: file not found"));
    return;
  }

  Serial.print(content);
  if (!content.endsWith("\n")) {
    Serial.println();
  }
}

void handleWrite(const TokenizedLine &cmd) {
  if (cmd.count < 3) {
    Serial.println(F("Usage: write <path> \"content\""));
    return;
  }

  String path = harixos::resolvePath(currentWorkingDirectory, cmd.tokens[1]);
  String content = cmd.tokens[2];
  if (harixos::writeText(path, content, false)) {
    Serial.printf("Wrote %u bytes to %s\n", content.length(), path.c_str());
  } else {
    Serial.println(F("write: failed"));
  }
}

void handleAppend(const TokenizedLine &cmd) {
  if (cmd.count < 3) {
    Serial.println(F("Usage: append <path> \"content\""));
    return;
  }

  String path = harixos::resolvePath(currentWorkingDirectory, cmd.tokens[1]);
  String content = cmd.tokens[2];
  if (harixos::writeText(path, content, true)) {
    Serial.printf("Appended %u bytes to %s\n", content.length(), path.c_str());
  } else {
    Serial.println(F("append: failed"));
  }
}

void handleNotepad(const TokenizedLine &cmd) {
  if (cmd.count < 2) {
    Serial.println(F("Usage: notepad <path>"));
    return;
  }

  String path = harixos::resolvePath(currentWorkingDirectory, cmd.tokens[1]);
  harixos::runNotepad(path);
}

void handlePost(const TokenizedLine &cmd) {
  if (cmd.count < 3) {
    Serial.println(F("Usage: post <url> <body> [content-type]"));
    return;
  }

  String contentType =
      cmd.count >= 4 ? cmd.tokens[3] : String("application/json");
  harixos::api::ApiResult result =
      harixos::HttpDownloader::post(cmd.tokens[1], cmd.tokens[2], contentType);
  if (result.isError()) {
    Serial.print(F("ERROR: "));
  }
  Serial.println(result.message);
}

// Shared body lives in iot::handleCommand; the shell passes the raw tail so
// parsing is byte-identical with the script dispatcher's `mqtt <args>`.
void handleMqtt(const String &line) {
  harixos::api::ApiResult result =
      harixos::iot::handleCommand(line.substring(4), Serial);
  if (result.isError()) {
    Serial.print(F("ERROR: "));
  }
  Serial.println(result.message);
}

void handleOnchange(const String &line) {
  if (line.length() <= 8) {
    harixos::iot::listOnchangeRules(Serial);
    return;
  }
  String rest = line.substring(8);
  rest.trim();
  String action = rest.substring(0, rest.indexOf(' '));
  action.trim();
  String args = rest.substring(action.length());
  args.trim();
  action.toLowerCase();
  harixos::api::ApiResult result;
  if (action == F("add")) {
    result = harixos::iot::shellAddOnchange(args, Serial);
  } else if (action == F("remove") || action == F("rm")) {
    result = harixos::iot::shellRemoveOnchange(args, Serial);
  } else if (action == F("list") || action == F("pins")) {
    harixos::iot::listOnchangeRules(Serial);
    return;
  } else {
    Serial.println(F("Usage: onchange [add|remove|list] (see 'help onchange')"));
    return;
  }
  if (result.isError()) {
    Serial.print(F("ERROR: "));
  }
  Serial.println(result.message);
}

void handleRelay(const String &line) {
  // Strip the "relay " command word; runCommand expects bare args.
  String rest = (line.length() > 6) ? line.substring(6) : String();
  rest.trim();
  harixos::api::ApiResult result = harixos::iot::runCommand(rest, Serial);
  if (result.isError()) {
    Serial.print(F("ERROR: "));
  }
  Serial.println(result.message);
}

void handleSettings(const TokenizedLine &cmd) {
  if (cmd.count < 2) {
    harixos::printSettings(shellSettings, Serial);
    Serial.println(F("Commands: settings show | settings banner on|off | settings timezone <tz> | settings update on|off | settings save | settings reload"));
    return;
  }

  String action = toLowerCopy(cmd.tokens[1]);
  if (action == F("show")) {
    harixos::printSettings(shellSettings, Serial);
  } else if (action == F("banner")) {
    if (cmd.count < 3) {
      Serial.println(F("Usage: settings banner on|off"));
      return;
    }
    String value = toLowerCopy(cmd.tokens[2]);
    if (value == F("on")) {
      shellSettings.bannerEnabled = true;
    } else if (value == F("off")) {
      shellSettings.bannerEnabled = false;
    } else {
      Serial.println(F("Use on or off."));
      return;
    }
    if (harixos::saveSettings(shellSettings)) {
      Serial.println(F("Settings saved."));
    } else {
      Serial.println(F("Failed to save settings."));
    }
  } else if (action == F("update")) {
    if (cmd.count < 3) {
      Serial.println(F("Usage: settings update on|off"));
      return;
    }
    String value = toLowerCopy(cmd.tokens[2]);
    if (value == F("on")) {
      shellSettings.autoUpdateCheck = true;
    } else if (value == F("off")) {
      shellSettings.autoUpdateCheck = false;
    } else {
      Serial.println(F("Use on or off."));
      return;
    }
    if (harixos::saveSettings(shellSettings)) {
      Serial.println(F("Settings saved. Auto update check updated."));
    } else {
      Serial.println(F("Failed to save settings."));
    }
  } else if (action == F("timezone") || action == F("tz")) {
    if (cmd.count < 3) {
      Serial.println(F("Usage: settings timezone <region>"));
      Serial.println(F("Example: settings timezone Pacific/Auckland or UTC or PKT-5"));
      return;
    }
    String tzVal = cmd.tokens[2];
    shellSettings.timezone = tzVal;
    const char* posixStr = harixos::api::tz::toPosix(tzVal.c_str());
    setenv("TZ", posixStr, 1);
    tzset();
    if (harixos::saveSettings(shellSettings)) {
      Serial.printf("Settings saved. Timezone updated to: %s (%s)\r\n", tzVal.c_str(), posixStr);
    } else {
      Serial.println(F("Failed to save settings."));
    }
  } else if (action == F("save")) {
    if (harixos::saveSettings(shellSettings)) {
      Serial.println(F("Settings saved."));
    } else {
      Serial.println(F("Failed to save settings."));
    }
  } else if (action == F("reload")) {
    shellSettings = harixos::loadSettings();
    Serial.println(F("Settings reloaded."));
  } else {
    Serial.println(F("Unknown settings action."));
  }
}

void handleCalc(const String &line) {
  // Evaluate the raw line instead of cmd.tokens[1]: tokens are capped at
  // kMaxTokens, which silently truncated longer expressions, and the token
  // list also drops the spacing between operands.
  String expr = line;
  expr.trim();
  if (expr.length() >= 4 && expr.substring(0, 4).equalsIgnoreCase("calc")) {
    expr = expr.substring(4);
  }
  expr.trim();

  if (expr.length() == 0) {
    Serial.println(F("Usage: calc <expression>  e.g. calc 1+2*(3-4)/5"));
    return;
  }

  double res = 0;
  if (!harixos::api::expr::evaluate(expr.c_str(), res) || isnan(res)) {
    Serial.println(F("Invalid expression."));
    return;
  }
  Serial.printf("= %.10g\n", res);
}

void handleSet(const String &line) {
  // Strip the leading `set`, then hand everything else to expr::assign —
  // the same single implementation the script dispatcher uses.
  String args = line;
  args.trim();
  // Compare against "set " (4 chars) rather than "set": substring(0, 4)
  // yields four characters, and String::equalsIgnoreCase returns false on a
  // length mismatch, so a 3-char needle could never match.
  if (args.length() >= 4 && args.substring(0, 4).equalsIgnoreCase("set ")) {
    args = args.substring(4);
  }

  double value = 0;
  const char *err = harixos::api::expr::assign(args.c_str(), &value);
  if (err != nullptr) {
    Serial.println(err);
    return;
  }

  char name[harixos::api::vars::kMaxNameLength + 1];
  const char *expression = nullptr;
  if (harixos::api::expr::parseSet(args.c_str(), name, sizeof(name), &expression)) {
    Serial.printf("%s = %.10g\n", name, value);
  }
}

void handleWifiScan() {
  WiFi.mode(WIFI_STA);
  delay(50);

  Serial.println(F("Scanning..."));
  int networks = WiFi.scanNetworks(false, true);
  if (networks < 0) {
    Serial.println(F("Scan failed."));
    return;
  }

  if (networks == 0) {
    Serial.println(F("No networks found."));
    WiFi.scanDelete();
    return;
  }

  for (int index = 0; index < networks; ++index) {
    Serial.printf("%2d. %s\r\n", index + 1, WiFi.SSID(index).c_str());
    Serial.printf("    RSSI: %d dBm\r\n", WiFi.RSSI(index));
    Serial.printf("    CH: %d\r\n", WiFi.channel(index));
    Serial.printf("    Security: %s\r\n", WiFi.encryptionType(index) == ENC_TYPE_NONE ? "OPEN" : "SECURE");
  }
  WiFi.scanDelete();
}

void handleWifiConnect(const TokenizedLine &cmd) {
  if (cmd.count < 3) {
    Serial.println(F("Usage: wifi connect 'SSID' 'PASS'"));
    Serial.println(F("Example: wifi connect 'My WiFi' 'secret123'"));
    return;
  }

  String ssid = cmd.tokens[2];
  String password = cmd.count > 3 ? cmd.tokens[3] : String();

  Serial.print("Connecting to ");
  Serial.print(ssid);
  Serial.flush();
  delay(50);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000UL) {
    delay(200);
    Serial.print('.');
    if (httpServerRunning && httpServer) {
      httpServer->handleClient();
    }
    yield();
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setAutoReconnect(true); // Keep connection alive during this session
    Serial.println(F("Connected."));
    
    // Save credentials to settings
    shellSettings.wifiSSID = ssid;
    shellSettings.wifiPassword = password;
    harixos::saveSettings(shellSettings);
    Serial.println(F("WiFi credentials saved. Auto-connect enabled."));

    printWifiStatus();
    // Sync time via NTP now that we have internet. Use the TZ-aware overload
    // so newlib's DST rules stay intact (the int offset overload wipes them).
    configTime(harixos::api::tz::toPosix(shellSettings.timezone.c_str()), "pool.ntp.org", "time.nist.gov");
  } else {
    Serial.printf("Connection failed: %s\r\n", wifiStatusToString(WiFi.status()).c_str());
  }
}

// Blocking line read with frequent yields (timeout 15s, checks HTTP every
// iteration). Returns false on timeout, true once a line has been read.
bool readLineBlocking(String &line) {
  line = String();
  unsigned long start = millis();
  while (millis() - start < 15000UL) {
    while (Serial.available() > 0) {
      char c = static_cast<char>(Serial.read());
      if (c == '\r') continue;
      if (c == '\n') {
        return true;
      }
      if (isPrintable(static_cast<unsigned char>(c)) && line.length() < kMaxLineLength) {
        line += c;
      }
    }
    if (httpServerRunning && httpServer) {
      httpServer->handleClient();
    }
    delay(50);
    yield();
  }
  return false;
}

// Serve a requested LittleFS file and log requests
void handleHttpRequest() {
  if (!httpServer) return;
  String uri = httpServer->uri();
  String method = (httpServer->method() == HTTP_GET) ? "GET" : "OTHER";
  IPAddress remote = httpServer->client().remoteIP();
  Serial.printf("HTTP %s %s from %s\n", method.c_str(), uri.c_str(), remote.toString().c_str());

  String filePath;
  if (uri == "/") {
    filePath = httpServeFile;
  } else {
    String rel = uri;
    if (rel.startsWith("/")) rel = rel.substring(1);
    filePath = httpServeDir;
    if (!filePath.endsWith("/")) filePath += '/';
    filePath += rel;
  }

  filePath = harixos::normalizePath(filePath);
  if (!LittleFS.exists(filePath)) {
    httpServer->send(404, "text/plain", "Not found");
    return;
  }

  String contentType = "application/octet-stream";
  if (filePath.endsWith(".html") || filePath.endsWith(".htm")) contentType = "text/html";
  else if (filePath.endsWith(".css")) contentType = "text/css";
  else if (filePath.endsWith(".js")) contentType = "application/javascript";
  else if (filePath.endsWith(".png")) contentType = "image/png";
  else if (filePath.endsWith(".jpg") || filePath.endsWith(".jpeg")) contentType = "image/jpeg";
  else if (filePath.endsWith(".gif")) contentType = "image/gif";
  else if (filePath.endsWith(".txt")) contentType = "text/plain";

  File f = LittleFS.open(filePath, "r");
  if (!f) {
    httpServer->send(500, "text/plain", "Failed to open file");
    return;
  }
  httpServer->streamFile(f, contentType);
  f.close();
}

void handleServe(const TokenizedLine &cmd) {
  if (cmd.count < 2) {
    Serial.println(F("Usage: serve <file>|stop|status [port]"));
    return;
  }

  String action = toLowerCopy(cmd.tokens[1]);
  if (action == F("stop")) {
    handleHttpStop();
    return;
  }
  if (action == F("status")) {
    if (httpServerRunning && httpServer) {
      Serial.printf("HTTP serving %s on %s:%u\r\n", httpServeFile.c_str(), WiFi.localIP().toString().c_str(), httpServePort);
    } else {
      Serial.println(F("HTTP server not running."));
    }
    return;
  }

  String target = harixos::resolvePath(currentWorkingDirectory, cmd.tokens[1]);
  if (!harixos::exists(target)) {
    Serial.println(F("serve: file not found"));
    return;
  }
  if (harixos::isDirectory(target)) {
    Serial.println(F("serve: must be a file, not a directory"));
    return;
  }

  uint16_t port = 80;
  if (cmd.count >= 3) {
    port = static_cast<uint16_t>(cmd.tokens[2].toInt());
    if (port == 0) port = 80;
  }

  // Ensure WiFi connected; if not, prompt user for SSID/password
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("Not connected to WiFi. Enter SSID (blank to cancel):"));
    String ssid;
    if (!readLineBlocking(ssid)) {
      Serial.println(F("serve cancelled: timed out waiting for SSID."));
      return;
    }
    if (ssid.length() == 0) {
      Serial.println(F("serve cancelled."));
      return;
    }
    Serial.println(F("Enter password (leave blank for open networks):"));
    String pass;
    if (!readLineBlocking(pass)) {
      Serial.println(F("serve cancelled: timed out waiting for password."));
      return;
    }
    Serial.print("Connecting to ");
    Serial.print(ssid);
    Serial.flush();
    delay(50);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000UL) {
      delay(200);
      Serial.print('.');
      if (httpServer) {
        httpServer->handleClient();
      }
      yield();
    }
    Serial.println();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("WiFi connection failed."));
      return;
    }
    Serial.println(F("Connected."));
    shellSettings.wifiSSID = ssid;
    shellSettings.wifiPassword = pass;
    harixos::saveSettings(shellSettings);
    Serial.println(F("WiFi credentials saved. Auto-connect enabled."));
  }

  // Stop existing server if running
  if (httpServer) {
    httpServer->stop();
    delete httpServer;
    httpServer = nullptr;
    httpServerRunning = false;
  }

  httpServeFile = target;
  httpServeDir = harixos::parentPath(target);
  httpServePort = port;

  httpServer = new ESP8266WebServer(port);
  httpServer->onNotFound([]() { handleHttpRequest(); });
  httpServer->begin();
  httpServerRunning = true;

  Serial.printf("Serving %s on %s:%u\r\n", httpServeFile.c_str(), WiFi.localIP().toString().c_str(), httpServePort);
}

void handleHttpStop() {
  if (httpServer) {
    httpServer->stop();
    delete httpServer;
    httpServer = nullptr;
  }
  httpServerRunning = false;
  Serial.println(F("HTTP server stopped."));
}

void handleRun(const TokenizedLine &cmd) {
  if (cmd.count < 2) {
    Serial.println(F("Usage: run <app_name>|<path> | run list | run install <name> | run uninstall <name>"));
    return;
  }

  String action = toLowerCopy(cmd.tokens[1]);

  if (action == F("list")) {
    harixos::api::AppManager::listApps(Serial);
    return;
  }

  if (action == F("install")) {
    if (cmd.count < 3) {
      Serial.println(F("Usage: run install <name>"));
      return;
    }
    Serial.println(F("Enter app content (type 'END' on a new line to finish):"));
    String content;
    while (true) {
      String line;
      if (!readLineBlocking(line)) {
        Serial.println(F("Install aborted: timed out waiting for input."));
        return;
      }
      line.trim();
      if (line == "END") {
        break;
      }
      if (content.length() + line.length() + 1 > kMaxAppInstallBytes) {
        Serial.println(F("Install aborted: app content too large."));
        return;
      }
      content += line;
      content += '\n';
    }
    harixos::api::ApiResult result = harixos::api::AppManager::installApp(cmd.tokens[2], content);
    Serial.println(result.message);
    return;
  }

  if (action == F("uninstall")) {
    if (cmd.count < 3) {
      Serial.println(F("Usage: run uninstall <name>"));
      return;
    }
    harixos::api::ApiResult result = harixos::api::AppManager::uninstallApp(cmd.tokens[2]);
    Serial.println(result.message);
    return;
  }

  // Default: run app by name or path
  String appName = cmd.tokens[1];
  harixos::api::ApiResult result = harixos::api::AppManager::runApp(appName, Serial);
  if (result.isError()) {
    Serial.println(result.message);
  }
}

/* Duplicate about() removed; single canonical implementation exists earlier. */

void handlePull(const TokenizedLine &cmd) {
  if (cmd.count < 3) {
    Serial.println(F("Usage: pull <url> <save_path>"));
    Serial.println(F("Example: pull http://example.com/app.hx /data/app.hx"));
    Serial.println();
    Serial.println(F("Supported protocols: http, https"));
    return;
  }

  String url = cmd.tokens[1];
  String savePath = cmd.tokens[2];

  // Check WiFi connection
  if (!harixos::HttpDownloader::isWiFiConnected()) {
    Serial.println(F("Not connected to WiFi."));
    Serial.println(F("Connect using: wifi connect <ssid> <password>"));
    Serial.println();
    
    // Offer WiFi scan
    Serial.println(F("Available networks:"));
    WiFi.mode(WIFI_STA);
    delay(100);
    int networks = WiFi.scanNetworks(false, true);
    if (networks > 0) {
      for (int i = 0; i < networks && i < 10; ++i) {
        Serial.printf("  %d. %s (RSSI: %d)\r\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
      }
      WiFi.scanDelete();
    }
    return;
  }

  // Download file
  bool success = harixos::HttpDownloader::downloadFile(url, savePath, Serial);
  
  if (success) {
    Serial.println(F("File download complete!"));
  } else {
    Serial.println(F("File download failed."));
  }
}

// Helper to check for updates (can be used manually or during boot)
void checkSystemUpdates(bool silent) {
  if (WiFi.status() != WL_CONNECTED) {
    if (!silent) Serial.println(F("Error: Please connect to WiFi first."));
    return;
  }

  if (!silent) Serial.println(F("Checking for updates..."));
  
  const char* updateUrl = "https://raw.githubusercontent.com/Haris16-code/HarixOS/refs/heads/main/updates/esp8266/update.json";
  
  // Use a block to ensure memory is released as soon as possible
  {
    WiFiClientSecure client;
    client.setInsecure();
    // Reduce buffer sizes to save memory on ESP-01
    client.setBufferSizes(1024, 512); 
    
    HTTPClient http;
    http.setTimeout(10000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    // Add cache-busting timestamp to the URL to ensure we get a fresh copy from GitHub
    String freshUrl = String(updateUrl) + "?t=" + String(millis());
    
    if (http.begin(client, freshUrl)) {
      // Force no-cache headers
      http.addHeader("Cache-Control", "no-cache");
      http.addHeader("Pragma", "no-cache");
      
      int httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        http.end(); // Close connection early
        
        // Manual JSON Parsing (Version)
        int verPos = payload.indexOf("\"version\":");
        if (verPos != -1) {
          int start = payload.indexOf('"', verPos + 10);
          int end = payload.indexOf('"', start + 1);
          if (start != -1 && end != -1) {
            String newVersion = payload.substring(start + 1, end);
            
            if (newVersion != HARIXOS_VERSION_STRING) {
              Serial.println(F("\r\n[!] A new update is available!"));
              Serial.printf("Current version: %s\r\n", HARIXOS_VERSION_STRING);
              Serial.printf("Latest version:  %s\r\n", newVersion.c_str());
              
              // Manual JSON Parsing (Update Link)
              int linkPos = payload.indexOf("\"update_url\":");
              String dlLink = "";
              if (linkPos != -1) {
                int lStart = payload.indexOf('"', linkPos + 13);
                int lEnd = payload.indexOf('"', lStart + 1);
                if (lStart != -1 && lEnd != -1) {
                  dlLink = payload.substring(lStart + 1, lEnd);
                }
              }

              // Manual JSON Parsing (Changelog Array)
              int logsPos = payload.indexOf("\"changelog\":");
              if (logsPos != -1) {
                Serial.println(F("\r\nWhat's New:"));
                int arrayStart = payload.indexOf('[', logsPos);
                int arrayEnd = payload.indexOf(']', arrayStart);
                if (arrayStart != -1 && arrayEnd != -1) {
                  String logsArray = payload.substring(arrayStart + 1, arrayEnd);
                  
                  int lastPos = 0;
                  while (true) {
                    int logStart = logsArray.indexOf('"', lastPos);
                    if (logStart == -1) break;
                    int logEnd = logsArray.indexOf('"', logStart + 1);
                    if (logEnd == -1) break;
                    Serial.printf(" - %s\r\n", logsArray.substring(logStart + 1, logEnd).c_str());
                    lastPos = logEnd + 1;
                    yield();
                  }
                }
              }
              
              if (dlLink.length() > 0) {
                Serial.println();
                Serial.println(F("Download new update at:"));
                Serial.println(dlLink);
              } else {
                Serial.println(F("\r\nPlease visit GitHub to download the latest binary."));
              }
              Serial.println();
            } else {
              if (!silent) {
                Serial.printf("Current version: %s\r\n", HARIXOS_VERSION_STRING);
                Serial.println(F("HarixOS is up to date."));
              }
            }
          }
        }
        payload = String(); // Explicitly discard from RAM
      } else {
        if (!silent) Serial.printf("Error: Update check failed (HTTP %d)\r\n", httpCode);
      }
      http.end();
    } else {
      if (!silent) Serial.println(F("Error: Could not start HTTP request."));
    }
  } // WiFiClientSecure and HTTPClient destroyed here
}

void handleUpdate(const TokenizedLine &cmd) {
  if (cmd.count < 2 || toLowerCopy(cmd.tokens[1]) != "check") {
    Serial.println(F("Usage: update check"));
    return;
  }
  
  checkSystemUpdates(false);
}

void handleWifiDisconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println(F("WiFi disconnected and radio disabled."));
}

void handleWifiAp(const TokenizedLine &cmd) {
  if (cmd.count < 3) {
    Serial.println(F("Usage: wifi ap <ssid> [password] [channel]"));
    return;
  }

  String ssid = cmd.tokens[2];
  String password = cmd.count > 3 ? cmd.tokens[3] : String("12345678");
  uint8_t channel = cmd.count > 4 ? static_cast<uint8_t>(cmd.tokens[4].toInt()) : kDefaultApChannel;
  if (channel == 0) {
    channel = kDefaultApChannel;
  }

  WiFi.mode(WIFI_AP);
  bool started = WiFi.softAP(ssid.c_str(), password.c_str(), channel, false, 4);
  if (!started) {
    Serial.println(F("Failed to start access point."));
    return;
  }

  Serial.printf("AP started: %s\r\n", ssid.c_str());
  Serial.printf("  IP: %s\r\n", WiFi.softAPIP().toString().c_str());
  printWifiStatus();
}

void handleWifiMode(const TokenizedLine &cmd) {
  if (cmd.count < 3) {
    Serial.println(F("Usage: wifi mode off|sta|ap|staap"));
    return;
  }

  String mode = toLowerCopy(cmd.tokens[2]);
  if (mode == F("off")) {
    WiFi.mode(WIFI_OFF);
  } else if (mode == F("sta")) {
    WiFi.mode(WIFI_STA);
  } else if (mode == F("ap")) {
    WiFi.mode(WIFI_AP);
  } else if (mode == F("staap")) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    Serial.println(F("Unknown WiFi mode."));
    return;
  }

  Serial.printf("WiFi mode set to %s\r\n", wifiModeToString(WiFi.getMode()).c_str());
}

void handleWifiIp() {
  Serial.printf("Station IP: %s\r\n", WiFi.localIP().toString().c_str());
  Serial.printf("Gateway: %s\r\n", WiFi.gatewayIP().toString().c_str());
  Serial.printf("DNS: %s\r\n", WiFi.dnsIP().toString().c_str());
  Serial.printf("Subnet: %s\r\n", WiFi.subnetMask().toString().c_str());
  Serial.printf("AP IP: %s\r\n", WiFi.softAPIP().toString().c_str());
}

void handleWifiMac() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  Serial.printf("Station MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  if (WiFi.softAPmacAddress(mac)) {
    Serial.printf("AP MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
}

void handleGpioList() {
  printGpioPins();
}

void handleGpioMode(const TokenizedLine &cmd) {
  if (cmd.count < 4) {
    Serial.println(F("Usage: gpio mode <pin> in|out|pullup"));
    return;
  }

  uint8_t pin;
  if (!parsePin(cmd.tokens[2], pin) || !isAvailableGpio(pin)) {
    Serial.println(F("Invalid or reserved GPIO for ESP8266."));
    return;
  }

  String mode = toLowerCopy(cmd.tokens[3]);
  if (mode == F("in") || mode == F("input")) {
    pinMode(pin, INPUT);
  } else if (mode == F("pullup") || mode == F("input_pullup")) {
    pinMode(pin, INPUT_PULLUP);
  } else if (mode == F("out") || mode == F("output")) {
    pinMode(pin, OUTPUT);
  } else {
    Serial.println(F("Unknown mode. Use in, out, or pullup."));
    return;
  }

  Serial.printf("GPIO%u mode set to %s\r\n", pin, mode.c_str());
}

void handleGpioRead(const TokenizedLine &cmd) {
  if (cmd.count < 3) {
    Serial.println(F("Usage: gpio read <pin>"));
    return;
  }

  uint8_t pin;
  if (!parsePin(cmd.tokens[2], pin) || !isAvailableGpio(pin)) {
    Serial.println(F("Invalid or reserved GPIO for ESP8266."));
    return;
  }

  Serial.printf("GPIO%u = %d\r\n", pin, digitalRead(pin));
}

void handleGpioWrite(const TokenizedLine &cmd) {
  if (cmd.count < 4) {
    Serial.println(F("Usage: gpio write <pin> on|off|toggle|0|1"));
    return;
  }

  uint8_t pin;
  if (!parsePin(cmd.tokens[2], pin) || !isAvailableGpio(pin)) {
    Serial.println(F("Invalid or reserved GPIO for ESP8266."));
    return;
  }

  String v = toLowerCopy(cmd.tokens[3]);
  if (isBootStrapPin(pin)) {
    int target = -1;
    if (v == F("on") || v == F("1")) {
      target = HIGH;
    } else if (v == F("off") || v == F("0")) {
      target = LOW;
    } else if (v == F("toggle")) {
      Serial.println(F("Blocked: toggle on boot strap pins can leave unsafe boot state."));
      return;
    }

    if (target != -1 && isUnsafeBootLevel(pin, target)) {
      Serial.println(F("Blocked: requested level is unsafe for ESP8266 boot straps."));
      return;
    }
  }

  pinMode(pin, OUTPUT);
  if (v == F("on") || v == F("1")) {
    digitalWrite(pin, HIGH);
    Serial.printf("GPIO%u -> ON\r\n", pin);
  } else if (v == F("off") || v == F("0")) {
    digitalWrite(pin, LOW);
    Serial.printf("GPIO%u -> OFF\r\n", pin);
  } else if (v == F("toggle")) {
    int cur = digitalRead(pin);
    digitalWrite(pin, cur ? LOW : HIGH);
    Serial.printf("GPIO%u -> %s\r\n", pin, digitalRead(pin) ? "ON" : "OFF");
  } else {
    Serial.println(F("Unknown value. Use on/off/toggle/0/1."));
    return;
  }
  Serial.println(F("Warning: keep boot strap pins in safe states before reboot."));
}

void handleGpioPulse(const TokenizedLine &cmd) {
  if (cmd.count < 4) {
    Serial.println(F("Usage: gpio pulse <pin> <count> [delay_ms]"));
    return;
  }

  uint8_t pin;
  if (!parsePin(cmd.tokens[2], pin) || !isAvailableGpio(pin)) {
    Serial.println(F("Invalid or reserved GPIO for ESP8266."));
    return;
  }

  int count = cmd.tokens[3].toInt();
  int delayMs = cmd.count > 4 ? cmd.tokens[4].toInt() : 250;
  if (count <= 0 || delayMs < 0) {
    Serial.println(F("Invalid pulse parameters."));
    return;
  }

  pinMode(pin, OUTPUT);
  for (int index = 0; index < count; ++index) {
    digitalWrite(pin, HIGH);
    delay(delayMs);
    digitalWrite(pin, LOW);
    delay(delayMs);
    yield();
  }

  if (isBootStrapPin(pin)) {
    // Ensure strap pins are left in a safe post-command state.
    if (pin == 0 || pin == 2) {
      digitalWrite(pin, HIGH);
    } else if (pin == 15) {
      digitalWrite(pin, LOW);
    }
  }

  Serial.printf("GPIO%u pulsed %d times\r\n", pin, count);
}

void handleI2cBegin(const TokenizedLine &cmd) {
  if (cmd.count < 4) {
    Serial.println(F("Usage: i2c begin <sda_pin> <scl_pin>"));
    Serial.println(F("Choose board-appropriate GPIO pins for software I2C."));
    return;
  }

  uint8_t sda;
  uint8_t scl;
  if (!parsePin(cmd.tokens[2], sda) || !parsePin(cmd.tokens[3], scl)) {
    Serial.println(F("Invalid I2C pins."));
    return;
  }

  Wire.begin(sda, scl);
  Serial.printf("I2C started on SDA GPIO%u, SCL GPIO%u\r\n", sda, scl);
}

void handleI2cScan() {
  Serial.println(F("Scanning I2C bus..."));
  int found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.printf("  Found device at 0x%02X\r\n", address);
      ++found;
    }
    delay(1);
    yield();
  }
  if (found == 0) {
    Serial.println(F("  No devices found."));
  }
}

void handleI2c(const TokenizedLine &cmd) {
  if (cmd.count < 2) {
    Serial.println(F("Usage: i2c begin <sda> <scl> | i2c scan"));
    return;
  }

  String action = toLowerCopy(cmd.tokens[1]);
  if (action == F("begin")) {
    handleI2cBegin(cmd);
  } else if (action == F("scan")) {
    handleI2cScan();
  } else {
    Serial.println(F("Unknown I2C action."));
  }
}

void handleWifi(const TokenizedLine &cmd) {
  if (cmd.count < 2) {
    printWifiStatus();
    return;
  }

  String action = toLowerCopy(cmd.tokens[1]);
  if (action == F("status")) {
    printWifiStatus();
  } else if (action == F("scan")) {
    handleWifiScan();
  } else if (action == F("connect")) {
    handleWifiConnect(cmd);
  } else if (action == F("disconnect")) {
    handleWifiDisconnect();
  } else if (action == F("ap")) {
    handleWifiAp(cmd);
  } else if (action == F("mode")) {
    handleWifiMode(cmd);
  } else if (action == F("ip")) {
    handleWifiIp();
  } else if (action == F("mac")) {
    handleWifiMac();
  } else {
    Serial.println(F("Unknown WiFi action."));
  }
}

void tryAutoWifi() {
  if (shellSettings.wifiSSID.length() == 0) {
    return;
  }

  Serial.printf("Auto-connect: Scanning for '%s'...\r\n", shellSettings.wifiSSID.c_str());
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  bool found = false;
  for (int i = 0; i < n; ++i) {
    if (WiFi.SSID(i) == shellSettings.wifiSSID) {
      found = true;
      break;
    }
  }
  WiFi.scanDelete();

  if (found) {
    Serial.print("Network found. Connecting");
    WiFi.begin(shellSettings.wifiSSID.c_str(), shellSettings.wifiPassword.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000UL) {
      delay(500);
      Serial.print('.');
      yield();
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(F("Auto-connected to WiFi."));
      configTime(harixos::api::tz::toPosix(shellSettings.timezone.c_str()), "pool.ntp.org", "time.nist.gov");
    } else {
      Serial.println(F("Auto-connect failed."));
    }
  } else {
    Serial.println(F("Saved network not in range."));
  }
}

void handleGpio(const TokenizedLine &cmd) {
  if (cmd.count < 2) {
    handleGpioList();
    return;
  }

  String action = toLowerCopy(cmd.tokens[1]);
  if (action == F("list") || action == F("pins")) {
    handleGpioList();
  } else if (action == F("mode")) {
    handleGpioMode(cmd);
  } else if (action == F("read")) {
    handleGpioRead(cmd);
  } else if (action == F("write")) {
    handleGpioWrite(cmd);
  } else if (action == F("pulse")) {
    handleGpioPulse(cmd);
  } else {
    Serial.println(F("Unknown GPIO action."));
  }
}

void handleTime(const TokenizedLine &cmd) {
  if (cmd.count == 1) {
    time_t now = time(nullptr);
    if (now < 1000000000) {
      Serial.println(F("Time is not set. Use 'time set <HH:MM:SS> <YYYY-MM-DD>' or 'time sync'"));
      return;
    }
    struct tm *timeinfo = localtime(&now);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);
    Serial.println(buf);
    return;
  }
  
  String action = toLowerCopy(cmd.tokens[1]);
  if (action == F("sync")) {
    if (cmd.count >= 3) {
      String tzVal = cmd.tokens[2];
      shellSettings.timezone = tzVal;
      harixos::saveSettings(shellSettings);
      const char* posixStr = harixos::api::tz::toPosix(tzVal.c_str());
      setenv("TZ", posixStr, 1);
      tzset();
      Serial.printf("Timezone updated to: %s (%s)\r\n", tzVal.c_str(), posixStr);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(F("Syncing time via NTP..."));
      const char* posixStr = harixos::api::tz::toPosix(shellSettings.timezone.c_str());
      configTime(posixStr, "pool.ntp.org", "time.nist.gov");
      delay(500); // Give it a brief moment
      Serial.println(F("NTP sync requested. Check 'time' in a few seconds."));
    } else {
      Serial.println(F("Cannot sync time. WiFi is not connected."));
    }
  } else if (action == F("list-tz") || action == F("timezones")) {
    Serial.println(F("Common Timezone Regions:"));
    Serial.println(F("  Americas:"));
    Serial.println(F("    America/New_York  UTC-5  US Eastern"));
    Serial.println(F("    America/Chicago   UTC-6  US Central"));
    Serial.println(F("    America/Denver    UTC-7  US Mountain"));
    Serial.println(F("    America/Los_Angeles UTC-8 US Pacific"));
    Serial.println(F("    America/Sao_Paulo UTC-3  Brazil"));
    Serial.println(F("  Europe:"));
    Serial.println(F("    Europe/London     UTC+0  UK / Western European"));
    Serial.println(F("    Europe/Paris      UTC+1  Central European"));
    Serial.println(F("    Europe/Moscow     UTC+4  Russia Moscow"));
    Serial.println(F("    Europe/Athens     UTC+2  Eastern European"));
    Serial.println(F("  Africa:"));
    Serial.println(F("    Africa/Johannesburg UTC+2 South Africa"));
    Serial.println(F("    Africa/Lagos      UTC+1  West Africa"));
    Serial.println(F("    Africa/Nairobi    UTC+3  East Africa"));
    Serial.println(F("  Asia:"));
    Serial.println(F("    Asia/Dubai        UTC+4  UAE"));
    Serial.println(F("    Asia/Kolkata      UTC+5:30 India"));
    Serial.println(F("    Asia/Bangkok      UTC+7  SE Asia"));
    Serial.println(F("    Asia/Shanghai     UTC+8  China"));
    Serial.println(F("    Asia/Tokyo        UTC+9  Japan"));
    Serial.println(F("    Asia/Seoul        UTC+9  Korea"));
    Serial.println(F("  Oceania:"));
    Serial.println(F("    Australia/Sydney  UTC+10 Australian Eastern"));
    Serial.println(F("    Pacific/Auckland  UTC+12 New Zealand"));
    Serial.println(F("    Pacific/Honolulu  UTC-10 Hawaii"));
    Serial.println(F("  Other:"));
    Serial.println(F("    UTC               UTC+0  Coordinated Universal Time"));
    Serial.println(F("  DST is handled automatically. Use 'settings timezone <region>' to set."));
  } else if (action == F("set")) {
    if (cmd.count < 4) {
      Serial.println(F("Usage: time set <HH:MM:SS> <YYYY-MM-DD>"));
      return;
    }
    struct tm t = {0};
    int hh, mm, ss, YYYY, MM, DD;
    if (sscanf(cmd.tokens[2].c_str(), "%d:%d:%d", &hh, &mm, &ss) == 3 &&
        sscanf(cmd.tokens[3].c_str(), "%d-%d-%d", &YYYY, &MM, &DD) == 3) {
      t.tm_hour = hh;
      t.tm_min = mm;
      t.tm_sec = ss;
      t.tm_year = YYYY - 1900;
      t.tm_mon = MM - 1;
      t.tm_mday = DD;
      time_t now = mktime(&t);
      struct timeval tv = { .tv_sec = now, .tv_usec = 0 };
      settimeofday(&tv, nullptr);
      Serial.println(F("Time manually set."));
    } else {
      Serial.println(F("Invalid time format."));
    }
  } else {
    Serial.println(F("Unknown time action."));
  }
}

void handleSchedule(const String &line) {
  // Parse the raw line rather than cmd.tokens: `schedule add` plus six cron
  // fields already consumes 8 of kMaxTokens=12, so reassembling the token
  // list would silently truncate the command to its first four words.
  String rest = line;
  rest.trim();
  if (rest.length() >= 8 && rest.substring(0, 8).equalsIgnoreCase("schedule")) {
    rest = rest.substring(8);
  }
  rest.trim();

  if (rest.length() == 0) {
    harixos::kernel::systemScheduler.listTasks(Serial);
    Serial.println(F("Usage: schedule add <sec> <min> <hour> <dom> <month> <dow> <command> | schedule remove <id> | schedule run <id> | schedule list"));
    return;
  }

  int sep = rest.indexOf(' ');
  String action = toLowerCopy(sep < 0 ? rest : rest.substring(0, sep));
  String tail = sep < 0 ? String("") : rest.substring(sep + 1);
  tail.trim();

  if (action == F("list")) {
    harixos::kernel::systemScheduler.listTasks(Serial);
    return;
  }

  if (action == F("add")) {
    if (tail.length() == 0) {
      Serial.println(F("Usage: schedule add <sec> <min> <hour> <dom> <month> <dow> <command>"));
      return;
    }

    // The remainder is exactly one save line without its newline, so the
    // save-format budget is the right cap for it.
    char tailBuf[harixos::api::cron::kMaxSaveLine];
    tail.toCharArray(tailBuf, sizeof(tailBuf));

    char cronBuf[64];
    const char *command = nullptr;
    const char *err = harixos::api::cron::splitLine(tailBuf, cronBuf,
                                                    sizeof(cronBuf), &command);
    int badField = -1;
    if (err == nullptr) {
      harixos::api::cron::Spec spec;
      err = harixos::api::cron::parse(cronBuf, spec, &badField);
    }
    if (err != nullptr) {
      if (badField >= 0) Serial.printf("%s (field %d)\r\n", err, badField);
      else Serial.println(err);
      return;
    }

    int id = harixos::kernel::systemScheduler.add(cronBuf, String(command));
    if (id < 0) {
      Serial.println(F("Scheduler full."));
      return;
    }
    if (!harixos::kernel::systemScheduler.save()) {
      Serial.printf("Task %d scheduled, but /harixos/schedule.cfg could not be written.\r\n", id);
      return;
    }
    Serial.printf("Task scheduled with ID %d\r\n", id);
    return;
  }

  if (action == F("remove")) {
    if (tail.length() == 0) {
      Serial.println(F("Usage: schedule remove <id>"));
      return;
    }
    int id = tail.toInt();
    if (!harixos::kernel::systemScheduler.removeTask(id)) {
      Serial.println(F("Task not found."));
      return;
    }
    if (!harixos::kernel::systemScheduler.save()) {
      Serial.println(F("Task removed, but /harixos/schedule.cfg could not be written."));
      return;
    }
    Serial.println(F("Task removed."));
    return;
  }

  if (action == F("run")) {
    if (tail.length() == 0) {
      Serial.println(F("Usage: schedule run <id>"));
      return;
    }
    int id = tail.toInt();
    if (harixos::kernel::systemScheduler.runTask(id) != 0) {
      Serial.println(F("Task not found."));
      return;
    }
    return;
  }

  Serial.println(F("Unknown schedule action."));
}

// ponytail: advertised help topics are hand-maintained here. Add a new
// `else if (topic == F(...))` block in handleHelp AND list the topic in
// kHelpTopics, or `help <topic>` advertises a dead topic. If this list ever
// drifts, convert handleHelp's if/else chain to a table keyed on kHelpTopics.
static const char* const kHelpTopics =
    "wifi|gpio|i2c|fs|serve|post|mqtt|onchange|relay|time|schedule|update|sensor|servo|motor|run|calc|set|vars|settings|powerprofile|cpufreq";

void handleHelp(const TokenizedLine &cmd) {
  if (cmd.count == 1) {
    Serial.println(F("HarixOS command shell"));
    Serial.println();
    Serial.println(F("System commands:"));
    Serial.println(F("  help                 Show all commands"));
    Serial.println(F("  about                Show version and features"));
    Serial.println(F("  info                 Show system, WiFi, and GPIO summary"));
    Serial.println(F("  chip                 Show chip and flash details"));
    Serial.println(F("  heap                 Show free heap"));
    Serial.println(F("  uptime               Show runtime"));
    Serial.println(F("  reboot               Reboot the ESP8266"));
    Serial.println(F("  adc                  Read A0"));
    Serial.println(F("  pull <url> <path>    Download file from internet"));
    Serial.println(F("  time                 Show current time & sync options"));
    Serial.println(F("  schedule             Manage background tasks"));
    Serial.println(F("  update check         Check for system updates"));
    Serial.println();
    Serial.println(F("Filesystem:"));
    Serial.println(F("  pwd                  Print current directory"));
    Serial.println(F("  cd <path>            Change directory"));
    Serial.println(F("  ls [-R] [path]       List files and folders"));
    Serial.println(F("  mkdir <path>         Create folders"));
    Serial.println(F("  touch <path>         Create empty files"));
    Serial.println(F("  rm [-r] <path>       Remove files or folders"));
    Serial.println(F("  cp <src> <dst>       Copy files"));
    Serial.println(F("  mv <src> <dst>       Move files"));
    Serial.println(F("  cat <path>           Print file contents"));
    Serial.println(F("  write <path> <txt>   Replace file contents"));
    Serial.println(F("  append <path> <txt>  Append to file"));
    Serial.println(F("  fs ...               Filesystem tools, same as top-level (fs pwd|cd|ls|mkdir|touch|rm|cp|mv|cat|write|append)"));
    Serial.println();
    Serial.println(F("Hardware & networking:"));
    Serial.println(F("  gpio ...             GPIO control and listing (gpio list|mode|read|write|pulse)"));
    Serial.println(F("  wifi ...             WiFi connection and scanning (wifi status|scan|connect|disconnect|ap|mode|ip|mac)"));
    Serial.println(F("  i2c ...              I2C bus tools (i2c begin|scan)"));
    Serial.println();
    Serial.println(F("Applications:"));
    Serial.println(F("  notepad <path>       Open text editor"));
    Serial.println(F("  settings show        Show current settings"));
    Serial.println(F("  settings banner on|off  Toggle startup banner"));
    Serial.println(F("  settings timezone <region>  Set timezone (e.g. Pacific/Auckland, UTC, PKT-5)"));
    Serial.println(F("  settings update on|off  Toggle auto-update check"));
    Serial.println(F("  settings save        Save settings to flash"));
    Serial.println(F("  settings reload      Reload settings from flash"));
    Serial.println(F("  powerprofile ...     Manage power profile (powerprofile [full|balanced|powersave|minimal|off] | set <profile> | apply | status)"));
    Serial.println(F("  cpufreq ...          Manage CPU frequency (cpufreq [40|80] | set <freq> | status)"));
    Serial.println(F("  sensor ...           HC-SR04 ultrasonic sensor (sensor init|ping|read|list)"));
    Serial.println(F("  servo ...            SG90 servo motor (servo attach|detach|write|read|list)"));
    Serial.println(F("  motor ...            L293D motor shield (motor init|forward|reverse|stop|brake|speed|list)"));
    Serial.println(F("  relay ...            Relay/latched switch (relay add <pin> <name> [on|off] | set <name> on|off | toggle <name> | status <name> | list)"));
    Serial.println(F("  run ...              Install/list/run/uninstall .hx apps (run install|list|run|uninstall)"));
    Serial.println(F("  calc <expr>          Evaluate arithmetic expressions"));
    Serial.println(F("  set <name> = <expr>  Store a variable for $name in scripts"));
    Serial.println(F("  serve ...            HTTP file server tools (serve start|stop|status|serve <file> [port])"));
    Serial.println(F("  post <url> <body>    Send HTTP webhook (POST)"));
    Serial.println(F("  mqtt ...             MQTT/IoT service (mqtt status|start|stop|on|off|pub)"));
    Serial.println(F("  onchange ...         GPIO onchange rules (onchange add|remove|list)"));
    Serial.println(F("  vars ...             Variable storage (vars list|set|get|del|save|load|clear)"));
    Serial.println();
    Serial.println(F("Help topics:"));
    Serial.println(F("  help ") + String(kHelpTopics) + F("  Show topic help"));
    return;
  }

  String topic = toLowerCopy(cmd.tokens[1]);
  if (topic == F("wifi")) {
    Serial.println(F("WiFi commands:"));
    Serial.println(F("  wifi status              Show current WiFi status"));
    Serial.println(F("  wifi scan                Scan available networks"));
    Serial.println(F("  wifi connect 'SSID' 'PW' Connect to network (saved to settings)"));
    Serial.println(F("  wifi disconnect          Disconnect WiFi"));
    Serial.println(F("  wifi ap 'SSID' 'PW'      Start access point"));
    Serial.println(F("  wifi mode off|sta|ap|staap Set WiFi mode"));
    Serial.println(F("  wifi ip                  Show IP configuration"));
    Serial.println(F("  wifi mac                 Show MAC addresses"));
    Serial.println();
    Serial.println(F("Note: Once connected via 'wifi connect', credentials are saved."));
    Serial.println(F("      On boot, HarixOS will auto-connect if the network is in range."));
  } else if (topic == F("gpio")) {
    Serial.println(F("GPIO commands:"));
    Serial.println(F("  gpio list"));
    Serial.println(F("  gpio mode <pin> in|out|pullup"));
    Serial.println(F("  gpio read <pin>"));
    Serial.println(F("  gpio write <pin> on|off|toggle|0|1"));
    Serial.println(F("  gpio pulse <pin> <count> [delay_ms]"));
  } else if (topic == F("fs")) {
    Serial.println(F("FS commands:"));
    Serial.println(F("  fs pwd"));
    Serial.println(F("  fs cd <path>"));
    Serial.println(F("  fs ls [-R] [path]"));
    Serial.println(F("  fs mkdir <path>"));
    Serial.println(F("  fs touch <path>"));
    Serial.println(F("  fs rm [-r] <path>"));
    Serial.println(F("  fs cp <src> <dst>"));
    Serial.println(F("  fs mv <src> <dst>"));
    Serial.println(F("  fs cat <path>"));
    Serial.println(F("  fs write <path> \"content\""));
    Serial.println(F("  fs append <path> \"content\""));
  } else if (topic == F("i2c")) {
    Serial.println(F("I2C commands:"));
    Serial.println(F("  i2c begin <sda_pin> <scl_pin>"));
    Serial.println(F("  i2c scan"));
  } else if (topic == F("serve")) {
    Serial.println(F("Serve commands:"));
    Serial.println(F("  serve <file> [port]"));
    Serial.println(F("  serve status"));
    Serial.println(F("  serve stop"));
    Serial.println(F("  Example: serve /index.html 80"));
  } else if (topic == F("time")) {
    Serial.println(F("Time commands:"));
    Serial.println(F("  time                     Show current time"));
    Serial.println(F("  time sync [region]       Sync NTP & optionally set timezone (IANA or POSIX)"));
    Serial.println(F("  time set <HH:MM:SS> <YYYY-MM-DD> Set time manually"));
    Serial.println(F("  time list-tz             Show common timezone regions"));
    Serial.println();
    Serial.println(F("Timezones:"));
    Serial.println(F("  Set via: settings timezone <IANA_REGION>   (e.g. Pacific/Auckland, UTC)"));
    Serial.println(F("       or: time sync <IANA_REGION>          (auto-offset from mapping)"));
    Serial.println(F("  POSIX strings still work (e.g. PKT-5, GMT0BST,M3.5.0/M10.5.0)"));
    Serial.println(F("  DST is handled automatically by the system"));
  } else if (topic == F("schedule")) {
    Serial.println(F("Scheduler commands (6-field cron: sec min hour dom month dow):"));
    Serial.println(F("  schedule list                     List all tasks"));
    Serial.println(F("  schedule remove <id>              Remove task by ID"));
    Serial.println(F("  schedule run <id>                 Run a task now, ignoring its schedule"));
    Serial.println(F("  schedule add <6 fields> <command> Add a task"));
    Serial.println();
    Serial.println(F("Fields: 0-59 sec, 0-59 min, 0-23 hour, 1-31 dom,"));
    Serial.println(F("        1-12 month, 0-6 dow (0 = Sunday)"));
    Serial.println(F("Syntax: *  n  a-b  */n  a-b/n  comma lists"));
    Serial.println(F("  dom and dow are AND'ed when both are restricted."));
    Serial.println(F("  Tasks are saved automatically; there is no save command."));
    Serial.println();
    Serial.println(F("Examples:"));
    Serial.println(F("  schedule add */5 * * * * * heap"));
    Serial.println(F("  schedule add 0 30 14 * * * settings save"));
    Serial.println(F("  schedule add 0 0 9 15 * 1 ping"));
  } else if (topic == F("update")) {
    Serial.println(F("Update commands:"));
    Serial.println(F("  update check         Check for system updates via GitHub"));
    Serial.println();
    Serial.println(F("Example:"));
    Serial.println(F("  update check"));
  } else if (topic == F("sensor")) {
    Serial.println(F("Sensor commands (HC-SR04 ultrasonic):"));
    Serial.println(F("  sensor init <trigger> <echo>    Initialize sensor on given pins"));
    Serial.println(F("  sensor ping [trigger] [echo]     Take a distance reading (defaults: trigger=4, echo=5)"));
    Serial.println(F("  sensor read [echo]               Read last measured distance (default echo=5)"));
    Serial.println(F("  sensor list                      List all initialized sensors"));
    Serial.println();
    Serial.println(F("Distance output: mm, cm, and meters"));
    Serial.println(F("Object detection: reports 'yes' or 'no'"));
  } else if (topic == F("servo")) {
    Serial.println(F("Servo commands (SG90 PWM on ESP8266):"));
    Serial.println(F("  servo attach <pin>       Attach servo to GPIO pin"));
    Serial.println(F("  servo detach <pin>       Detach servo from GPIO pin"));
    Serial.println(F("  servo write <pin> <angle>  Set angle (0-180 degrees)"));
    Serial.println(F("  servo read <pin>         Read current angle"));
    Serial.println(F("  servo list               List all attached servos"));
    Serial.println();
    Serial.println(F("Uses ESP8266 native PWM (ledc). Auto-attaches on write if not attached."));
  } else if (topic == F("mqtt")) {
    Serial.println(F("MQTT commands (IoT / Home Assistant):"));
    Serial.println(F("  mqtt status              Show connection, config, counters"));
    Serial.println(F("  mqtt start               Enable MQTT service"));
    Serial.println(F("  mqtt stop                Disable MQTT service"));
    Serial.println(F("  mqtt on|off              Alias for start/stop (used by HA switch)"));
    Serial.println(F("  mqtt pub <topic> <payload>  Publish raw message"));
    Serial.println();
    Serial.println(F("MQTT topics:"));
    Serial.println(F("  <prefix>/telemetry       Device telemetry JSON"));
    Serial.println(F("  <prefix>/shell/in        Shell commands (subscribe)"));
    Serial.println(F("  <prefix>/shell/out       Command replies (publish)"));
    Serial.println(F("  <prefix>/availability    Online/offline (LWT, retained)"));
    Serial.println(F("  homeassistant/...        HA discovery messages"));
    Serial.println();
    Serial.println(F("Note: shell/in does NOT use retain (PubSubClient limitation)."));
  } else if (topic == F("motor")) {
    Serial.println(F("Motor commands (L293D Motor Shield):"));
    Serial.println(F("  motor init [m1|m2]       Initialize motor (M1: D5/D6, M2: D7/D1)"));
    Serial.println(F("  motor forward [m1|m2]    Run motor forward"));
    Serial.println(F("  motor reverse [m1|m2]    Run motor reverse"));
    Serial.println(F("  motor stop [m1|m2]       Stop motor"));
    Serial.println(F("  motor brake [m1|m2]      Brake motor"));
    Serial.println(F("  motor speed <0-100> [m1|m2]  Set speed percentage"));
    Serial.println(F("  motor list               List all motors"));
    Serial.println();
    Serial.println(F("Controls 2 DC motors via PWM + GPIO direction pins."));
    Serial.println(F("Speed: 0-100% PWM duty cycle. Direction: forward/reverse."));
  } else if (topic == F("onchange")) {
    Serial.println(F("Onchange commands (GPIO edge detection):"));
    Serial.println(F("  onchange add <pin> <rising|falling|both>  Register edge handler"));
    Serial.println(F("  onchange remove <pin>  Remove onchange rule"));
    Serial.println(F("  onchange list          List all onchange rules"));
    Serial.println(F("      Pins accept 'D5' or '5'. Rules persist across reboots."));
  } else if (topic == F("relay")) {
    Serial.println(F("Relay / latched switch commands:"));
    Serial.println(F("  relay add <pin> <name> [on|off]  Register a relay (initial level optional)"));
    Serial.println(F("  relay set <name> on|off  Turn a relay on or off"));
    Serial.println(F("  relay toggle <name>     Toggle a relay's state"));
    Serial.println(F("  relay status <name>     Print a relay's current state"));
    Serial.println(F("  relay list              List all relays"));
    Serial.println(F("      Names are 1-15 chars [A-Za-z0-9_-]; state persists across reboots."));
  } else if (topic == F("vars")) {
    Serial.println(F("Variable storage (persistent across reboots):"));
    Serial.println(F("  vars list              List all stored variables"));
    Serial.println(F("  vars set <name> <expr> Store a variable"));
    Serial.println(F("  vars get <name>        Print a variable's value"));
    Serial.println(F("  vars del <name>        Delete a variable"));
    Serial.println(F("  vars save              Write variables to flash"));
    Serial.println(F("  vars load              Reload variables from flash"));
    Serial.println(F("  vars clear             Delete all variables"));
  } else if (topic == F("post")) {
    Serial.println(F("HTTP webhook POST:"));
    Serial.println(F("  post <url> <body> [content-type]"));
    Serial.println(F("  Default content-type is application/json."));
  } else if (topic == F("settings")) {
    Serial.println(F("Device settings:"));
    Serial.println(F("  settings show          Print current settings"));
    Serial.println(F("  settings banner on|off  Toggle startup banner"));
    Serial.println(F("  settings timezone <tz> Set timezone (IANA, UTC, or POSIX)"));
    Serial.println(F("  settings update on|off  Toggle auto-update check"));
    Serial.println(F("  settings save          Persist settings to flash"));
    Serial.println(F("  settings reload        Reload settings from flash"));
  } else if (topic == F("run")) {
    Serial.println(F(".hx app manager:"));
    Serial.println(F("  run list               List installed apps"));
    Serial.println(F("  run install <name>     Install an app (reads from serial)"));
    Serial.println(F("  run <name>             Run an installed app"));
    Serial.println(F("  run uninstall <name>   Remove an app"));
  } else if (topic == F("calc")) {
    Serial.println(F("Arithmetic evaluator:"));
    Serial.println(F("  calc <expression>      e.g. calc 1+2*(3-4)/5"));
  } else if (topic == F("set")) {
    Serial.println(F("Store a variable for use in scripts:"));
    Serial.println(F("  set <name> = <expr>    e.g. set threshold = 500"));
    Serial.println(F("  Value persists and can be referenced as $name in scripts."));
  } else if (topic == F("powerprofile")) {
    Serial.println(F("Power profile management:"));
    Serial.println(F("  powerprofile           Show current profile"));
    Serial.println(F("  powerprofile <profile> Set profile (full|balanced|powersave|minimal|off)"));
    Serial.println(F("  powerprofile apply     Re-apply saved profile"));
    Serial.println(F("  powerprofile status    Show profile and CPU frequency"));
  } else if (topic == F("cpufreq")) {
    Serial.println(F("CPU frequency management:"));
    Serial.println(F("  cpufreq                Show current frequency"));
    Serial.println(F("  cpufreq <40|80>        Set CPU frequency"));
    Serial.println(F("  cpufreq status         Show current frequency"));
    Serial.println(F("  Note: 40 MHz is rejected by the ESP8266 SDK."));
  } else {
    Serial.println(F("Unknown help topic."));
  }
}

void handlePowerProfile(const String &line);
void handleCpuFreq(const String &line);
void handleSensor(const String &line);
void handleServo(const String &line);
void handleMotor(const String &line);
void handleVars(const String &line);

void executeCommand(const String &line) {
  TokenizedLine cmd = tokenize(line);
  if (cmd.count == 0) {
    return;
  }

  String command = toLowerCopy(cmd.tokens[0]);
  if (command == F("help") || command == F("?")) {
    handleHelp(cmd);
  } else if (command == F("about")) {
    handleAboutInfo();
  } else if (command == F("info")) {
    handleInfo();
  } else if (command == F("chip")) {
    handleChip();
  } else if (command == F("heap")) {
    handleHeap();
  } else if (command == F("uptime")) {
    handleUptime();
  } else if (command == F("reboot")) {
    handleReset();
  } else if (command == F("reset")) {
    Serial.println(F("Command renamed: use reboot"));
    handleReset();
  } else if (command == F("adc") || command == F("a0")) {
    handleAdc();
  } else if (command == F("pull")) {
    handlePull(cmd);
  } else if (command == F("pwd")) {
    handlePwd();
  } else if (command == F("cd")) {
    handleCd(cmd);
  } else if (command == F("ls")) {
    handleLs(cmd);
  } else if (command == F("mkdir")) {
    handleMkdir(cmd);
  } else if (command == F("touch")) {
    handleTouch(cmd);
  } else if (command == F("rm")) {
    handleRm(cmd);
  } else if (command == F("cp")) {
    handleCp(cmd);
  } else if (command == F("mv")) {
    handleMv(cmd);
  } else if (command == F("cat")) {
    handleCat(cmd);
  } else if (command == F("write")) {
    handleWrite(cmd);
  } else if (command == F("append")) {
    handleAppend(cmd);
  } else if (command == F("notepad")) {
    handleNotepad(cmd);
  } else if (command == F("post")) {
    handlePost(cmd);
  } else if (command == F("mqtt")) {
    handleMqtt(line);
  } else if (command == F("onchange")) {
    handleOnchange(line);
  } else if (command == F("relay")) {
    handleRelay(line);
  } else if (command == F("settings")) {
    handleSettings(cmd);
  } else if (command == F("wifi")) {
    handleWifi(cmd);
  } else if (command == F("serve")) {
    handleServe(cmd);
  } else if (command == F("run")) {
    handleRun(cmd);
  } else if (command == F("gpio")) {
    handleGpio(cmd);
  } else if (command == F("fs")) {
    handleFs(cmd);
  } else if (command == F("calc")) {
    handleCalc(line);
  } else if (command == F("set")) {
    handleSet(line);
  } else if (command == F("i2c")) {
    handleI2c(cmd);
  } else if (command == F("cls") || command == F("clear")) {
    for (uint8_t index = 0; index < 20; ++index) {
      Serial.println();
    }
  } else if (command == F("time")) {
    handleTime(cmd);
  } else if (command == F("schedule")) {
    handleSchedule(line);
  } else if (command == F("update")) {
    handleUpdate(cmd);
  } else if (command == F("powerprofile")) {
    handlePowerProfile(line);
  } else if (command == F("cpufreq")) {
    handleCpuFreq(line);
  } else if (command == F("sensor")) {
    String tail = line.substring(6);
    handleSensor(tail);
  } else if (command == F("servo")) {
    String tail = line.substring(5);
    handleServo(tail);
  } else if (command == F("motor")) {
    String tail = line.substring(5);
    handleMotor(tail);
  } else if (command == F("vars")) {
    handleVars(line.substring(5));
  } else {
    Serial.printf("Unknown command: %s\r\n", cmd.tokens[0].c_str());
    Serial.println(F("Type help for the command list."));
  }
}



void handleSerialInput() {
  static unsigned long lastCharTime = 0;
  const unsigned long serialDebounceMs = 50;

  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    lastCharTime = millis();

    if (c == '\r' || c == '\n') {
      if (inputLine.length() > 0) {
        Serial.println();
        String line = trimCopy(inputLine);
        inputLine = String();
        promptVisible = false;
        executeCommand(line);
      } else if (promptVisible) {
        Serial.println();
      }
      if (!promptVisible) {
        printPrompt();
      }
      continue;
    }

    if (c == '\b' || c == 127) {
      if (inputLine.length() > 0) {
        inputLine.remove(inputLine.length() - 1);
        Serial.print(F("\b \b"));
      }
      continue;
    }

    if (isPrintable(static_cast<unsigned char>(c)) && inputLine.length() < kMaxLineLength) {
      inputLine += c;
      Serial.write(c);
    }
  }
}

void printBanner() {
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F(" HarixOS"));
  Serial.println(F(" Type help for commands"));
  Serial.println(F("========================================"));
  Serial.println();
}

void handleFs(const TokenizedLine &cmd) {
  if (cmd.count < 2) {
    handleLs(cmd);
    return;
  }
  String action = toLowerCopy(cmd.tokens[1]);
  if (action == F("pwd")) {
    handlePwd();
  } else if (action == F("cd")) {
    handleCd(cmd);
  } else if (action == F("ls") || action == F("list")) {
    handleLs(cmd);
  } else if (action == F("mkdir")) {
    handleMkdir(cmd);
  } else if (action == F("touch")) {
    handleTouch(cmd);
  } else if (action == F("rm") || action == F("remove")) {
    handleRm(cmd);
  } else if (action == F("cp")) {
    handleCp(cmd);
  } else if (action == F("mv")) {
    handleMv(cmd);
  } else if (action == F("cat") || action == F("read")) {
    handleCat(cmd);
  } else if (action == F("write")) {
    handleWrite(cmd);
  } else if (action == F("append")) {
    handleAppend(cmd);
  } else if (action == F("notepad")) {
    handleNotepad(cmd);
  } else if (action == F("settings")) {
    handleSettings(cmd);
  } else {
    Serial.println(F("Unknown fs action."));
  }
}

void handlePowerProfile(const String &line) {
  String rest = line;
  rest.trim();
  if (rest.equalsIgnoreCase("powerprofile")) {
    rest = "";
  } else if (rest.startsWith("powerprofile")) {
    rest = rest.substring(12);
    rest.trim();
  }
  if (rest.length() == 0) {
    Serial.printf("Power profile: %s\r\nUsage: powerprofile <full|balanced|powersave|minimal|off>\r\n", shellSettings.powerProfile.c_str());
    return;
  }

  int sep = rest.indexOf(' ');
  String action = sep < 0 ? rest : rest.substring(0, sep);
  String tail = sep < 0 ? String("") : rest.substring(sep + 1);
  tail.trim();

  if (action.equalsIgnoreCase("set") || action.equalsIgnoreCase("")) {
    if (tail.length() == 0) {
      Serial.printf("Current profile: %s\r\nUsage: powerprofile set <full|balanced|powersave|minimal|off>\r\n", shellSettings.powerProfile.c_str());
      return;
    }
    harixos::api::PowerProfile profile = harixos::api::stringToPowerProfile(tail);
    shellSettings.powerProfile = harixos::api::powerProfileToString(profile);
    harixos::api::applyPowerProfile(profile);
    harixos::api::setCpuFrequency(harixos::api::stringToCpuFreq(String(shellSettings.cpufreq)));
    harixos::saveSettings(shellSettings);
    Serial.printf("Power profile set to %s.\r\n", shellSettings.powerProfile.c_str());
  } else if (action.equalsIgnoreCase("apply")) {
    harixos::api::PowerProfile profile = harixos::api::stringToPowerProfile(shellSettings.powerProfile);
    harixos::api::applyPowerProfile(profile);
    harixos::api::setCpuFrequency(harixos::api::stringToCpuFreq(String(shellSettings.cpufreq)));
    Serial.printf("Power profile applied (%s).\r\n", shellSettings.powerProfile.c_str());
  } else if (action.equalsIgnoreCase("status")) {
    Serial.printf("Power profile: %s\r\n", shellSettings.powerProfile.c_str());
    Serial.printf("CPU Freq: %d MHz\r\n", shellSettings.cpufreq);
  } else {
    harixos::api::PowerProfile profile = harixos::api::stringToPowerProfile(action);
    shellSettings.powerProfile = harixos::api::powerProfileToString(profile);
    harixos::api::applyPowerProfile(profile);
    harixos::api::setCpuFrequency(harixos::api::stringToCpuFreq(String(shellSettings.cpufreq)));
    harixos::saveSettings(shellSettings);
    Serial.printf("Power profile set to %s.\r\n", shellSettings.powerProfile.c_str());
  }
}

void handleCpuFreq(const String &line) {
  String rest = line;
  rest.trim();
  if (rest.equalsIgnoreCase("cpufreq")) {
    rest = "";
  } else if (rest.startsWith("cpufreq")) {
    rest = rest.substring(8);
    rest.trim();
  }
  if (rest.length() == 0) {
    Serial.printf("CPU frequency: %d MHz\r\nUsage: cpufreq <40|80>\r\n", shellSettings.cpufreq);
    return;
  }

  int sep = rest.indexOf(' ');
  String action = sep < 0 ? rest : rest.substring(0, sep);
  String tail = sep < 0 ? String("") : rest.substring(sep + 1);
  tail.trim();

  int freq = -1;
  if (action.equalsIgnoreCase("set") || action.equalsIgnoreCase("")) {
    if (tail.length() == 0) {
      Serial.printf("Current freq: %d MHz\r\nUsage: cpufreq set <40|80>\r\n", shellSettings.cpufreq);
      return;
    }
    freq = tail.toInt();
  } else if (action.equalsIgnoreCase("status")) {
    Serial.printf("CPU frequency: %d MHz\r\n", shellSettings.cpufreq);
    return;
  } else {
    freq = (tail.length() > 0 ? tail : action).toInt();
  }
  if (freq != 40 && freq != 80) {
    Serial.println(F("Invalid frequency. Use 40 or 80."));
    return;
  }
  if (!harixos::api::setCpuFrequency(harixos::api::stringToCpuFreq(String(freq)))) {
    Serial.println(F("Error: 40 MHz is not supported by the ESP8266 SDK. CPU frequency unchanged."));
    return;
  }
  shellSettings.cpufreq = freq;
  harixos::saveSettings(shellSettings);
  Serial.printf("CPU frequency set to %d MHz.\r\n", freq);
}

void handleSensor(const String &line) {
  harixos::api::ApiResult result = harixos::api::SensorAPI::runCommand(line, Serial);
  if (result.isError()) {
    Serial.print(F("ERROR: "));
    Serial.println(result.message);
  }
}

void handleServo(const String &line) {
  harixos::api::ApiResult result = harixos::api::ServoAPI::runCommand(line, Serial);
  if (result.isError()) {
    Serial.print(F("ERROR: "));
    Serial.println(result.message);
  }
}

void handleMotor(const String &line) {
  harixos::api::ApiResult result = harixos::api::MotorAPI::runCommand(line, Serial);
  if (result.isError()) {
    Serial.print(F("ERROR: "));
    Serial.println(result.message);
  }
}

void handleVars(const String &line) {
  const char *err = harixos::api::vars::runCommand(
      line.c_str(),
      [](const char *outLine, void *stream) {
        static_cast<Stream *>(stream)->println(outLine);
      },
      &Serial);
  if (err != nullptr) {
    Serial.println(err);
  }
}

}  // namespace


extern "C" {
  #include "user_interface.h"
}

// Force the RF (radio) to be completely off at boot to prevent brownouts on ESP-01
RF_PRE_INIT() {
  system_phy_set_powerup_option(3);
}

void setup() {
  
  Serial.begin(115200);
  Serial.setTimeout(25);
  delay(500);

  Serial.println();
  Serial.println(F("***********************************"));
  Serial.println(F("*** SYSTEM HARDWARE BOOTING ***"));
  Serial.println(F("***********************************"));
  Serial.flush();
  delay(100);

  // Disable automatic WiFi during boot to prevent crash loops
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);

  Wire.begin();
  if (LittleFS.begin()) {
    Serial.println(F("LittleFS mounted."));
  } else {
    Serial.println(F("LittleFS mount failed."));
  }

  harixos::api::expr::setResolver(harixos::api::resolveDeviceValueToken);

  shellSettings = harixos::loadSettings();
  harixos::kernel::systemScheduler.load();
  const char* bootPosix = harixos::api::tz::toPosix(shellSettings.timezone.c_str());
  setenv("TZ", bootPosix, 1);
  tzset();

  if (shellSettings.bannerEnabled) {
    printBanner();
    printSystemInfo();
  } else {
    Serial.println(F("HarixOS ready."));
  }
  Serial.println();
  
  tryAutoWifi();
  harixos::kernel::CpuHandler::init();
  
  // Apply saved power profile and CPU frequency at boot
  harixos::api::PowerProfile bootProfile = harixos::api::stringToPowerProfile(shellSettings.powerProfile);
  harixos::api::applyPowerProfile(bootProfile);
  if (!harixos::api::setCpuFrequency(harixos::api::stringToCpuFreq(String(shellSettings.cpufreq)))) {
    Serial.println(F("Warning: 40 MHz is not supported by the ESP8266 SDK. Running at 80 MHz."));
    shellSettings.cpufreq = 80;
    harixos::saveSettings(shellSettings);
  }

  // If update check is enabled, check now and show results.
  if (shellSettings.autoUpdateCheck) {
    delay(500); // Small delay to let network settle
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(F("System: Running auto-update check..."));
      checkSystemUpdates(false); // Show results even if up to date
    }
  }

  harixos::iot::begin();
  harixos::iot::beginOnchange();
  harixos::iot::beginRelay();

  printPrompt();
  
  // Ensure the serial buffer is completely sent before starting the main loop
  Serial.flush();
  delay(100);
}

void loop() {
  handleSerialInput();
  if (httpServerRunning && httpServer) {
    httpServer->handleClient();
  }
  harixos::kernel::systemScheduler.update();
  harixos::iot::update();
  harixos::iot::updateOnchange();
  
  // Auto-switch to lower power when no serial session is connected
  // Only switch if terminal has been idle for more than 1 minute
  static bool lastSerialState = true;
  static unsigned long lastStateChange = 0;
  const unsigned long idleTimeoutMs = 60000UL;  // 1 minute
  
  bool currentSerialState = harixos::api::isSerialConnected();
  
  if (currentSerialState != lastSerialState) {
    lastStateChange = millis();
    lastSerialState = currentSerialState;
  } else if ((millis() - lastStateChange >= idleTimeoutMs)) {
    // Terminal has been stable for more than 1 minute - execute the pending switch
    if (!currentSerialState) {
      // Terminal idle - switch to powersave if on balanced/full
      if (shellSettings.powerProfile.equalsIgnoreCase("balanced") || shellSettings.powerProfile.equalsIgnoreCase("full")) {
        harixos::api::PowerProfile psProfile = harixos::api::stringToPowerProfile("powersave");
        harixos::api::applyPowerProfile(psProfile);
        Serial.println(F("Terminal idle for 1 minute. Switched to powersave."));
      }
    } else {
      // Terminal active for 1 minute - restore saved profile
      harixos::api::PowerProfile restoredProfile = harixos::api::stringToPowerProfile(shellSettings.powerProfile);
      harixos::api::applyPowerProfile(restoredProfile);
      harixos::api::setCpuFrequency(harixos::api::stringToCpuFreq(String(shellSettings.cpufreq)));
      Serial.printf("Terminal active for 1 minute. Restored profile: %s.\r\n", shellSettings.powerProfile.c_str());
    }
    // Reset to prevent repeated switches
    lastStateChange = millis();
  }
  
  // Use safe yield to process background tasks and feed WDT
  harixos::kernel::CpuHandler::yieldSafely();
}
