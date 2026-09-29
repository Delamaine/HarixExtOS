#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "api/cron.h"

using harixos::api::cron::Spec;
using harixos::api::cron::parse;

void setUp(void) {}
void tearDown(void) {}

// Expected masks are spelled out as explicit bit lists rather than hex
// literals so a reviewer can read the intent: `*/10 seconds` means "bits
// 0, 10, 20, 30, 40, 50", not "whatever the implementation produced".
static uint64_t bitsOf(std::initializer_list<int> indices) {
  uint64_t m = 0;
  for (int b : indices) {
    if (b >= 0 && b < 64) m |= (1ULL << b);
  }
  return m;
}

static uint64_t rangeBits(int lo, int hi) {
  uint64_t m = 0;
  for (int b = lo; b <= hi; ++b) m |= (1ULL << b);
  return m;
}

static void assertMask(uint64_t actual, uint64_t expected) {
  if (actual != expected) {
    char msg[96];
    snprintf(msg, sizeof(msg), "mask: expected 0x%016llX got 0x%016llX",
             (unsigned long long)expected, (unsigned long long)actual);
    TEST_FAIL_MESSAGE(msg);
  }
}

static void assertError(const char *expression, const char *expectedMessage,
                        int expectedBadField) {
  Spec spec;
  memset(&spec, 0xAA, sizeof(spec));
  int bad = -99;
  const char *err = parse(expression, spec, &bad);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING(expectedMessage, err);
  TEST_ASSERT_EQUAL_INT(expectedBadField, bad);
}

static void test_parse_all_stars(void) {
  Spec s;
  int bad = -99;
  TEST_ASSERT_NULL(parse("* * * * * *", s, &bad));
  assertMask(s.sec, rangeBits(0, 59));
  assertMask(s.minute, rangeBits(0, 59));
  assertMask(s.hour, rangeBits(0, 23));
  assertMask(s.dom, rangeBits(1, 31));
  assertMask(s.month, rangeBits(1, 12));
  assertMask(s.dow, rangeBits(0, 6));
  TEST_ASSERT_FALSE(s.domRestricted);
  TEST_ASSERT_FALSE(s.dowRestricted);
}

static void test_parse_single_second(void) {
  Spec s;
  TEST_ASSERT_NULL(parse("30 * * * * *", s, nullptr));
  assertMask(s.sec, bitsOf({30}));
  assertMask(s.minute, rangeBits(0, 59));
}

static void test_parse_range(void) {
  Spec s;
  TEST_ASSERT_NULL(parse("0-10 * * * * *", s, nullptr));
  assertMask(s.sec, rangeBits(0, 10));
  TEST_ASSERT_EQUAL_INT(0, (s.sec >> 11) & 1);
}

static void test_parse_step_from_star_sec(void) {
  Spec s;
  TEST_ASSERT_NULL(parse("*/10 * * * * *", s, nullptr));
  assertMask(s.sec, bitsOf({0, 10, 20, 30, 40, 50}));
}

static void test_parse_step_from_range(void) {
  Spec s;
  TEST_ASSERT_NULL(parse("0-30/10 * * * * *", s, nullptr));
  assertMask(s.sec, bitsOf({0, 10, 20, 30}));
}

static void test_parse_list(void) {
  Spec s;
  TEST_ASSERT_NULL(parse("1,3,5 * * * * *", s, nullptr));
  assertMask(s.sec, bitsOf({1, 3, 5}));
}

static void test_parse_list_of_ranges(void) {
  Spec s;
  TEST_ASSERT_NULL(parse("0-2,5 * * * * *", s, nullptr));
  assertMask(s.sec, bitsOf({0, 1, 2, 5}));
}

// Spec 7.1 edge rule: dom starts at 1, so */2 covers the odd days and never
// bit 0. Standard cron behaves the same way; this pins it.
static void test_parse_dom_step_starts_at_one(void) {
  Spec s;
  TEST_ASSERT_NULL(parse("* * * */2 * *", s, nullptr));
  assertMask(s.dom, bitsOf({1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27,
                           29, 31}));
  TEST_ASSERT_EQUAL_INT(0, s.dom & 1);
}

static void test_parse_dow_step_starts_at_zero(void) {
  Spec s;
  TEST_ASSERT_NULL(parse("* * * * * */2", s, nullptr));
  assertMask(s.dow, bitsOf({0, 2, 4, 6}));
}

static void test_parse_dow_zero_is_sunday(void) {
  Spec s;
  TEST_ASSERT_NULL(parse("* * * * * 0", s, nullptr));
  assertMask(s.dow, bitsOf({0}));
}

static void test_parse_dow_six_is_saturday(void) {
  Spec s;
  TEST_ASSERT_NULL(parse("* * * * * 6", s, nullptr));
  assertMask(s.dow, bitsOf({6}));
}

static void test_parse_rejects_five_fields(void) {
  assertError("* * * * *", "expected 6 fields", -1);
}

static void test_parse_rejects_seven_fields(void) {
  assertError("* * * * * * *", "expected 6 fields", -1);
}

static void test_parse_rejects_second_60(void) {
  assertError("60 * * * * *", "value out of range", 0);
}

static void test_parse_rejects_hour_24(void) {
  assertError("* * 24 * * *", "value out of range", 2);
}

static void test_parse_rejects_dom_32(void) {
  assertError("* * * 32 * *", "value out of range", 3);
}

static void test_parse_rejects_month_13(void) {
  assertError("* * * * 13 *", "value out of range", 4);
}

static void test_parse_rejects_dow_7(void) {
  assertError("* * * * * 7", "value out of range", 5);
}

static void test_parse_rejects_reversed_range(void) {
  assertError("10-5 * * * * *", "reversed range", 0);
}

static void test_parse_rejects_zero_step(void) {
  assertError("*/0 * * * * *", "step must be >= 1", 0);
}

static void test_parse_rejects_garbage(void) {
  assertError("foo * * * * *", "unsupported syntax", 0);
}

static void test_parse_sets_dom_restricted(void) {
  Spec s;
  TEST_ASSERT_NULL(parse("* * * 15 * *", s, nullptr));
  TEST_ASSERT_TRUE(s.domRestricted);
  TEST_ASSERT_FALSE(s.dowRestricted);
  assertMask(s.dom, bitsOf({15}));
}

static void test_parse_sets_dow_restricted(void) {
  Spec s;
  TEST_ASSERT_NULL(parse("* * * * * 1", s, nullptr));
  TEST_ASSERT_TRUE(s.dowRestricted);
  TEST_ASSERT_FALSE(s.domRestricted);
}

static void test_parse_accepts_extra_whitespace(void) {
  Spec s;
  TEST_ASSERT_NULL(parse("  */5   0 9  * * 1 ", s, nullptr));
  assertMask(s.sec, bitsOf({0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55}));
  assertMask(s.minute, bitsOf({0}));
  assertMask(s.hour, bitsOf({9}));
  assertMask(s.dom, rangeBits(1, 31));
  assertMask(s.month, rangeBits(1, 12));
  assertMask(s.dow, bitsOf({1}));
  TEST_ASSERT_FALSE(s.domRestricted);
  TEST_ASSERT_TRUE(s.dowRestricted);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_parse_all_stars);
  RUN_TEST(test_parse_single_second);
  RUN_TEST(test_parse_range);
  RUN_TEST(test_parse_step_from_star_sec);
  RUN_TEST(test_parse_step_from_range);
  RUN_TEST(test_parse_list);
  RUN_TEST(test_parse_list_of_ranges);
  RUN_TEST(test_parse_dom_step_starts_at_one);
  RUN_TEST(test_parse_dow_step_starts_at_zero);
  RUN_TEST(test_parse_dow_zero_is_sunday);
  RUN_TEST(test_parse_dow_six_is_saturday);
  RUN_TEST(test_parse_rejects_five_fields);
  RUN_TEST(test_parse_rejects_seven_fields);
  RUN_TEST(test_parse_rejects_second_60);
  RUN_TEST(test_parse_rejects_hour_24);
  RUN_TEST(test_parse_rejects_dom_32);
  RUN_TEST(test_parse_rejects_month_13);
  RUN_TEST(test_parse_rejects_dow_7);
  RUN_TEST(test_parse_rejects_reversed_range);
  RUN_TEST(test_parse_rejects_zero_step);
  RUN_TEST(test_parse_rejects_garbage);
  RUN_TEST(test_parse_sets_dom_restricted);
  RUN_TEST(test_parse_sets_dow_restricted);
  RUN_TEST(test_parse_accepts_extra_whitespace);
  return UNITY_END();
}
