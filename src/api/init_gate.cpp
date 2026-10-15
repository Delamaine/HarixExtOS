#include "init_gate.h"

#include <string.h>

namespace harixos {
namespace api {
namespace {

constexpr size_t kMaxInitPaths = 8;
constexpr size_t kMaxPathLen = 40;
char s_done[kMaxInitPaths][kMaxPathLen];
size_t s_count = 0;

}  // namespace

bool initAlreadyDone(const char *path) {
  if (path == nullptr) return false;
  for (size_t i = 0; i < s_count; ++i) {
    if (strcmp(s_done[i], path) == 0) return true;
  }
  return false;
}

void markInitDone(const char *path) {
  if (path == nullptr || path[0] == '\0' || s_count >= kMaxInitPaths) return;
  if (initAlreadyDone(path)) return;
  strncpy(s_done[s_count], path, kMaxPathLen - 1);
  s_done[s_count][kMaxPathLen - 1] = '\0';
  ++s_count;
}

void clearInitDone() { s_count = 0; }

}  // namespace api
}  // namespace harixos
