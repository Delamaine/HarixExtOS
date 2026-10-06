#ifndef HARIXOS_RELAY_H
#define HARIXOS_RELAY_H

#include <Arduino.h>

#include "kernel/iot/relay_logic.h"
#include "api/api_types.h"

namespace harixos { namespace iot {

using harixos::api::ApiResult;
using harixos::api::API_OK;
using harixos::api::API_ERROR;
using harixos::api::API_INVALID_PIN;
using harixos::api::API_INVALID_ARGUMENT;
using harixos::api::API_DUPLICATE;

constexpr uint8_t kMaxRelays = 4;
constexpr const char* kRelaysPath = "/relays.conf";

// Begin relay subsystem: load persisted relays and restore outputs.
void beginRelay();

// Register a relay on a pin with a name. initialLevel restores persisted state.
ApiResult initRelay(uint8_t pin, const char* name, bool initialLevel = false);

// Set a relay by name to a level. Publishes state and persists.
ApiResult setRelay(const char* name, bool level);

// Publish retained current state for every registered relay (MQTT connect
// hook): a level changed while disconnected must reach HA on reconnect.
void publishRelayStates();

// List relays (for shell).
void listRelays(Stream &out);

// Collect relay names for discovery (comma-separated).
String collectRelayNames();

// Shell command parser: "add ..." / "set <name> on|off|toggle|status|list".
ApiResult runCommand(const String &args, Stream &out);

// Persist relay rules to filesystem.
bool saveRelayRules();

// Load relay rules from filesystem.
bool loadRelayRules();

}}

#endif
