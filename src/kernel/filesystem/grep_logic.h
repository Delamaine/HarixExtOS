#pragma once

#include <stddef.h>

namespace harixos {
namespace grep {

// True when `line` contains `pattern` as a literal substring (never a
// regex), honouring case-insensitivity and inversion the way grep -i/-v
// combine. Empty pattern matches every line.
bool lineMatches(const char *line, const char *pattern, bool icase,
                 bool invert);

}  // namespace grep
}  // namespace harixos
