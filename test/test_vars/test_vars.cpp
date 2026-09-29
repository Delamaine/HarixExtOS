#include <unity.h>

#include <cstdio>
#include <cstring>

#include "api/vars.h"

using harixos::api::vars::clear;
using harixos::api::vars::count;
using harixos::api::vars::get;
using harixos::api::vars::isValidName;
using harixos::api::vars::set;

void setUp(void) { clear(); }
void tearDown(void) {}

static void test_set_get_roundtrip(void) {
  TEST_ASSERT_TRUE(set("x", 42));
  double v = 0;
  TEST_ASSERT_TRUE(get("x", v));
  TEST_ASSERT_EQUAL_DOUBLE(42, v);
}

static void test_get_undefined_returns_false_and_leaves_out(void) {
  double v = 7;
  TEST_ASSERT_FALSE(get("nope", v));
  TEST_ASSERT_EQUAL_DOUBLE(7, v);
}

static void test_capacity_16_accepted_17th_rejected(void) {
  char name[16];
  for (int i = 0; i < 16; ++i) {
    snprintf(name, sizeof(name), "v%d", i);
    TEST_ASSERT_TRUE(set(name, i));
  }
  TEST_ASSERT_EQUAL_UINT(16, count());
  TEST_ASSERT_FALSE(set("v16", 16));
  TEST_ASSERT_EQUAL_UINT(16, count());
}

static void test_valid_name_accepts_leading_underscore(void) {
  TEST_ASSERT_TRUE(isValidName("_a1"));
}

static void test_valid_name_rejects_leading_digit(void) {
  TEST_ASSERT_FALSE(isValidName("1x"));
}

static void test_valid_name_rejects_dollar(void) {
  TEST_ASSERT_FALSE(isValidName("$x"));
}

static void test_valid_name_rejects_spaces(void) {
  TEST_ASSERT_FALSE(isValidName("a b"));
}

static void test_valid_name_rejects_empty(void) {
  TEST_ASSERT_FALSE(isValidName(""));
}

static void test_valid_name_rejects_over_length(void) {
  char ok[17];
  memset(ok, 'a', 16);
  ok[16] = '\0';
  TEST_ASSERT_TRUE(isValidName(ok));  // exactly 16

  char tooLong[19];
  memset(tooLong, 'a', 17);
  tooLong[17] = '\0';
  TEST_ASSERT_FALSE(isValidName(tooLong));  // 17
}

static void test_set_overwrites_existing(void) {
  TEST_ASSERT_TRUE(set("x", 1));
  TEST_ASSERT_TRUE(set("x", 2));
  double v = 0;
  TEST_ASSERT_TRUE(get("x", v));
  TEST_ASSERT_EQUAL_DOUBLE(2, v);
  TEST_ASSERT_EQUAL_UINT(1, count());
}

static void test_clear_empties_store(void) {
  TEST_ASSERT_TRUE(set("x", 1));
  clear();
  TEST_ASSERT_EQUAL_UINT(0, count());
  double v = 0;
  TEST_ASSERT_FALSE(get("x", v));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_set_get_roundtrip);
  RUN_TEST(test_get_undefined_returns_false_and_leaves_out);
  RUN_TEST(test_capacity_16_accepted_17th_rejected);
  RUN_TEST(test_valid_name_accepts_leading_underscore);
  RUN_TEST(test_valid_name_rejects_leading_digit);
  RUN_TEST(test_valid_name_rejects_dollar);
  RUN_TEST(test_valid_name_rejects_spaces);
  RUN_TEST(test_valid_name_rejects_empty);
  RUN_TEST(test_valid_name_rejects_over_length);
  RUN_TEST(test_set_overwrites_existing);
  RUN_TEST(test_clear_empties_store);
  return UNITY_END();
}
