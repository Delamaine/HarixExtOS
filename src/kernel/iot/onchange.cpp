#include "kernel/iot/onchange.h"

#include <FS.h>
#include <LittleFS.h>
#include <Arduino.h>

namespace harixos { namespace iot {

using harixos::api::API_INVALID_PIN;
using harixos::api::API_DUPLICATE;
using harixos::api::API_ERROR;
using harixos::api::API_OK;

static OnchangeRule s_rules[kMaxOnchangeRules];
static uint8_t s_ruleCount = 0;

static uint8_t findRuleIndex(uint8_t pin) {
  for (uint8_t i = 0; i < s_ruleCount; ++i) {
    if (s_rules[i].pin == pin && s_rules[i].active) return i;
  }
  return 0xFF;
}

static bool isPinValid(uint8_t pin) {
  if (pin > 16) return false;
  if (pin >= 6 && pin <= 11) return false;
  if (pin == 1 || pin == 3) return false;
  return true;
}

ApiResult registerOnchange(uint8_t pin, EdgeMode edgeMode,
                           std::function<void(uint8_t pin, bool level)> callback) {
  if (!isPinValid(pin)) {
    return ApiResult(API_INVALID_PIN, "GPIO" + String(pin) + " is reserved or invalid");
  }
  uint8_t idx = findRuleIndex(pin);
  if (idx < s_ruleCount && s_rules[idx].active) {
    return ApiResult(API_DUPLICATE, "onchange rule already registered for GPIO" + String(pin));
  }
  if (s_ruleCount >= kMaxOnchangeRules) {
    return ApiResult(API_ERROR, "Maximum onchange rules reached (" + String(kMaxOnchangeRules) + ")");
  }
  if (idx >= s_ruleCount) {
    idx = s_ruleCount++;
  }
  s_rules[idx].pin = pin;
  s_rules[idx].edgeMode = edgeMode;
  s_rules[idx].callback = callback;
  s_rules[idx].active = true;
  s_rules[idx].debounce = DebounceState{false, false, false, 0};
  pinMode(pin, INPUT);
  return ApiResult(API_OK, "onchange rule registered for GPIO" + String(pin));
}

ApiResult unregisterOnchange(uint8_t pin) {
  uint8_t idx = findRuleIndex(pin);
  if (idx >= s_ruleCount || !s_rules[idx].active) {
    return ApiResult(API_INVALID_PIN, "No onchange rule for GPIO" + String(pin));
  }
  s_rules[idx].active = false;
  s_ruleCount--;
  return ApiResult(API_OK, "onchange rule removed for GPIO" + String(pin));
}

bool hasOnchangeRule(uint8_t pin) {
  return findRuleIndex(pin) < s_ruleCount && s_rules[findRuleIndex(pin)].active;
}

EdgeMode getEdgeMode(uint8_t pin) {
  uint8_t idx = findRuleIndex(pin);
  if (idx < s_ruleCount && s_rules[idx].active) {
    return s_rules[idx].edgeMode;
  }
  return EdgeMode::Both;
}

void feedOnchange(uint8_t pin, bool rawLevel, uint32_t nowMs) {
  for (uint8_t i = 0; i < s_ruleCount; ++i) {
    if (s_rules[i].active) {
      feedEdge(s_rules[i].debounce, rawLevel, nowMs, nullptr);
    }
  }
}

void updateOnchange() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < s_ruleCount; ++i) {
    if (!s_rules[i].active) continue;
    bool rawLevel = digitalRead(s_rules[i].pin);
    bool newLevel = false;
    if (feedEdge(s_rules[i].debounce, rawLevel, now, &newLevel)) {
      EdgeMode mode = s_rules[i].edgeMode;
      bool oldLevel = !s_rules[i].debounce.stableLevel;
      if (edgeMatches(mode, oldLevel, newLevel)) {
        if (s_rules[i].callback) {
          s_rules[i].callback(s_rules[i].pin, newLevel);
        }
      }
    }
  }
}

static String edgeModeToString(EdgeMode mode) {
  switch (mode) {
    case EdgeMode::Rising: return "rising";
    case EdgeMode::Falling: return "falling";
    case EdgeMode::Both: return "both";
  }
  return "both";
}

static EdgeMode stringToEdgeMode(const String &s) {
  String lower = s;
  lower.toLowerCase();
  if (lower == F("rising")) return EdgeMode::Rising;
  if (lower == F("falling")) return EdgeMode::Falling;
  return EdgeMode::Both;
}

bool saveOnchangeRules() {
  File f = LittleFS.open(kRulesPath, "w");
  if (!f) return false;
  for (uint8_t i = 0; i < s_ruleCount; ++i) {
    if (!s_rules[i].active) continue;
    f.printf("%u %s\r\n", s_rules[i].pin, edgeModeToString(s_rules[i].edgeMode).c_str());
  }
  f.close();
  return true;
}

bool loadOnchangeRules() {
  File f = LittleFS.open(kRulesPath, "r");
  if (!f) return false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    uint8_t pin;
    String modeStr;
    int space = line.indexOf(' ');
    if (space > 0) {
      pin = line.substring(0, space).toInt();
      modeStr = line.substring(space + 1);
    } else {
      pin = line.toInt();
      modeStr = "both";
    }
    if (isPinValid(pin)) {
      registerOnchange(pin, stringToEdgeMode(modeStr), [](uint8_t p, bool l) {
        (void)p; (void)l;
      });
    }
  }
  f.close();
  return true;
}

void beginOnchange() {
  for (uint8_t i = 0; i < s_ruleCount; ++i) {
    s_rules[i].debounce = DebounceState{false, false, false, 0};
  }
  loadOnchangeRules();
}

void listOnchangeRules(Stream &output) {
  if (s_ruleCount == 0) {
    output.println(F("No onchange rules."));
    return;
  }
  output.println(F("Onchange rules:"));
  for (uint8_t i = 0; i < s_ruleCount; ++i) {
    if (!s_rules[i].active) continue;
    output.printf("  GPIO%u: %s\r\n", s_rules[i].pin, edgeModeToString(s_rules[i].edgeMode).c_str());
  }
}

ApiResult shellAddOnchange(const String &args, Stream &output) {
  String rest = args;
  String pinStr = rest.substring(0, rest.indexOf(' '));
  rest = rest.substring(pinStr.length());
  rest.trim();
  String modeStr = rest.substring(0, rest.indexOf(' '));
  pinStr.trim();
  modeStr.trim();

  uint8_t pin;
  if (pinStr.startsWith("D")) {
    pin = pinStr.substring(1).toInt();
  } else {
    pin = pinStr.toInt();
  }

  EdgeMode mode = stringToEdgeMode(modeStr);
  ApiResult result = registerOnchange(pin, mode, [](uint8_t p, bool l) {
    (void)p; (void)l;
  });
  if (!result.isError()) {
    saveOnchangeRules();
    output.printf("Added onchange rule: GPIO%u %s\r\n", pin, edgeModeToString(mode).c_str());
  }
  return result;
}

ApiResult shellRemoveOnchange(const String &args, Stream &output) {
  String pinStr = args;
  pinStr.trim();
  uint8_t pin;
  if (pinStr.startsWith("D")) {
    pin = pinStr.substring(1).toInt();
  } else {
    pin = pinStr.toInt();
  }

  ApiResult result = unregisterOnchange(pin);
  if (!result.isError()) {
    saveOnchangeRules();
    output.printf("Removed onchange rule: GPIO%u\r\n", pin);
  }
  return result;
}

}}
