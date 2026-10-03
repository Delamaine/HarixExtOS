#ifndef HARIXOS_ONCHANGE_LOGIC_H
#define HARIXOS_ONCHANGE_LOGIC_H

#include <stdint.h>

namespace harixos { namespace iot {
enum class EdgeMode : uint8_t { Rising, Falling, Both };
struct DebounceState {
  bool initialized;
  bool previousRaw;
  bool stableLevel;
  uint32_t lastTransitionMs;
};
constexpr uint32_t kDebounceMs = 50;
// Feeds one raw sample. Returns true exactly when the *stable* level
// changes (raw held differs from stable for kDebounceMs), then
// *newLevel holds the new stable level.
bool feedEdge(DebounceState &s, bool rawLevel, uint32_t nowMs, bool *newLevel);
// True when a stable-level change from oldLevel to newLevel satisfies mode.
bool edgeMatches(EdgeMode mode, bool oldLevel, bool newLevel);
}}

#endif
