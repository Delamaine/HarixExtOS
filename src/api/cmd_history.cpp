#include "cmd_history.h"

#include <string.h>

namespace harixos {
namespace api {
namespace history {

namespace {
char s_lines[kCapacity][kMaxLine + 1];
size_t s_head = 0;   // next write slot
size_t s_count = 0;
}  // namespace

void reset() {
  s_head = 0;
  s_count = 0;
}

void push(const char *line) {
  if (line == nullptr || line[0] == '\0') {
    return;
  }
  if (s_count > 0) {
    size_t newest = (s_head + kCapacity - 1) % kCapacity;
    if (strcmp(s_lines[newest], line) == 0) {
      return;
    }
  }
  strncpy(s_lines[s_head], line, kMaxLine);
  s_lines[s_head][kMaxLine] = '\0';
  s_head = (s_head + 1) % kCapacity;
  if (s_count < kCapacity) {
    ++s_count;
  }
}

const char *get(size_t stepsBack) {
  if (stepsBack == 0 || stepsBack > s_count) {
    return nullptr;
  }
  size_t idx = (s_head + kCapacity - stepsBack) % kCapacity;
  return s_lines[idx];
}

size_t count() {
  return s_count;
}

}  // namespace history
}  // namespace api
}  // namespace harixos
