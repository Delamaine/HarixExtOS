#pragma once

#include <stdint.h>

namespace harixos {
namespace api {
namespace tz {

// Mapping entry: IANA region name -> POSIX TZ string + UTC offset.
struct TzEntry {
  const char* iana;       // e.g., "Pacific/Auckland"
  const char* posix;      // e.g., "+12|-12,M9.5.0/M4.1.0/3"
  int32_t offsetMinutes;  // standard (non-DST) offset in minutes
};

// Returns the POSIX TZ string for an IANA region name, normalized to the
// form newlib's tzset() accepts (alphabetic zone name, ',' between DST
// rules). If the input is already a POSIX/offset string it is normalized
// too. Returns "UTC0" for unknown IANA names.
const char* toPosix(const char* ianaName);

// Returns the standard (non-DST) UTC offset in minutes for an IANA region
// name or a POSIX/offset string, using the human sign convention
// (UTC+12 -> 720). Returns 0 for unknown names.
int32_t offsetMinutes(const char* ianaName);

// Returns true if the given string is a POSIX TZ string rather than an IANA
// region name (IANA names contain '/' without ',').
bool isPosix(const char* s);

// Returns the index of the entry in the table, or -1 if not found.
int findIndex(const char* ianaName);

}  // namespace tz
}  // namespace api
}  // namespace harixos
