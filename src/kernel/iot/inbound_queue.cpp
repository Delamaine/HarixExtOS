#include "kernel/iot/inbound_queue.h"

#include <cstring>

namespace harixos { namespace iot {

bool InboundQueue::push(const char *line, size_t len) {
  if (len == 0 || len >= kLineMax) return false;
  if (count_ == kCapacity) {
    head_ = (head_ + 1) % kCapacity;
    count_--;
    dropped_++;
  }
  memcpy(lines_[tail_], line, len);
  lines_[tail_][len] = '\0';
  tail_ = (tail_ + 1) % kCapacity;
  count_++;
  return true;
}

bool InboundQueue::pop(char *out) {
  if (count_ == 0) return false;
  memcpy(out, lines_[head_], kLineMax);
  head_ = (head_ + 1) % kCapacity;
  count_--;
  return true;
}

size_t InboundQueue::size() const { return count_; }

uint32_t InboundQueue::dropped() const { return dropped_; }

void InboundQueue::reset() {
  head_ = 0;
  tail_ = 0;
  count_ = 0;
  dropped_ = 0;
}

InboundQueue &inbound() {
  static InboundQueue instance;
  return instance;
}

}}
