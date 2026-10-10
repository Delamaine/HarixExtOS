#include "hostname.h"

namespace harixos {
namespace api {
namespace hostname {

bool valid(const char *name) {
  if (name == nullptr || name[0] == '\0') return false;
  unsigned len = 0;
  while (name[len] != '\0') {
    char c = name[len];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
          c == '.'))
      return false;
    if (c == '.' && (len == 0 || name[len - 1] == '.')) return false;
    if (++len > 63) return false;
  }
  return name[len - 1] != '.';
}

}  // namespace hostname
}  // namespace api
}  // namespace harixos
