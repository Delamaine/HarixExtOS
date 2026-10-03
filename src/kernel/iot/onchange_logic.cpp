#include "kernel/iot/onchange_logic.h"

namespace harixos { namespace iot {

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
