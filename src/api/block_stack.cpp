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
  frames_[depth_++] = {true, running, true, lineStart, bodyStart, nowMs, 0,
                       false, false, false, 0};
  return nullptr;
}

const char *BlockStack::onTry(int catchStart) {
  lastJumpTarget_ = -1;
  if (depth_ == kMaxBlockDepth) return "Block nesting limit exceeded";
  if (catchStart < 0) return "try without catch";
  const bool running = !skipping();
  // seenElse doubles as "consumed" for try frames (skipped region or
  // broken-through): a consumed catch never runs and never catches.
  frames_[depth_++] = {false, running, !running, 0, 0, 0, 0,
                       true, false, false, catchStart};
  return nullptr;
}

const char *BlockStack::onCatch() {
  if (depth_ == 0) return "catch without try";
  Frame &top = frames_[depth_ - 1];
  if (!top.isTry) return "catch without try";
  if (top.inCatch) return nullptr;  // already entered by onError's jump
  top.inCatch = true;
  if (!top.errorCaught) {
    top.running = false;  // try body finished clean: skip the catch body
  }
  return nullptr;
}

bool BlockStack::onError(int &jumpTo) {
  jumpTo = -1;
  // Walk down without mutating first: an unhandled error must leave every
  // enclosing block intact (execution continues on the next line).
  for (size_t i = depth_; i-- > 0;) {
    const Frame &f = frames_[i];
    if (!f.isTry) continue;
    if (f.inCatch) continue;    // bubbles past catch-phase tries
    if (f.seenElse) continue;   // consumed (skipped or broken-through)
    // Caught: abandon every frame above it (their `end` lines never run).
    depth_ = i + 1;
    Frame &t = frames_[i];
    t.inCatch = true;
    t.errorCaught = true;
    jumpTo = t.catchStart;
    return true;
  }
  return false;
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
      if (frames_[depth_ - 1].isTry) return "else without matching if";
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
