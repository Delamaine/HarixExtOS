#include <unity.h>
#include <string.h>

#include "kernel/iot/onchange_logic.h"

using namespace harixos::iot;

void setUp(void) {}
void tearDown(void) {}

static void test_parse_valid_classes(void) {
  BinaryClass c;
  TEST_ASSERT_TRUE(parseBinaryClass("motion", c));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)BinaryClass::Motion, (uint8_t)c);
  TEST_ASSERT_TRUE(parseBinaryClass("occupancy", c));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)BinaryClass::Occupancy, (uint8_t)c);
  TEST_ASSERT_TRUE(parseBinaryClass("door", c));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)BinaryClass::Door, (uint8_t)c);
  TEST_ASSERT_TRUE(parseBinaryClass("window", c));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)BinaryClass::Window, (uint8_t)c);
  TEST_ASSERT_TRUE(parseBinaryClass("moisture", c));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)BinaryClass::Moisture, (uint8_t)c);
}

static void test_parse_rejects_unknown(void) {
  BinaryClass c = BinaryClass::Motion;
  TEST_ASSERT_FALSE(parseBinaryClass("smoke", c));
  TEST_ASSERT_FALSE(parseBinaryClass("", c));
}

static void test_token_roundtrip(void) {
  const char *tokens[] = {"motion", "occupancy", "door", "window", "moisture"};
  for (int i = 0; i < 5; ++i) {
    TEST_ASSERT_EQUAL_STRING(tokens[i], binaryClassToken((BinaryClass)i));
    BinaryClass parsed;
    TEST_ASSERT_TRUE(parseBinaryClass(binaryClassToken((BinaryClass)i), parsed));
    TEST_ASSERT_EQUAL_UINT8(i, (uint8_t)parsed);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_valid_classes);
  RUN_TEST(test_parse_rejects_unknown);
  RUN_TEST(test_token_roundtrip);
  return UNITY_END();
}
