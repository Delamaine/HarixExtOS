#include <unity.h>

#include <cmath>
#include <cstring>

#include "kernel/iot/sensor_logic.h"

using namespace harixos::iot;

void setUp(void) {}
void tearDown(void) {}

static void test_valid_name(void) {
  TEST_ASSERT_TRUE(sensorValidName("garage"));
  TEST_ASSERT_FALSE(sensorValidName(""));
  TEST_ASSERT_FALSE(sensorValidName("has space"));
  TEST_ASSERT_FALSE(sensorValidName("bad.name!"));
  char longName[17];
  memset(longName, 'a', 16);
  longName[16] = '\0';
  TEST_ASSERT_FALSE(sensorValidName(longName));
}

static void test_parse_type(void) {
  uint8_t t = 0xFF;
  TEST_ASSERT_TRUE(sensorParseType("ultrasonic", &t));
  TEST_ASSERT_EQUAL_UINT(kSensorUltrasonic, t);
  TEST_ASSERT_TRUE(sensorParseType("dht22", &t));
  TEST_ASSERT_EQUAL_UINT(kSensorDht22, t);
  TEST_ASSERT_TRUE(sensorParseType("ds18b20", &t));
  TEST_ASSERT_EQUAL_UINT(kSensorDs18b20, t);
  TEST_ASSERT_TRUE(sensorParseType("bme280", &t));
  TEST_ASSERT_EQUAL_UINT(kSensorBme280, t);
  TEST_ASSERT_FALSE(sensorParseType("temprature", &t));
  TEST_ASSERT_FALSE(sensorParseType("", &t));
  TEST_ASSERT_EQUAL_STRING("dht22", sensorTypeName(kSensorDht22));
}

static void test_find_and_pin_collision(void) {
  SensorDef pool[2] = {};
  pool[0].type = kSensorUltrasonic;
  pool[0].a = 14;
  pool[0].b = 12;
  strcpy(pool[0].name, "door");
  pool[1].type = kSensorDht22;
  pool[1].a = 4;
  pool[1].b = 0;
  strcpy(pool[1].name, "hall");
  TEST_ASSERT_EQUAL_INT(0, sensorFindIndex(pool, 2, "door"));
  TEST_ASSERT_EQUAL_INT(1, sensorFindIndex(pool, 2, "hall"));
  TEST_ASSERT_EQUAL_INT(-1, sensorFindIndex(pool, 2, "nope"));
  TEST_ASSERT_TRUE(sensorHasPin(pool, 2, 14, 12, -1));
  TEST_ASSERT_TRUE(sensorHasPin(pool, 2, 4, 0, -1));
  TEST_ASSERT_FALSE(sensorHasPin(pool, 2, 4, 0, 1));
  TEST_ASSERT_FALSE(sensorHasPin(pool, 2, 9, 9, -1));
}

static void test_quantities_units_classes(void) {
  uint8_t t;
  sensorParseType("dht22", &t);
  TEST_ASSERT_TRUE(sensorHasQuantity(t, kQtyTemperature));
  TEST_ASSERT_TRUE(sensorHasQuantity(t, kQtyHumidity));
  TEST_ASSERT_FALSE(sensorHasQuantity(t, kQtyPressure));
  sensorParseType("ultrasonic", &t);
  TEST_ASSERT_TRUE(sensorHasQuantity(t, kQtyDistance));
  TEST_ASSERT_FALSE(sensorHasQuantity(t, kQtyTemperature));
  TEST_ASSERT_EQUAL_STRING("temperature", sensorQtyName(kQtyTemperature));
  TEST_ASSERT_EQUAL_STRING("\xC2\xB0" "C", sensorUnit(t, kQtyTemperature));
  TEST_ASSERT_EQUAL_STRING("temperature", sensorDeviceClass(t, kQtyTemperature));
  TEST_ASSERT_EQUAL_STRING("cm", sensorUnit(t, kQtyDistance));
  TEST_ASSERT_EQUAL_STRING("hPa", sensorUnit(t, kQtyPressure));
}

static void test_set_and_format_reading(void) {
  SensorDef d = {};
  sensorSetReading(d, kQtyTemperature, 21.25);
  TEST_ASSERT_EQUAL_FLOAT(21.25, d.temperature);
  sensorSetReading(d, kQtyTemperature, NAN);
  TEST_ASSERT_TRUE(isnan(d.temperature));
  char buf[16];
  sensorFormatValue(21.26, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("21.3", buf);
  sensorFormatValue(21.24, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("21.2", buf);
}

static void test_persist_line_roundtrip(void) {
  SensorDef d = {};
  d.type = kSensorDs18b20;
  d.a = 5;
  d.b = 2;
  strcpy(d.name, "probe1");
  char line[64];
  sensorPersistLine(d, line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING("probe1|ds18b20|5|2", line);
  SensorDef out = {};
  TEST_ASSERT_TRUE(sensorParseLine(line, &out));
  TEST_ASSERT_EQUAL_STRING("probe1", out.name);
  TEST_ASSERT_EQUAL_UINT(kSensorDs18b20, out.type);
  TEST_ASSERT_EQUAL_UINT(5, out.a);
  TEST_ASSERT_EQUAL_UINT(2, out.b);
}

static void test_discovery_payload_field_exact(void) {
  char out[256];
  TEST_ASSERT_TRUE(sensorDiscoveryPayload(
      "haps", "harixos/x", "garage", kSensorDht22, kQtyTemperature, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING(
      "{\"name\":\"haps_garage_temperature\",\"state_topic\":\"harixos/x/sensor/garage/temperature\","
      "\"unit_of_measurement\":\"" "\xC2\xB0" "C\",\"device_class\":\"temperature\","
      "\"uniq_id\":\"haps_sensor_garage_temperature\",\"dev\":{\"ids\":\"haps\"}}",
      out);
  char small[8];
  TEST_ASSERT_FALSE(sensorDiscoveryPayload(
      "haps", "harixos/x", "garage", kSensorDht22, kQtyTemperature, small, sizeof(small)));
}

static void test_discovery_payload_bme_pressure(void) {
  char out[256];
  TEST_ASSERT_TRUE(sensorDiscoveryPayload(
      "haps", "harixos/x", "garage", kSensorBme280, kQtyPressure, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING(
      "{\"name\":\"haps_garage_pressure\",\"state_topic\":\"harixos/x/sensor/garage/pressure\","
      "\"unit_of_measurement\":\"hPa\",\"device_class\":\"pressure\","
      "\"uniq_id\":\"haps_sensor_garage_pressure\",\"dev\":{\"ids\":\"haps\"}}",
      out);
}

static void test_discovery_payload_ultrasonic_distance(void) {
  char out[256];
  TEST_ASSERT_TRUE(sensorDiscoveryPayload(
      "haps", "harixos/x", "door", kSensorUltrasonic, kQtyDistance, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING(
      "{\"name\":\"haps_door_distance\",\"state_topic\":\"harixos/x/sensor/door/distance\","
      "\"unit_of_measurement\":\"cm\",\"device_class\":\"distance\","
      "\"uniq_id\":\"haps_sensor_door_distance\",\"dev\":{\"ids\":\"haps\"}}",
      out);
}

static void test_parse_line_rejects_malformed(void) {
  SensorDef out = {};
  TEST_ASSERT_FALSE(sensorParseLine("", &out));
  TEST_ASSERT_FALSE(sensorParseLine("garage|dht22|4", &out));
  TEST_ASSERT_FALSE(sensorParseLine("", &out));
  TEST_ASSERT_FALSE(sensorParseLine("|dht22|4|0", &out));
  TEST_ASSERT_FALSE(sensorParseLine("bad name|dht22|4|0", &out));
  TEST_ASSERT_FALSE(sensorParseLine("garage|nonsense|4|0", &out));
  TEST_ASSERT_FALSE(sensorParseLine("garage|dht22|0|0", &out));
  TEST_ASSERT_FALSE(sensorParseLine("garage|bme280|4|0", &out));
  TEST_ASSERT_FALSE(sensorParseLine("garage|bme280|0|1", &out));
  TEST_ASSERT_FALSE(sensorParseLine("door|ultrasonic|5|0", &out));
  TEST_ASSERT_FALSE(sensorParseLine("door|ultrasonic|0|6", &out));
  TEST_ASSERT_FALSE(sensorParseLine("garage|dht22|4x|0", &out));
  TEST_ASSERT_FALSE(sensorParseLine("garage|dht22|260|0", &out));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_name);
  RUN_TEST(test_parse_type);
  RUN_TEST(test_find_and_pin_collision);
  RUN_TEST(test_quantities_units_classes);
  RUN_TEST(test_set_and_format_reading);
  RUN_TEST(test_persist_line_roundtrip);
  RUN_TEST(test_parse_line_rejects_malformed);
  RUN_TEST(test_discovery_payload_field_exact);
  RUN_TEST(test_discovery_payload_bme_pressure);
  RUN_TEST(test_discovery_payload_ultrasonic_distance);
  return UNITY_END();
}