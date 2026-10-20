#pragma once

#include <stddef.h>
#include <stdint.h>

namespace harixos {
namespace api {

constexpr size_t kMaxBlockDepth = 8;

// while-loop guards. Checked at each loop-back (the `end` of a running
// while frame). Iterations bounds tight no-delay loops; wall time bounds
// delay-containing loops that would otherwise freeze shell, web, and
// scheduler until reboot.
constexpr uint32_t kMaxWhileIterations = 100000;
constexpr uint32_t kMaxWhileMs = 5 * 60 * 1000;

enum class BlockEvent { If, Else, End, Other };

// Forward-only stack tracking whether the current script line is inside a
// live or skipped `if`/`while` block. Separate from ScriptDepthGuard's
// kMaxScriptDepth: that caps run recursion, this caps block nesting.
// Deliberately expression- and clock-free: the engine evaluates conditions
// and passes `nowMs` in, so this file stays host-testable.
class BlockStack {
 public:
  bool skipping() const;
  size_t depth() const;
  // if/else/end. For a running while frame, `cond` is the engine's
  // re-evaluation of the while condition: true keeps the frame and arms
  // lastJumpTarget() at its bodyStart, false pops it. Caps are enforced
  // here; exceeding one pops the frame and returns static error text.
  const char *onEvent(BlockEvent ev, bool cond, uint32_t nowMs = 0);  // nullptr = ok
  const char *onWhile(int lineStart, int bodyStart, bool cond, uint32_t nowMs = 0);
  const char *onBreak();                          // nullptr = ok
  // try/catch. `catchStart` is the engine's forward-scan offset of the
  // matching `catch` line (negative = try without catch, an error). The
  // catch body runs only if an error was armed between try and catch;
  // otherwise onCatch flips the frame to skip mode (else-style).
  const char *onTry(int catchStart);
  const char *onCatch();                          // nullptr = ok
  // Raise: arm the nearest enclosing try (skipping past tries already in
  // their catch phase and consumed ones) and jump to its catch line.
  // Abandons frames above it. No try found = no mutation, returns false.
  bool onError(int &jumpTo);
  bool topIsWhile() const;
  bool topRunning() const;
  int topLineStart() const;
  int lastJumpTarget() const;                     // -1 = none; cleared by each new event
  int unclosedCount() const;                      // 0 when balanced
  void reset();

 private:
  struct Frame {
    bool isWhile;
    bool running;
    bool seenElse;     // also: consumed (try skipped/broken-through)
    int lineStart;      // while only: offset of the `while` line in the script
    int bodyStart;      // while only: loop-back target (line after the while)
    uint32_t startedAt;  // while only: nowMs at first entry
    uint32_t iterations;  // while only: completed loop-backs
    bool isTry;
    bool inCatch;       // try only: catch phase entered (normally or by error)
    bool errorCaught;   // try only: onError armed this frame
    int catchStart;     // try only: offset of the matching `catch` line
  };

  Frame frames_[kMaxBlockDepth] = {};
  size_t depth_ = 0;
  int lastJumpTarget_ = -1;
};

}  // namespace api
}  // namespace harixos
