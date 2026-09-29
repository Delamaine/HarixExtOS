#pragma once

#include <stddef.h>
#include <stdint.h>

namespace harixos {
namespace api {
namespace cron {

// Bitmask form of a 6-field cron expression, built once at parse time so the
// scheduler can match a tick in a few shifts rather than re-parsing text.
//
// Field order and ranges (spec 7.1):
//   sec 0-59 | min 0-59 | hour 0-23 | dom 1-31 | month 1-12 | dow 0-6
// dow 0 is Sunday. dom and dow are AND'ed by matches(), which deliberately
// deviates from standard cron's OR.
struct Spec {
  uint64_t sec;         // bit n = second n
  uint64_t minute;      // bit n = minute n
  uint32_t hour;        // bit n = hour n
  uint32_t dom;         // bit n = day-of-month n (bits 1..31)
  uint16_t month;       // bit n = month n (bits 1..12)
  uint8_t  dow;         // bit n = weekday n (bits 0..6, 0 = Sunday)
  bool domRestricted;   // true when the dom field was not a bare '*'
  bool dowRestricted;
};

// Returns nullptr on success, else a static message.
// *badField receives the 0-based failing field index, or -1 if the failure
// is not field-specific (wrong count). On failure `out` is left untouched,
// so a caller may treat it as undefined without clearing it first.
const char *parse(const char *expression, Spec &out, int *badField);

}  // namespace cron
}  // namespace api
}  // namespace harixos
