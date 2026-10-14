#include "kernel/iot/onchange_logic.h"

#include <string.h>

namespace harixos { namespace iot {

static const char* const kClassTokens[] = {"motion", "occupancy", "door", "window", "moisture"};

bool parseBinaryClass(const char *s, BinaryClass &out) {
  if (!s) return false;
  for (int i = 0; i < 5; ++i) {
    if (strcmp(s, kClassTokens[i]) == 0) {
      out = (BinaryClass)i;
      return true;
    }
  }
  return false;
}

const char *binaryClassToken(BinaryClass cls) {
  uint8_t i = (uint8_t)cls;
  return i < 5 ? kClassTokens[i] : kClassTokens[0];
}

bool feedEdge(DebounceState &s, bool rawLevel, uint32_t nowMs, bool *newLevel) {
  if (!s.initialized) {
    s.previousRaw = s.stableLevel = rawLevel;
    s.lastTransitionMs = nowMs;
    s.initialized = true;
    return false;
  }
  if (rawLevel != s.previousRaw) {
    s.previousRaw = rawLevel;
    s.lastTransitionMs = nowMs;
  }
  if (rawLevel != s.stableLevel &&
      (uint32_t)(nowMs - s.lastTransitionMs) >= kDebounceMs) {
    s.stableLevel = rawLevel;
    *newLevel = rawLevel;
    return true;
  }
  return false;
}

bool edgeMatches(EdgeMode mode, bool oldLevel, bool newLevel) {
  switch (mode) {
    case EdgeMode::Rising:  return !oldLevel && newLevel;
    case EdgeMode::Falling: return oldLevel && !newLevel;
    case EdgeMode::Both:    return oldLevel != newLevel;
  }
  return false;
}
}}
