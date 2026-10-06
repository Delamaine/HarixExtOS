#include "kernel/iot/relay.h"

#include <FS.h>
#include <LittleFS.h>
#include <Arduino.h>

#include "../../apps/settings/settings.h"
#include "../../api/gpio_api.h"
#include "kernel/iot/mqtt_service.h"

namespace harixos { namespace iot {

struct RelayEntry {
  char name[16];
  RelayState state;
};

static RelayEntry s_relays[kMaxRelays];
static bool s_inited[kMaxRelays] = {false};

static uint8_t findRelayIndex(const char* name) {
  for (uint8_t i = 0; i < kMaxRelays; ++i) {
    if (s_inited[i] && strcmp(s_relays[i].name, name) == 0) return i;
  }
  return 0xFF;
}

static bool hasActiveRelay() {
  for (uint8_t i = 0; i < kMaxRelays; ++i) {
    if (s_inited[i]) return true;
  }
  return false;
}

// Publish one relay's retained state topic (best-effort; MQTT may be down).
static void publishRelayState(uint8_t idx) {
  if (!isConnected()) return;
  String topic = shellSettings.mqttPrefix + String("/relay/") +
                 String(s_relays[idx].name) + "/state";
  char payload[8];
  relayStatePayload(s_relays[idx].state.level, payload, sizeof(payload));
  publishRaw(topic, String(payload), true);
}

void publishRelayStates() {
  for (uint8_t i = 0; i < kMaxRelays; ++i) {
    if (s_inited[i]) publishRelayState(i);
  }
}

ApiResult initRelay(uint8_t pin, const char* name, bool initialLevel) {
  if (name == nullptr || !relayValidName(name)) {
    return ApiResult(API_INVALID_ARGUMENT, "relay name: 1-15 chars [A-Za-z0-9_-]");
  }
  if (!harixos::api::GpioAPI::isAvailablePin(pin)) {
    return ApiResult(API_INVALID_PIN, "GPIO" + String(pin) + " is reserved or invalid");
  }
  for (uint8_t i = 0; i < kMaxRelays; ++i) {
    if (s_inited[i] && s_relays[i].state.pin == pin) {
      return ApiResult(API_DUPLICATE, "relay already registered for GPIO" + String(pin));
    }
  }
  if (findRelayIndex(name) < kMaxRelays) {
    return ApiResult(API_DUPLICATE, "relay already registered with name " + String(name));
  }
  for (uint8_t i = 0; i < kMaxRelays; ++i) {
    if (!s_inited[i]) {
      strncpy(s_relays[i].name, name, sizeof(s_relays[i].name) - 1);
      s_relays[i].name[sizeof(s_relays[i].name) - 1] = '\0';
      s_relays[i].state.pin = pin;
      s_relays[i].state.level = initialLevel;
      s_inited[i] = true;

      pinMode(pin, OUTPUT);
      digitalWrite(pin, initialLevel ? HIGH : LOW);
      // Runtime registration: make HA aware of the new entity/state right away.
      publishRelayState(i);
      invalidateDiscovery();
      return ApiResult(API_OK, "relay " + String(name) + " registered for GPIO" + String(pin));
    }
  }
  return ApiResult(API_ERROR, "Maximum relays reached (" + String(kMaxRelays) + ")");
}

ApiResult setRelay(const char* name, bool level) {
  uint8_t idx = findRelayIndex(name);
  if (idx >= kMaxRelays) {
    return ApiResult(API_INVALID_ARGUMENT, "no relay named " + String(name));
  }
  if (!relaySetLevel(s_relays[idx].state, level)) {
    return ApiResult(API_OK, "");
  }
  digitalWrite(s_relays[idx].state.pin, s_relays[idx].state.level ? HIGH : LOW);
  publishRelayState(idx);
  if (!saveRelayRules()) {
    return ApiResult(API_ERROR, "relay set but save failed");
  }
  return ApiResult(API_OK, "");
}

void listRelays(Stream &out) {
  if (!hasActiveRelay()) {
    out.println(F("No relays."));
    return;
  }
  out.println(F("Relays:"));
  for (uint8_t i = 0; i < kMaxRelays; ++i) {
    if (!s_inited[i]) continue;
    out.printf("  %s: GPIO%u, %s\r\n", s_relays[i].name, s_relays[i].state.pin,
               s_relays[i].state.level ? "on" : "off");
  }
}

String collectRelayNames() {
  String names;
  for (uint8_t i = 0; i < kMaxRelays; ++i) {
    if (!s_inited[i]) continue;
    if (names.length() > 0) names += F(",");
    names += String(s_relays[i].name);
  }
  return names;
}

ApiResult runCommand(const String &args, Stream &out) {
  String rest = args;
  int firstSpace = rest.indexOf(' ');
  String action = (firstSpace > 0) ? rest.substring(0, firstSpace) : rest;
  action.trim();
  action.toLowerCase();

  if (action == F("list") || action.length() == 0) {
    listRelays(out);
    return ApiResult(API_OK, "");
  }
  if (action == F("add")) {
    // add <pin> <name> [on|off]
    String rest2 = rest.substring(firstSpace + 1);
    int sp = rest2.indexOf(' ');
    String pinStr = (sp > 0) ? rest2.substring(0, sp) : rest2;
    pinStr.trim();
    rest2 = (sp > 0) ? rest2.substring(sp + 1) : String();
    rest2.trim();
    int sp2 = rest2.indexOf(' ');
    String name = (sp2 > 0) ? rest2.substring(0, sp2) : rest2;
    name.trim();
    String levelStr = (sp2 > 0) ? rest2.substring(sp2 + 1) : String();
    levelStr.trim();
    levelStr.toLowerCase();
    if (pinStr.length() == 0 || name.length() == 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: relay add <pin> <name> [on|off]");
    }
    bool initial = false;
    if (levelStr.length() > 0 && !relayParseLevel(levelStr.c_str(), &initial)) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: relay add <pin> <name> [on|off]");
    }
    ApiResult result = initRelay((uint8_t)pinStr.toInt(), name.c_str(), initial);
    if (!result.isError() && !saveRelayRules()) {
      return ApiResult(API_ERROR, "relay registered but save failed");
    }
    return result;
  }
  if (action == F("set")) {
    String name = rest.substring(firstSpace + 1);
    int nameEnd = name.indexOf(' ');
    String nameStr = (nameEnd > 0) ? name.substring(0, nameEnd) : name;
    nameStr.trim();
    String levelStr = (nameEnd > 0) ? name.substring(nameEnd + 1) : String();
    levelStr.trim();
    levelStr.toLowerCase();
    bool level;
    if (nameStr.length() == 0 ||
        !relayParseLevel(levelStr.c_str(), &level)) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: relay set <name> on|off");
    }
    return setRelay(nameStr.c_str(), level);
  }
  if (action == F("toggle")) {
    String name = rest.substring(firstSpace + 1);
    name.trim();
    if (name.length() == 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: relay toggle <name>");
    }
    uint8_t idx = findRelayIndex(name.c_str());
    if (idx >= kMaxRelays) {
      return ApiResult(API_INVALID_ARGUMENT, "no relay named " + String(name));
    }
    return setRelay(name.c_str(), !s_relays[idx].state.level);
  }
  if (action == F("status")) {
    String name = rest.substring(firstSpace + 1);
    name.trim();
    if (name.length() == 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: relay status <name>");
    }
    uint8_t idx = findRelayIndex(name.c_str());
    if (idx < kMaxRelays) {
      out.printf("%s: %s\r\n", s_relays[idx].name,
                 s_relays[idx].state.level ? "on" : "off");
    } else {
      out.println("no relay named " + name);
    }
    return ApiResult(API_OK, "");
  }
  return ApiResult(API_INVALID_ARGUMENT,
                   "Usage: relay [add <pin> <name> [on|off]|set <name> on|off|toggle <name>|status <name>|list]");
}

bool saveRelayRules() {
  File f = LittleFS.open(kRelaysPath, "w");
  if (!f) return false;
  for (uint8_t i = 0; i < kMaxRelays; ++i) {
    if (!s_inited[i]) continue;
    char line[32];
    relayPersistLine(s_relays[i].state.pin, s_relays[i].state.level, line, sizeof(line));
    f.printf("%s %s\r\n", line, s_relays[i].name);
  }
  f.close();
  return true;
}

bool loadRelayRules() {
  File f = LittleFS.open(kRelaysPath, "r");
  if (!f) return false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int firstSpace = line.indexOf(' ');
    if (firstSpace <= 0) continue;
    uint8_t pin = line.substring(0, firstSpace).toInt();
    String rest = line.substring(firstSpace + 1);
    int secondSpace = rest.indexOf(' ');
    if (secondSpace <= 0) continue;
    int level = rest.substring(0, secondSpace).toInt();
    char name[16];
    strncpy(name, rest.substring(secondSpace + 1).c_str(), sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    if (harixos::api::GpioAPI::isAvailablePin(pin)) {
      initRelay(pin, name, level != 0);
    }
  }
  f.close();
  return true;
}

void beginRelay() {
  loadRelayRules();
}

}}