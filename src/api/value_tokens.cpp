#include "value_tokens.h"

#include <Arduino.h>
#include <string.h>
#include <time.h>

namespace harixos {
namespace api {
namespace {

bool tokenEquals(const char *token, size_t tokenLen, const char *lit) {
  const size_t n = strlen(lit);
  return tokenLen == n && memcmp(token, lit, n) == 0;
}

}  // namespace

bool resolveDeviceValueToken(const char *token, size_t tokenLen, double &out) {
  if (token == nullptr) return false;

  if (tokenEquals(token, tokenLen, "heap"))    { out = ESP.getFreeHeap(); return true; }
  if (tokenEquals(token, tokenLen, "adc"))     { out = analogRead(A0);    return true; }
  if (tokenEquals(token, tokenLen, "uptime"))  { out = millis() / 1000.0; return true; }
  if (tokenEquals(token, tokenLen, "millis"))  { out = millis();          return true; }
  if (tokenEquals(token, tokenLen, "time"))    { out = (double)time(nullptr); return true; }

  static const char kReadpin[] = "readpin ";
  const size_t kReadpinLen = sizeof(kReadpin) - 1;
  if (tokenLen > kReadpinLen && memcmp(token, kReadpin, kReadpinLen) == 0) {
    int pin = 0;
    for (size_t i = kReadpinLen; i < tokenLen; ++i) {
      if (token[i] < '0' || token[i] > '9') return false;  // non-numeric arg
      pin = pin * 10 + (token[i] - '0');
      if (pin > 255) return false;
    }
    out = digitalRead(pin);
    return true;
  }

  return false;
}

}  // namespace api
}  // namespace harixos
