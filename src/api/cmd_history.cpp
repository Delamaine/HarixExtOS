#include "cmd_history.h"

#include <stdio.h>
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

const char *at(size_t n) {
  if (n < 1 || n > s_count) {
    return nullptr;
  }
  return get(s_count - n + 1);
}

size_t count() {
  return s_count;
}

namespace {
void copyLine(const char *src, char *out, size_t outSize) {
  strncpy(out, src, outSize - 1);
  out[outSize - 1] = '\0';
}
}  // namespace

bool expand(const char *line, char *out, size_t outSize) {
  if (line == nullptr || out == nullptr || outSize == 0) {
    return false;
  }
  if (line[0] != '!') {
    copyLine(line, out, outSize);
    return true;
  }
  // Whole-line "!<digits>" only — "!5" expands, "!5 x" and "!" stay literal.
  const char *p = line + 1;
  unsigned long n = 0;
  bool allDigits = (*p != '\0');
  for (; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') {
      allDigits = false;
      break;
    }
    if (n < 1000000) {  // saturate absurd digit runs instead of overflowing
      n = n * 10 + (unsigned long)(*p - '0');
    }
  }
  if (!allDigits) {
    copyLine(line, out, outSize);
    return true;
  }
  const char *entry = at((size_t)n);
  if (entry == nullptr) {
    snprintf(out, outSize, "no history entry %lu", n);
    return false;
  }
  copyLine(entry, out, outSize);
  return true;
}

bool canExec(const char *line) {
  if (line == nullptr) {
    return false;
  }
  while (*line == ' ' || *line == '\t') {
    ++line;
  }
  if (line[0] == '!') {
    return false;
  }
  return strncasecmp(line, "exec ", 5) != 0;
}

}  // namespace history
}  // namespace api
}  // namespace harixos
