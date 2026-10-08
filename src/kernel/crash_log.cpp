#include "crash_log.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace harixos {

bool crashReasonIsAbnormal(uint32_t reasonCode) {
  return reasonCode >= 1 && reasonCode <= 3;
}

int crashBootCount(const char* existing) {
  if (existing == nullptr) return 0;
  const char* found = nullptr;
  const char* p = existing;
  while ((p = strstr(p, "boots=")) != nullptr) {
    found = p;
    p += 6;  // strlen("boots=")
  }
  if (found == nullptr) return 0;
  return atoi(found + 6);
}

int crashFormatLine(char* out, size_t n, const char* reasonStr, uint32_t exccause,
                    uint32_t epc1, uint32_t excvaddr, uint32_t depc, int boots) {
  if (out == nullptr || n == 0) return -1;
  int r = snprintf(out, n,
                   "%s|%" PRIu32 "|0x%" PRIX32 "|0x%" PRIX32 "|0x%" PRIX32
                   "|boots=%d",
                   reasonStr, exccause, epc1, excvaddr, depc, boots);
  return (r < 0 || (size_t)r >= n) ? -1 : r;
}

int crashAppendLine(char* out, size_t n, const char* existing, const char* line) {
  if (out == nullptr || n == 0) return -1;
  out[0] = '\0';
  if (line == nullptr) return -1;
  if (existing == nullptr) existing = "";

  size_t exLen = strlen(existing);
  size_t lineLen = strlen(line);
  bool needNl = exLen > 0 && existing[exLen - 1] != '\n';

  // Newline count of the final content: those in existing, plus the
  // terminator for a partial last line, plus the new line itself.
  int totalNl = 0;
  for (size_t i = 0; i < exLen; ++i) {
    if (existing[i] == '\n') ++totalNl;
  }
  if (needNl) ++totalNl;
  ++totalNl;  // the appended line

  // Drop oldest lines beyond the cap (only existing contributes them).
  int drop = totalNl - (int)kCrashMaxLines;
  size_t start = 0;
  if (drop > 0) {
    for (int i = 0; i < drop && start < exLen; ++i) {
      while (start < exLen && existing[start] != '\n') ++start;
      if (start < exLen) ++start;
    }
  }

  size_t endLen = (exLen - start) + (needNl ? 1 : 0) + lineLen + 1;  // + '\n'
  if (endLen + 1 > n) return -1;  // no room for the NUL

  size_t pos = 0;
  memcpy(out + pos, existing + start, exLen - start);
  pos += exLen - start;
  if (needNl) out[pos++] = '\n';
  memcpy(out + pos, line, lineLen);
  pos += lineLen;
  out[pos++] = '\n';
  out[pos] = '\0';
  return totalNl - (drop > 0 ? drop : 0);
}

}  // namespace harixos
