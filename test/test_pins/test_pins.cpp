#include <unity.h>

#include "api/pins.h"

using harixos::api::pins::isAvailable;

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

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_usable_pins);
  RUN_TEST(test_rejects_flash_and_uart);
  RUN_TEST(test_rejects_out_of_range);
  return UNITY_END();
}
