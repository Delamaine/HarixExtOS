#include "block_stack.h"

namespace harixos {
namespace api {

bool BlockStack::skipping() const {
  return depth_ > 0 && !frames_[depth_ - 1].running;
}

size_t BlockStack::depth() const {
  return depth_;
}

const char *BlockStack::onEvent(BlockEvent ev, bool cond) {
  switch (ev) {
    case BlockEvent::If:
      if (depth_ == kMaxBlockDepth) return "Block nesting limit exceeded";
      if (skipping()) {
        // Inside a skipped branch the condition is irrelevant; mark the
        // frame consumed so a later `else` cannot flip the outer if.
        frames_[depth_++] = {false, true};
      } else {
        frames_[depth_++] = {cond, false};
      }
      return nullptr;

    case BlockEvent::Else:
      if (depth_ == 0) return "else without matching if";
      if (frames_[depth_ - 1].seenElse) return nullptr;
      frames_[depth_ - 1].running = !frames_[depth_ - 1].running;
      frames_[depth_ - 1].seenElse = true;
      return nullptr;

    case BlockEvent::End:
      if (depth_ == 0) return "end without matching if";
      --depth_;
      return nullptr;

    case BlockEvent::Other:
    default:
      return nullptr;
  }
}

int BlockStack::unclosedCount() const {
  return (int)depth_;
}

void BlockStack::reset() {
  depth_ = 0;
}

}  // namespace api
}  // namespace harixos
