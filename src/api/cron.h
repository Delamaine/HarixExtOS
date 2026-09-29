#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

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

// timeinfo comes from localtime_r(). Returns true on a match.
// dom and dow are AND'ed, not OR'ed as in standard cron: a field that was
// written as a bare '*' restricts nothing and so always counts as a match,
// but when both fields are restricted both must hold.
bool matches(const Spec &spec, const struct tm &timeinfo);

// Budget for one persisted "<expression> <command>\n" line. scheduler.cpp
// cannot see main.cpp's anonymous-namespace line limit, so the save-format
// budget lives with the module that defines the format.
constexpr size_t kMaxSaveLine = 192;

// Splits "<6 cron fields> <command...>" into its two halves.
// cronOut receives the first six whitespace-separated fields, normalised to
// single spaces. *commandOut points into `line` at the text after field 6,
// so the caller must keep `line` alive for as long as it uses the command.
// Returns nullptr on success, else "expected 6 fields" / "missing command".
const char *splitLine(const char *line, char *cronOut, size_t cronCap,
                      const char **commandOut);

// The exact inverse, so the line format has one definition. Writes
// "<expression> <command>" plus a trailing '\n' as load() expects.
// Returns nullptr, or "line too long" if it would not fit in cap.
const char *makeLine(const char *expression, const char *command, char *out,
                     size_t cap);

}  // namespace cron
}  // namespace api
}  // namespace harixos
