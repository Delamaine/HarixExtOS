#include "block_stack.h"

namespace harixos {
namespace api {

bool BlockStack::skipping() const {
  return depth_ > 0 && !frames_[depth_ - 1].running;
}

size_t BlockStack::depth() const {
  return depth_;
}

bool BlockStack::topIsWhile() const {
  return depth_ > 0 && frames_[depth_ - 1].isWhile;
}

bool BlockStack::topRunning() const {
  return depth_ > 0 && frames_[depth_ - 1].running;
}

int BlockStack::topLineStart() const {
  return depth_ == 0 ? -1 : frames_[depth_ - 1].lineStart;
}

int BlockStack::lastJumpTarget() const {
  return lastJumpTarget_;
}

const char *BlockStack::onWhile(int lineStart, int bodyStart, bool cond,
                                uint32_t nowMs) {
  lastJumpTarget_ = -1;
  if (depth_ == kMaxBlockDepth) return "Block nesting limit exceeded";
  const bool running = !skipping() && cond;
  frames_[depth_++] = {true, running, true, lineStart, bodyStart, nowMs, 0};
  return nullptr;
}

const char *BlockStack::onBreak() {
  lastJumpTarget_ = -1;
  for (size_t i = depth_; i-- > 0;) {
    if (!frames_[i].isWhile) continue;
    // Keep every frame above the while: their `end` lines still lie ahead
    // and each must pop its own frame. Mark them not-running so the normal
    // forward-skip walks them in order; seenElse stops a later `else` from
    // reviving a skipped branch on the way out.
    for (size_t k = i; k < depth_; ++k) {
      frames_[k].running = false;
      frames_[k].seenElse = true;
    }
    return nullptr;
  }
  return "break outside while loop";
}

const char *BlockStack::onEvent(BlockEvent ev, bool cond, uint32_t nowMs) {
  lastJumpTarget_ = -1;
  switch (ev) {
    case BlockEvent::If:
      if (depth_ == kMaxBlockDepth) return "Block nesting limit exceeded";
      if (skipping()) {
        // Inside a skipped branch the condition is irrelevant; mark the
        // frame consumed so a later `else` cannot flip the outer if.
        frames_[depth_++] = {false, false, true, 0, 0, 0, 0};
      } else {
        frames_[depth_++] = {false, cond, false, 0, 0, 0, 0};
      }
      return nullptr;

    case BlockEvent::Else:
      if (depth_ == 0) return "else without matching if";
      if (frames_[depth_ - 1].isWhile) return "else without matching if";
      if (frames_[depth_ - 1].seenElse) return nullptr;
      frames_[depth_ - 1].running = !frames_[depth_ - 1].running;
      frames_[depth_ - 1].seenElse = true;
      return nullptr;

    case BlockEvent::End: {
      if (depth_ == 0) return "end without matching if";
      Frame &top = frames_[depth_ - 1];
      if (!top.isWhile) {
        --depth_;
        return nullptr;
      }
      if (!top.running) {
        // Skipped while: swallow the end like a skipped if.
        --depth_;
        return nullptr;
      }
      if (!cond) {
        // Condition went false: loop finishes.
        --depth_;
        return nullptr;
      }
      // Loop back: enforce caps, keep the frame, arm the jump.
      ++top.iterations;
      if (top.iterations > kMaxWhileIterations) {
        --depth_;
        return "while loop iteration limit exceeded";
      }
      if ((uint32_t)(nowMs - top.startedAt) > kMaxWhileMs) {
        --depth_;
        return "while loop time limit exceeded";
      }
      lastJumpTarget_ = top.bodyStart;
      return nullptr;
    }

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
  lastJumpTarget_ = -1;
}

}  // namespace api
}  // namespace harixos
