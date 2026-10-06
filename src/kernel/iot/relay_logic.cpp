#include "kernel/iot/relay_logic.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace harixos { namespace iot {

bool relaySetLevel(RelayState &r, bool level) {
  if (r.level == level) {
    return false;  // no change
  }
  r.level = level;
  return true;
}

bool relayParseLevel(const char *s, bool *out) {
  if (s == nullptr || out == nullptr) return false;
  if (strcmp(s, "on") == 0) { *out = true; return true; }
  if (strcmp(s, "off") == 0) { *out = false; return true; }
  return false;
}

bool relayValidName(const char *s) {
  if (s == nullptr) return false;
  size_t n = strlen(s);
  if (n == 0 || n > 15) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

void relayStatePayload(bool level, char *out, size_t n) {
  if (n == 0) return;
  const char *s = level ? "on" : "off";
  size_t slen = strlen(s);
  if (n < slen + 1) { out[0] = '\0'; return; }  // too small: never publish a partial payload
  memcpy(out, s, slen + 1);
}

void relayPersistLine(uint8_t pin, bool level, char *out, size_t n) {
  if (n == 0) return;
  int len = snprintf(out, n, "%u %d", pin, level ? 1 : 0);
  if (len < 0 || (size_t)len >= n) out[0] = '\0';  // truncated: treat as unpersistable
}

}}
