#pragma once

#include <stddef.h>

namespace harixos {
namespace api {

constexpr size_t kMaxBlockDepth = 8;

enum class BlockEvent { If, Else, End, Other };

// Forward-only stack tracking whether the current script line is inside a
// live or skipped `if` block. Separate from ScriptDepthGuard's
// kMaxScriptDepth: that caps run recursion, this caps if nesting.
class BlockStack {
 public:
  bool skipping() const;
  size_t depth() const;
  const char *onEvent(BlockEvent ev, bool cond);  // nullptr = ok, else static error text
  int unclosedCount() const;                      // 0 when balanced
  void reset();

 private:
  struct Frame {
    bool running;
    bool seenElse;
  };

  Frame frames_[kMaxBlockDepth] = {};
  size_t depth_ = 0;
};

}  // namespace api
}  // namespace harixos
