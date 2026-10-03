#ifndef HARIXOS_INBOUND_QUEUE_H
#define HARIXOS_INBOUND_QUEUE_H

#include <stddef.h>
#include <stdint.h>

namespace harixos { namespace iot {
class InboundQueue {
 public:
  static constexpr size_t kLineMax = 80;   // incl. NUL -> 79 usable chars
  static constexpr size_t kCapacity = 4;
  bool push(const char *line, size_t len); // false if len==0 || len>=kLineMax
  bool pop(char *out);                     // out[kLineMax], false when empty
  size_t size() const;
  uint32_t dropped() const;                // overflow count since reset()
  void reset();

 private:
  char lines_[kCapacity][kLineMax] = {};
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t count_ = 0;
  uint32_t dropped_ = 0;
};
InboundQueue &inbound();                   // process-wide singleton accessor
}}

#endif
