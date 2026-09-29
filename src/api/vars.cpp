#include "vars.h"

#include <cstring>

namespace harixos {
namespace api {
namespace vars {
namespace {

struct Slot {
  char name[kMaxNameLength + 1];
  double value;
  bool used;
};

Slot slots[kMaxVars];

int findIndex(const char *name) {
  if (name == nullptr) return -1;
  for (size_t i = 0; i < kMaxVars; ++i) {
    if (slots[i].used && strncmp(slots[i].name, name, kMaxNameLength + 1) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int firstFreeIndex() {
  for (size_t i = 0; i < kMaxVars; ++i) {
    if (!slots[i].used) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace

bool isValidName(const char *name) {
  if (name == nullptr || name[0] == '\0') return false;

  const size_t len = strlen(name);
  if (len > kMaxNameLength) return false;

  const char c0 = name[0];
  const bool firstOk = (c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || c0 == '_';
  if (!firstOk) return false;

  for (size_t i = 1; i < len; ++i) {
    const char c = name[i];
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '_';
    if (!ok) return false;
  }
  return true;
}

bool set(const char *name, double value) {
  if (!isValidName(name)) return false;

  int idx = findIndex(name);
  if (idx < 0) {
    idx = firstFreeIndex();
    if (idx < 0) return false;
    strncpy(slots[idx].name, name, kMaxNameLength);
    slots[idx].name[kMaxNameLength] = '\0';
    slots[idx].used = true;
  }
  slots[idx].value = value;
  return true;
}

bool get(const char *name, double &out) {
  const int idx = findIndex(name);
  if (idx < 0) return false;
  out = slots[idx].value;
  return true;
}

void clear() {
  for (size_t i = 0; i < kMaxVars; ++i) {
    slots[i].used = false;
    slots[i].name[0] = '\0';
    slots[i].value = 0;
  }
}

size_t count() {
  size_t n = 0;
  for (size_t i = 0; i < kMaxVars; ++i) {
    if (slots[i].used) ++n;
  }
  return n;
}

const char *nameAt(size_t index) {
  if (index >= kMaxVars || !slots[index].used) return "";
  return slots[index].name;
}

double valueAt(size_t index) {
  if (index >= kMaxVars) return 0;
  return slots[index].value;
}

}  // namespace vars
}  // namespace api
}  // namespace harixos
