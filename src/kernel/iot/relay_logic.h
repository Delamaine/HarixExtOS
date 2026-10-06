#ifndef HARIXOS_RELAY_LOGIC_H
#define HARIXOS_RELAY_LOGIC_H

#include <stdint.h>
#include <cstddef>

namespace harixos { namespace iot {

// Native-testable relay core (no Arduino dependency).
//
// A relay is a latched switch: once set on/off, it holds that level until
// told otherwise. The shell/Arduino layer owns the pool, persistence and
// GPIO; this file holds only the state transitions so they can be unit
// tested without a board.

// One relay's state.
struct RelayState {
  uint8_t pin;
  bool level;
};

// Apply a target level. Returns true only when the level actually changed.
bool relaySetLevel(RelayState &r, bool level);

// Parse a level string for `relay set <name> <level>` ("on"/"off").
// Returns false on anything else so typos never silently set a level.
bool relayParseLevel(const char *s, bool *out);

// Relay names land in MQTT topics and HA discovery ids: 1..15 chars of
// [A-Za-z0-9_-] only. Returns false otherwise.
bool relayValidName(const char *s);

// Serialize the on/off payload ("on"/"off") for `out`.
void relayStatePayload(bool level, char *out, size_t n);

// Serialize the persisted line "pin level" (level = 0/1) for `out`.
void relayPersistLine(uint8_t pin, bool level, char *out, size_t n);

}}

#endif
