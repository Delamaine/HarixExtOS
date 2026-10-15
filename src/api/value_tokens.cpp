#include "value_tokens.h"

#include <Arduino.h>
#include <string.h>
#include <time.h>

#include "sensor.h"

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

  // "sensor <name> <quantity>": last-good cached reading of a registered
  // sensor (same cache as MQTT publish; refreshed by sensor read/publish).
  static const char kSensor[] = "sensor ";
  const size_t kSensorLen = sizeof(kSensor) - 1;
  if (tokenLen > kSensorLen && memcmp(token, kSensor, kSensorLen) == 0) {
    char name[16], qty[12];
    size_t i = kSensorLen, n = 0;
    while (i < tokenLen && token[i] != ' ') {
      if (n >= sizeof(name) - 1) return false;
      name[n++] = token[i++];
    }
    name[n] = '\0';
    if (n == 0 || n > 15) return false;
    if (i >= tokenLen) return false;  // missing quantity
    ++i;
    size_t q = 0;
    while (i < tokenLen) {
      if (token[i] == ' ' || q >= sizeof(qty) - 1) return false;
      qty[q++] = token[i++];
    }
    qty[q] = '\0';
    uint8_t qtyId = 0;
    if (!harixos::iot::sensorParseQty(qty, &qtyId)) return false;
    float v = 0;
    if (!getSensorReading(name, qtyId, &v)) return false;
    out = v;
    return true;
  }

  return false;
}

}  // namespace api
}  // namespace harixos
