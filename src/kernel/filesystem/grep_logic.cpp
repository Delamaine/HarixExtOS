#include "grep_logic.h"

#include <ctype.h>
#include <string.h>

namespace harixos {
namespace grep {

// Case-insensitive strstr: returns the first occurrence of needle in hay,
// or nullptr. Portability: strcasestr is not in ESP8266's newlib.
static const char *findNoCase(const char *hay, const char *needle) {
  if (*needle == '\0') {
    return hay;
  }
  for (; *hay != '\0'; ++hay) {
    const char *h = hay;
    const char *n = needle;
    while (*n != '\0' && *h != '\0' &&
           tolower(static_cast<unsigned char>(*h)) ==
               tolower(static_cast<unsigned char>(*n))) {
      ++h;
      ++n;
    }
    if (*n == '\0') {
      return hay;
    }
  }
  return nullptr;
}

bool lineMatches(const char *line, const char *pattern, bool icase,
                 bool invert) {
  if (line == nullptr || pattern == nullptr) {
    return false;
  }
  const bool found =
      icase ? (findNoCase(line, pattern) != nullptr)
            : (strstr(line, pattern) != nullptr);
  return found != invert;
}

}  // namespace grep
}  // namespace harixos
