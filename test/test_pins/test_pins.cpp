#include <unity.h>

#include "api/pins.h"

using harixos::api::pins::isAvailable;
using harixos::api::pins::parse;

void test_usable_pins() {
  const uint8_t usable[] = {0, 2, 4, 5, 12, 13, 14, 15, 16};
  for (uint8_t p : usable) {
    TEST_ASSERT_TRUE(isAvailable(p));
  }
}

void test_rejects_flash_and_uart() {
  const uint8_t reserved[] = {1, 3, 6, 7, 8, 9, 10, 11};
  for (uint8_t p : reserved) {
    TEST_ASSERT_FALSE(isAvailable(p));
  }
}

void test_rejects_out_of_range() {
  TEST_ASSERT_FALSE(isAvailable(17));
  TEST_ASSERT_FALSE(isAvailable(255));
}

void test_parse_plain_number() {
  uint8_t pin = 99;
  TEST_ASSERT_TRUE(parse("4", &pin));
  TEST_ASSERT_EQUAL_UINT8(4, pin);
  TEST_ASSERT_TRUE(parse("0", &pin));
  TEST_ASSERT_EQUAL_UINT8(0, pin);
}

void test_parse_gpio_prefixed() {
  uint8_t pin = 99;
  TEST_ASSERT_TRUE(parse("GPIO4", &pin));
  TEST_ASSERT_EQUAL_UINT8(4, pin);
  TEST_ASSERT_TRUE(parse("gpio14", &pin));
  TEST_ASSERT_EQUAL_UINT8(14, pin);
}

void test_parse_rejects_garbage() {
  uint8_t pin = 99;
  TEST_ASSERT_FALSE(parse("set", &pin));
  TEST_ASSERT_FALSE(parse("", &pin));
  TEST_ASSERT_FALSE(parse("GPIO", &pin));
  TEST_ASSERT_FALSE(parse("D5", &pin));
  TEST_ASSERT_FALSE(parse("12x", &pin));
  TEST_ASSERT_FALSE(parse("256", &pin));
  TEST_ASSERT_FALSE(parse("GPIO256", &pin));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_usable_pins);
  RUN_TEST(test_rejects_flash_and_uart);
  RUN_TEST(test_rejects_out_of_range);
  RUN_TEST(test_parse_plain_number);
  RUN_TEST(test_parse_gpio_prefixed);
  RUN_TEST(test_parse_rejects_garbage);
  return UNITY_END();
}
