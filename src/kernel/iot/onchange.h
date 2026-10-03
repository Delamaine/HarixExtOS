#ifndef HARIXOS_ONCHANGE_H
#define HARIXOS_ONCHANGE_H

#include <Arduino.h>
#include <functional>

#include "kernel/iot/onchange_logic.h"
#include "api/api_types.h"

namespace harixos { namespace iot {

using harixos::api::ApiResult;

constexpr uint8_t kMaxOnchangeRules = 8;
constexpr const char* kRulesPath = "/onchange.rules";

struct OnchangeRule {
  uint8_t pin;
  EdgeMode edgeMode;
  DebounceState debounce;
  std::function<void(uint8_t pin, bool level)> callback;
  bool active;
};

// Register a callback for a pin with given edge mode.
// Returns API_OK on success, API_INVALID_ARGUMENT on duplicate/invalid.
ApiResult registerOnchange(uint8_t pin, EdgeMode edgeMode,
                           std::function<void(uint8_t pin, bool level)> callback);

// Remove registration for a pin.
ApiResult unregisterOnchange(uint8_t pin);

// Check if a pin has an active onchange rule.
bool hasOnchangeRule(uint8_t pin);

// Get edge mode for a pin (returns Both if not found).
EdgeMode getEdgeMode(uint8_t pin);

// ISR-friendly: feed a raw level sample for a pin.
void feedOnchange(uint8_t pin, bool rawLevel, uint32_t nowMs);

// Polling update: check all rules and invoke callbacks on edge match.
void updateOnchange();

// Persistence: save rules to filesystem.
bool saveOnchangeRules();

// Persistence: load rules from filesystem.
bool loadOnchangeRules();

// Begin onchange subsystem (initialize all debounce states).
void beginOnchange();

// List active onchange rules (for shell).
void listOnchangeRules(Stream &output);

// Add rule via shell (parses "D2 rising" or "4 falling" etc).
ApiResult shellAddOnchange(const String &args, Stream &output);

// Remove rule via shell (parses "D2" or "4").
ApiResult shellRemoveOnchange(const String &args, Stream &output);

}}

#endif
