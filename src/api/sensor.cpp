#include "sensor.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <DHTesp.h>
#include <DallasTemperature.h>
#include <FS.h>
#include <LittleFS.h>
#include <OneWire.h>
#include <Wire.h>

#include "apps/settings/settings.h"
#include "gpio_api.h"
#include "kernel/iot/mqtt_service.h"

namespace harixos {
namespace api {

using namespace harixos::iot;

const char* kSensorsPath = "/sensors.conf";

namespace {

SensorDef s_pool[SensorAPI::kMaxSensors];
bool s_inited[SensorAPI::kMaxSensors] = {false};
uint32_t s_lastDhtMs[SensorAPI::kMaxSensors] = {0};
DHTesp s_dht[SensorAPI::kMaxSensors];
const char* s_lastReadError = nullptr;  // reason for the most recent readSensor failure

int findIndex(const char* name) {
  for (int i = 0; i < SensorAPI::kMaxSensors; ++i) {
    if (s_inited[i] && strcmp(s_pool[i].name, name) == 0) return i;
  }
  return -1;
}

int findIndexByTrigger(uint8_t trigger) {
  for (int i = 0; i < SensorAPI::kMaxSensors; ++i) {
    if (s_inited[i] && s_pool[i].a == trigger) return i;
  }
  return -1;
}

int findIndexByEcho(uint8_t echo) {
  for (int i = 0; i < SensorAPI::kMaxSensors; ++i) {
    if (s_inited[i] && s_pool[i].type == kSensorUltrasonic && s_pool[i].b == echo) return i;
  }
  return -1;
}

static float qtyValue(const SensorDef& d, uint8_t q) {
  switch (q) {
    case kQtyDistance: return d.distance;
    case kQtyTemperature: return d.temperature;
    case kQtyHumidity: return d.humidity;
    case kQtyPressure: return d.pressure;
    default: return NAN;
  }
}

static bool isNumeric(const String& s) {
  if (s.length() == 0) return false;
  for (unsigned int i = 0; i < s.length(); ++i) {
    if (!isDigit(s[i])) return false;
  }
  return true;
}

// ===== backends =====

static bool readUltrasonic(SensorDef& d) {
  uint8_t trigger = d.a;
  uint8_t echo = d.b;
  pinMode(trigger, OUTPUT);
  pinMode(echo, INPUT);
  digitalWrite(trigger, LOW);
  delayMicroseconds(2);
  digitalWrite(trigger, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigger, LOW);
  uint16_t pulse = pulseIn(echo, HIGH, 30000);
  if (pulse == 0) return false;
  // Speed of sound 343 m/s: (pulse_us * 0.0343 cm/us) / 2 round-trip.
  sensorSetReading(d, kQtyDistance, pulse * 0.01715f);
  return true;
}

static bool dhtTooSoon(int idx) {
  return s_lastDhtMs[idx] != 0 && (int32_t)(millis() - s_lastDhtMs[idx]) < 2000;
}

static bool readDht(SensorDef& d, int idx) {
  if (dhtTooSoon(idx)) return false;
  s_dht[idx].setup(d.a, DHTesp::DHT22);
  TempAndHumidity th = s_dht[idx].getTempAndHumidity();
  if (s_dht[idx].getStatus() != DHTesp::ERROR_NONE) return false;
  sensorSetReading(d, kQtyTemperature, th.temperature);
  sensorSetReading(d, kQtyHumidity, th.humidity);
  s_lastDhtMs[idx] = millis();
  return true;
}

static bool readDs18b20(SensorDef& d) {
  OneWire oneWire(d.a);
  DallasTemperature ds(&oneWire);
  ds.begin();
  if (ds.getDeviceCount() <= d.b) return false;
  ds.requestTemperatures();
  float t = ds.getTempCByIndex(d.b);
  if (t <= -126.0f) return false;  // DEVICE_DISCONNECTED_C
  sensorSetReading(d, kQtyTemperature, t);
  return true;
}

// ===== BME280/BMP280 over I2C (hand-rolled, no lib) =====
// Registers and compensation per BME280 datasheet §4.2 / §9.1.3. Chip id 0x60
// is a BME280 (humidity present); 0x58 a BMP280 (temperature+pressure only).

struct BmeCalib {
  uint16_t t1;
  int16_t t2, t3;
  uint16_t p1;
  int16_t p2, p3, p4, p5, p6, p7, p8, p9;
  uint8_t h1;
  int16_t h2;
  uint8_t h3;
  int16_t h4, h5;
  int8_t h6;
};

static bool bmeReadBlock(uint8_t addr, uint8_t reg, uint8_t* out, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)n) < n) return false;
  for (uint8_t i = 0; i < n; ++i) out[i] = (uint8_t)Wire.read();
  return true;
}

static bool bmeReadCalib(uint8_t addr, BmeCalib* c) {
  uint8_t tp[26];
  if (!bmeReadBlock(addr, 0x88, tp, sizeof(tp))) return false;
  c->t1 = (uint16_t)(tp[0] | (tp[1] << 8));
  c->t2 = (int16_t)(tp[2] | (tp[3] << 8));
  c->t3 = (int16_t)(tp[4] | (tp[5] << 8));
  c->p1 = (uint16_t)(tp[6] | (tp[7] << 8));
  c->p2 = (int16_t)(tp[8] | (tp[9] << 8));
  c->p3 = (int16_t)(tp[10] | (tp[11] << 8));
  c->p4 = (int16_t)(tp[12] | (tp[13] << 8));
  c->p5 = (int16_t)(tp[14] | (tp[15] << 8));
  c->p6 = (int16_t)(tp[16] | (tp[17] << 8));
  c->p7 = (int16_t)(tp[18] | (tp[19] << 8));
  c->p8 = (int16_t)(tp[20] | (tp[21] << 8));
  c->p9 = (int16_t)(tp[22] | (tp[23] << 8));
  uint8_t h[7];
  uint8_t h1;
  if (!bmeReadBlock(addr, 0xA1, &h1, 1)) return false;
  if (!bmeReadBlock(addr, 0xE1, h, sizeof(h))) return false;
  c->h1 = h1;
  c->h2 = (int16_t)(h[0] | (h[1] << 8));
  c->h3 = h[2];
  c->h4 = (int16_t)(((int8_t)h[3] << 4) | (h[4] & 0x0F));
  c->h5 = (int16_t)(((int8_t)h[5] << 4) | (h[4] >> 4));
  c->h6 = (int8_t)h[6];
  return true;
}

static bool readBme280(SensorDef& d) {
  uint8_t addr = 0;
  uint8_t chipId = 0;
  for (uint8_t trial = 0; trial < 2; ++trial) {
    uint8_t candidate = 0x76 + trial;
    uint8_t id;
    if (bmeReadBlock(candidate, 0xD0, &id, 1) && (id == 0x60 || id == 0x58)) {
      addr = candidate;
      chipId = id;
      break;
    }
  }
  if (!addr) {
    s_lastReadError = "no BME280 on I2C";
    return false;
  }

  BmeCalib c;
  if (!bmeReadCalib(addr, &c)) {
    s_lastReadError = "BME280 read failed";
    return false;
  }

  // Oversampling x1, normal mode: ctrl_hum=0x01, ctrl_meas=0x27.
  Wire.beginTransmission(addr);
  Wire.write(0xF2);
  Wire.write(0x01);
  if (Wire.endTransmission() != 0) {
    s_lastReadError = "BME280 read failed";
    return false;
  }
  Wire.beginTransmission(addr);
  Wire.write(0xF4);
  Wire.write(0x27);
  if (Wire.endTransmission() != 0) {
    s_lastReadError = "BME280 read failed";
    return false;
  }
  delay(30);  // settle for x1 oversampling

  uint8_t raw[8];
  if (!bmeReadBlock(addr, 0xF7, raw, sizeof(raw))) {
    s_lastReadError = "BME280 read failed";
    return false;
  }

  uint32_t adcP = ((uint32_t)raw[0] << 12) | ((uint32_t)raw[1] << 4) | ((uint32_t)raw[2] >> 4);
  uint32_t adcT = ((uint32_t)raw[3] << 12) | ((uint32_t)raw[4] << 4) | ((uint32_t)raw[5] >> 4);
  uint32_t adcH = ((uint32_t)raw[6] << 8) | (uint32_t)raw[7];
  if (adcT == 0x80000 || adcP == 0x80000) {
    s_lastReadError = "BME280 read failed";
    return false;  // unpowered/uninitialized
  }

  // Temperature, then pressure, then humidity — datasheet §9.1.3.
  double v1 = (adcT / 16384.0 - c.t1 / 1024.0) * c.t2;
  double v2 = (adcT / 131072.0 - c.t1 / 8192.0) * (adcT / 131072.0 - c.t1 / 8192.0) * c.t3;
  double tFine = v1 + v2;
  sensorSetReading(d, kQtyTemperature, (float)(tFine / 5120.0));

  v1 = tFine / 2.0 - 64000.0;
  v2 = v1 * v1 * c.p6 / 32768.0;
  v2 += v1 * c.p5 * 2.0;
  v2 = v2 / 4.0 + c.p4 * 65536.0;
  v1 = (c.p3 * v1 * v1 / 524288.0 + c.p2 * v1) / 524288.0;
  v1 = (1.0 + v1 / 32768.0) * c.p1;
  if (v1 == 0.0) {
    s_lastReadError = "BME280 read failed";
    return false;
  }
  double pressPa = 1048576.0 - adcP;
  pressPa = (pressPa - v2 / 4096.0) * 6250.0 / v1;
  v1 = c.p9 * pressPa * pressPa / 2147483648.0;
  v2 = pressPa * c.p8 / 32768.0;
  pressPa += (v1 + v2 + c.p7) / 16.0;
  sensorSetReading(d, kQtyPressure, (float)(pressPa / 100.0));

  if (chipId == 0x60 && adcH != 0x8000) {  // BME280 only; BMP280 has no humidity
    double v3 = tFine - 76800.0;
    v3 = (adcH - (c.h4 * 64.0 + c.h5 / 16384.0 * v3)) *
         (c.h2 / 65536.0 * (1.0 + c.h6 / 67108864.0 * v3 * (1.0 + c.h3 / 67108864.0 * v3)));
    v3 = v3 * (1.0 - c.h1 * v3 / 524288.0);
    if (v3 < 0.0) v3 = 0.0;
    if (v3 > 100.0) v3 = 100.0;
    sensorSetReading(d, kQtyHumidity, (float)v3);
  }
  return true;
}

static bool readSensor(int idx) {
  s_lastReadError = nullptr;
  SensorDef& d = s_pool[idx];
  switch (d.type) {
    case kSensorUltrasonic: return readUltrasonic(d);
    case kSensorDht22: return readDht(d, idx);
    case kSensorDs18b20: return readDs18b20(d);
    case kSensorBme280: return readBme280(d);
    default: return false;
  }
}

// ===== registration / persistence =====

// Any GPIO clash between a new pinned sensor and an already-inited one.
// ds18b20's b is a device index, not a GPIO, so it never participates.
// bme280 has no GPIOs. b of the new sensor is a GPIO only for ultrasonic.
static bool pinsConflict(uint8_t type, uint8_t a, uint8_t b) {
  for (int i = 0; i < SensorAPI::kMaxSensors; ++i) {
    if (!s_inited[i]) continue;
    const SensorDef& e = s_pool[i];
    if (e.type == kSensorBme280) continue;
    if (a != 0 && (a == e.a || (e.type == kSensorUltrasonic && a == e.b))) return true;
    if (type == kSensorUltrasonic && b != 0 &&
        (b == e.a || (e.type == kSensorUltrasonic && b == e.b))) {
      return true;
    }
  }
  return false;
}

// out == nullptr silences the success line (boot-time config load).
static ApiResult registerSensor(uint8_t type, uint8_t a, uint8_t b,
                                const String& name, Stream* out) {
  if (!sensorValidName(name.c_str())) {
    return ApiResult(API_INVALID_ARGUMENT, "sensor name: 1-15 chars [A-Za-z0-9_-]");
  }
  // Pin rules mirror sensorParseLine so a persisted line is never accepted at
  // load and rejected at register (or vice versa): pinned types need a nonzero
  // GPIO, ultrasonic needs a real echo pin, bme280 takes no pins at all.
  if (type == kSensorBme280) {
    if (a != 0 || b != 0) {
      return ApiResult(API_INVALID_ARGUMENT, "bme280 takes no pins");
    }
  } else {
    if (a == 0 || (type == kSensorUltrasonic && b == 0)) {
      return ApiResult(API_INVALID_ARGUMENT, "sensor pin must be a nonzero GPIO number");
    }
    if (type == kSensorUltrasonic) {
      if (!GpioAPI::isAvailablePin(a) || !GpioAPI::isAvailablePin(b)) {
        return ApiResult(API_INVALID_PIN,
                         "GPIO" + String(a) + " or GPIO" + String(b) + " is reserved or invalid");
      }
    } else {
      if (!GpioAPI::isAvailablePin(a)) {
        return ApiResult(API_INVALID_PIN, "GPIO" + String(a) + " is reserved or invalid");
      }
    }
  }
  if (pinsConflict(type, a, b)) {
    return ApiResult(API_DUPLICATE, "sensor already registered for those pins");
  }
  if (type == kSensorBme280) {
    for (int i = 0; i < SensorAPI::kMaxSensors; ++i) {
      if (s_inited[i] && s_pool[i].type == kSensorBme280) {
        return ApiResult(API_DUPLICATE, "bme280 already registered");
      }
    }
  }
  if (sensorFindIndex(s_pool, SensorAPI::kMaxSensors, name.c_str()) >= 0) {
    return ApiResult(API_DUPLICATE, "sensor already registered with name " + name);
  }
  for (int i = 0; i < SensorAPI::kMaxSensors; ++i) {
    if (!s_inited[i]) {
      SensorDef& d = s_pool[i];
      memset(&d, 0, sizeof(d));
      strncpy(d.name, name.c_str(), sizeof(d.name) - 1);
      d.name[sizeof(d.name) - 1] = '\0';
      d.type = type;
      d.a = a;
      d.b = b;
      d.temperature = NAN;
      d.humidity = NAN;
      d.pressure = NAN;
      d.distance = NAN;
      s_inited[i] = true;
      invalidateDiscovery();
      if (out) {
        out->printf("sensor %s registered (%s)\r\n", d.name, sensorTypeName(type));
      }
      return ApiResult(API_OK, "");
    }
  }
  return ApiResult(API_ERROR, "Maximum sensors reached (" + String(SensorAPI::kMaxSensors) + ")");
}

static bool saveSensorConfig() {
  File f = LittleFS.open(kSensorsPath, "w");
  if (!f) return false;
  for (int i = 0; i < SensorAPI::kMaxSensors; ++i) {
    if (!s_inited[i]) continue;
    char line[40];
    sensorPersistLine(s_pool[i], line, sizeof(line));
    f.println(line);
  }
  f.close();
  return true;
}

static void loadSensorConfig() {
  File f = LittleFS.open(kSensorsPath, "r");
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    SensorDef d;
    if (!sensorParseLine(line.c_str(), &d)) {
      Serial.printf("sensor: skipping bad line: %s\r\n", line.c_str());
      continue;
    }
    ApiResult r = registerSensor(d.type, d.a, d.b, String(d.name), nullptr);
    if (r.isError()) {
      Serial.printf("sensor: skipping bad line: %s\r\n", line.c_str());
    }
  }
  f.close();
}

// ===== output helpers =====

static void printReadings(int idx, Stream& out) {
  SensorDef& d = s_pool[idx];
  out.printf("%s (%s)", d.name, sensorTypeName(d.type));
  if (d.type == kSensorUltrasonic) {
    out.printf(" t=GPIO%d e=GPIO%d", d.a, d.b);
  } else if (d.type == kSensorDht22 || d.type == kSensorDs18b20) {
    out.printf(" GPIO%d", d.a);
    if (d.type == kSensorDs18b20 && d.b > 0) out.printf(" #%d", d.b);
  }
  for (uint8_t q = 0; q < kSensorQtyCount; ++q) {
    if (!sensorHasQuantity(d.type, q)) continue;
    float v = qtyValue(d, q);
    if (isnan(v)) continue;
    char buf[16];
    sensorFormatValue(v, buf, sizeof(buf));
    out.printf(", %s=%s %s", sensorQtyName(q), buf, sensorUnit(d.type, q));
  }
  out.println();
}

static void listSensors(Stream& out) {
  bool any = false;
  for (int i = 0; i < SensorAPI::kMaxSensors; ++i) {
    if (!s_inited[i]) continue;
    any = true;
    printReadings(i, out);
  }
  if (!any) out.println(F("No sensors."));
}

static ApiResult readByName(const String& name, Stream& out) {
  int idx = findIndex(name.c_str());
  if (idx < 0) return ApiResult(API_ERROR, "sensor not found");
  if (s_pool[idx].type == kSensorDht22 && dhtTooSoon(idx)) {
    return ApiResult(API_ERROR, "read too soon");
  }
  if (!readSensor(idx)) {
    return ApiResult(API_ERROR,
                     s_lastReadError ? String(s_lastReadError) : "read " + name + " failed");
  }
  printReadings(idx, out);
  return ApiResult(API_OK, "");
}

// ===== legacy aliases (pin-addressed, ultrasonic only, deprecated) =====

static ApiResult legacyInit(const String& tail, Stream& out) {
  int sp = tail.indexOf(' ');
  if (sp < 0) {
    return ApiResult(API_INVALID_ARGUMENT, "Usage: sensor init <trigger> <echo>");
  }
  uint8_t trigger = (uint8_t)tail.substring(0, sp).toInt();
  String echoStr = tail.substring(sp + 1);
  echoStr.trim();
  uint8_t echo = (uint8_t)echoStr.toInt();
  char name[16];
  snprintf(name, sizeof(name), "hc%d", echo);
  return registerSensor(kSensorUltrasonic, trigger, echo, name, &out);
}

static ApiResult legacyPing(const String& tail, Stream& out) {
  uint8_t trigger = 4;
  uint8_t echo = 5;
  if (tail.length() > 0) {
    int sp = tail.indexOf(' ');
    if (sp < 0) {
      trigger = (uint8_t)tail.toInt();
    } else {
      trigger = (uint8_t)tail.substring(0, sp).toInt();
      echo = (uint8_t)tail.substring(sp + 1).toInt();
    }
  }
  int idx = findIndexByTrigger(trigger);
  if (idx < 0) {
    char name[16];
    snprintf(name, sizeof(name), "hc%d", echo);
    ApiResult r = registerSensor(kSensorUltrasonic, trigger, echo, name, &out);
    if (r.isError()) return r;
    idx = findIndexByTrigger(trigger);
  }
  if (!readSensor(idx)) {
    return ApiResult(API_ERROR, "no object detected or read failed");
  }
  out.printf("Distance: %.1f cm\r\n", s_pool[idx].distance);
  return ApiResult(API_OK, "");
}

static ApiResult legacyRead(const String& tail, Stream& out) {
  uint8_t echo = 5;
  if (tail.length() > 0) echo = (uint8_t)tail.toInt();
  int idx = findIndexByEcho(echo);
  if (idx < 0) {
    return ApiResult(API_ERROR, "no ultrasonic sensor on echo GPIO" + String(echo));
  }
  if (!readSensor(idx)) {
    return ApiResult(API_ERROR, "no object detected or read failed");
  }
  out.printf("Distance: %.1f cm\r\n", s_pool[idx].distance);
  return ApiResult(API_OK, "");
}

// ===== register / publish parsers =====

// Reject non-numeric and out-of-range pin tokens ("D5" -> 0 must not slip
// through); registerSensor still enforces GPIO availability.
static bool parsePin(const String& s, uint8_t* out) {
  if (!isNumeric(s) || s.toInt() > 255) return false;
  *out = (uint8_t)s.toInt();
  return true;
}

static ApiResult doRegister(const String& tail, Stream& out) {
  int sp = tail.indexOf(' ');
  if (sp < 0) {
    return ApiResult(API_INVALID_ARGUMENT,
                     "Usage: sensor register <ultrasonic <t> <e>|dht22 <pin>|ds18b20 <pin> [index]|bme280> <name>");
  }
  String typeStr = tail.substring(0, sp);
  typeStr.trim();
  typeStr.toLowerCase();
  String rest = tail.substring(sp + 1);
  rest.trim();

  uint8_t type;
  if (!sensorParseType(typeStr.c_str(), &type)) {
    return ApiResult(API_INVALID_ARGUMENT,
                     "Usage: sensor register <ultrasonic <t> <e>|dht22 <pin>|ds18b20 <pin> [index]|bme280> <name>");
  }

  uint8_t a = 0;
  uint8_t b = 0;
  String nameStr;
  if (type == kSensorUltrasonic) {
    int sp1 = rest.indexOf(' ');
    if (sp1 < 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: sensor register ultrasonic <trigger> <echo> <name>");
    }
    uint8_t t;
    if (!parsePin(rest.substring(0, sp1), &t)) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: sensor register ultrasonic <trigger> <echo> <name>");
    }
    a = t;
    rest = rest.substring(sp1 + 1);
    String echoStr;
    if (rest.indexOf(' ') < 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: sensor register ultrasonic <trigger> <echo> <name>");
    }
    echoStr = rest.substring(0, rest.indexOf(' '));
    nameStr = rest.substring(rest.indexOf(' ') + 1);
    if (!parsePin(echoStr, &b)) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: sensor register ultrasonic <trigger> <echo> <name>");
    }
  } else if (type == kSensorDht22) {
    int sp1 = rest.indexOf(' ');
    if (sp1 < 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: sensor register dht22 <pin> <name>");
    }
    if (!parsePin(rest.substring(0, sp1), &a)) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: sensor register dht22 <pin> <name>");
    }
    nameStr = rest.substring(sp1 + 1);
  } else if (type == kSensorDs18b20) {
    int sp1 = rest.indexOf(' ');
    if (sp1 < 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: sensor register ds18b20 <pin> [index] <name>");
    }
    if (!parsePin(rest.substring(0, sp1), &a)) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: sensor register ds18b20 <pin> [index] <name>");
    }
    rest = rest.substring(sp1 + 1);
    int sp2 = rest.indexOf(' ');
    if (sp2 < 0) {
      nameStr = rest;  // index defaults to 0
    } else {
      if (!parsePin(rest.substring(0, sp2), &b)) {
        return ApiResult(API_INVALID_ARGUMENT, "Usage: sensor register ds18b20 <pin> [index] <name>");
      }
      nameStr = rest.substring(sp2 + 1);
    }
  } else {  // bme280: no pins
    nameStr = rest;
  }
  nameStr.trim();
  if (nameStr.length() == 0) {
    return ApiResult(API_INVALID_ARGUMENT, "missing sensor name");
  }
  return registerSensor(type, a, b, nameStr, &out);
}

static ApiResult doPublish(const String& tail, Stream& out) {
  if (tail.length() > 0) {
    int idx = findIndex(tail.c_str());
    if (idx < 0) return ApiResult(API_ERROR, "sensor not found");
    if (s_pool[idx].type == kSensorDht22 && dhtTooSoon(idx)) {
      return ApiResult(API_ERROR, "read too soon");
    }
    readSensor(idx);  // failure keeps the cached reading published
  } else {
    for (int i = 0; i < SensorAPI::kMaxSensors; ++i) {
      if (!s_inited[i]) continue;
      if (s_pool[i].type == kSensorDht22 && dhtTooSoon(i)) continue;
      readSensor(i);
    }
  }
  publishSensorStates();
  return ApiResult(API_OK, "");
}

}  // namespace

ApiResult SensorAPI::runCommand(const String& args, Stream& out) {
  String rest = args;
  int firstSpace = rest.indexOf(' ');
  String action = (firstSpace > 0) ? rest.substring(0, firstSpace) : rest;
  action.trim();
  action.toLowerCase();
  String tail = (firstSpace > 0) ? rest.substring(firstSpace + 1) : String();
  tail.trim();

  if (action == F("list") || action.length() == 0) {
    listSensors(out);
    return ApiResult(API_OK, "");
  }
  if (action == F("register")) {
    ApiResult r = doRegister(tail, out);
    if (!r.isError() && !saveSensorConfig()) {
      return ApiResult(API_ERROR, "sensor registered but save failed");
    }
    return r;
  }
  if (action == F("unregister")) {
    int idx = findIndex(tail.c_str());
    if (idx < 0) return ApiResult(API_ERROR, "sensor not found");
    s_inited[idx] = false;
    invalidateDiscovery();
    if (!saveSensorConfig()) {
      return ApiResult(API_ERROR, "sensor unregistered but save failed");
    }
    out.printf("sensor %s unregistered\r\n", tail.c_str());
    return ApiResult(API_OK, "");
  }
  if (action == F("read")) {
    if (tail.length() == 0 || (isNumeric(tail) && findIndex(tail.c_str()) < 0)) {
      return legacyRead(tail, out);  // numeric + no such name -> echo-pin alias
    }
    return readByName(tail, out);
  }
  if (action == F("publish")) {
    return doPublish(tail, out);
  }
  if (action == F("init")) {
    return legacyInit(tail, out);
  }
  if (action == F("ping")) {
    return legacyPing(tail, out);
  }
  return ApiResult(API_INVALID_ARGUMENT,
                   "Usage: sensor [register ...|unregister <name>|list|read <name>|publish [name]|init <t> <e>|ping [t] [e]]");
}

void beginSensor() {
  loadSensorConfig();
}

void publishSensorStates() {
  if (!isConnected()) return;
  for (int i = 0; i < SensorAPI::kMaxSensors; ++i) {
    if (!s_inited[i]) continue;
    SensorDef& d = s_pool[i];
    for (uint8_t q = 0; q < kSensorQtyCount; ++q) {
      if (!sensorHasQuantity(d.type, q)) continue;
      float v = qtyValue(d, q);
      if (isnan(v)) continue;
      char buf[16];
      sensorFormatValue(v, buf, sizeof(buf));
      String topic = shellSettings.mqttPrefix + "/sensor/" + String(d.name) + "/" +
                     String(sensorQtyName(q));
      publishRaw(topic, String(buf), true);
    }
  }
}

String collectSensorNames() {
  String names;
  for (int i = 0; i < SensorAPI::kMaxSensors; ++i) {
    if (!s_inited[i]) continue;
    if (names.length() > 0) names += F(",");
    names += String(s_pool[i].name);
  }
  return names;
}

bool sensorTypeOf(const char* name, uint8_t* type) {
  int idx = findIndex(name);
  if (idx < 0) return false;
  if (type) *type = s_pool[idx].type;
  return true;
}

}  // namespace api
}  // namespace harixos