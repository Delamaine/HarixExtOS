#include <unity.h>

#include <cstring>

#include "api/hostname.h"

using harixos::api::hostname::valid;

void test_valid_single_label() {
  TEST_ASSERT_TRUE(valid("esp8266"));
}

void test_valid_dotted() {
  TEST_ASSERT_TRUE(valid("esp8266.rimu"));
}

void test_valid_numeric_hyphen() {
  TEST_ASSERT_TRUE(valid("a1-b2"));
  TEST_ASSERT_TRUE(valid("-x-"));
  TEST_ASSERT_TRUE(valid("123"));
}

void test_valid_total_63() {
  char n[64];
  memset(n, 'a', 63);
  n[63] = '\0';
  TEST_ASSERT_TRUE(valid(n));
}

void test_rejects_null() {
  TEST_ASSERT_FALSE(valid(nullptr));
}

void test_rejects_empty() {
  TEST_ASSERT_FALSE(valid(""));
}

void test_rejects_uppercase() {
  TEST_ASSERT_FALSE(valid("ESP8266"));
  TEST_ASSERT_FALSE(valid("ESP8266.rimu"));
}

void test_rejects_leading_dot() {
  TEST_ASSERT_FALSE(valid(".rimu"));
}

void test_rejects_double_dot() {
  TEST_ASSERT_FALSE(valid("a..b"));
}

void test_rejects_trailing_dot() {
  TEST_ASSERT_FALSE(valid("rimu."));
}

void test_rejects_bad_chars() {
  TEST_ASSERT_FALSE(valid("a b"));
  TEST_ASSERT_FALSE(valid("a_b"));
  TEST_ASSERT_FALSE(valid("a!b"));
}

void test_rejects_label_64() {
  char n[65];
  memset(n, 'a', 64);
  n[64] = '\0';
  TEST_ASSERT_FALSE(valid(n));
}

void test_rejects_total_64() {
  char n[65];
  memset(n, 'a', 63);
  n[63] = '.';
  n[64] = '\0';
  TEST_ASSERT_FALSE(valid(n));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_valid_single_label);
  RUN_TEST(test_valid_dotted);
  RUN_TEST(test_valid_numeric_hyphen);
  RUN_TEST(test_valid_total_63);
  RUN_TEST(test_rejects_null);
  RUN_TEST(test_rejects_empty);
  RUN_TEST(test_rejects_uppercase);
  RUN_TEST(test_rejects_leading_dot);
  RUN_TEST(test_rejects_double_dot);
  RUN_TEST(test_rejects_trailing_dot);
  RUN_TEST(test_rejects_bad_chars);
  RUN_TEST(test_rejects_label_64);
  RUN_TEST(test_rejects_total_64);
  return UNITY_END();
}
