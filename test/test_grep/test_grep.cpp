#include <unity.h>

#include <cstring>

#include "kernel/filesystem/grep_logic.h"

using harixos::grep::lineMatches;

void setUp(void) {}
void tearDown(void) {}

static void test_literal_substring_found(void) {
  TEST_ASSERT_TRUE(lineMatches("gpio 12 pulse", "12", false, false));
}

static void test_literal_substring_missing(void) {
  TEST_ASSERT_FALSE(lineMatches("gpio 12 pulse", "13", false, false));
}

static void test_default_is_case_sensitive(void) {
  TEST_ASSERT_FALSE(lineMatches("Sensor Readings", "sensor", false, false));
  TEST_ASSERT_TRUE(lineMatches("Sensor Readings", "Sensor", false, false));
}

static void test_icase_matches_any_case(void) {
  TEST_ASSERT_TRUE(lineMatches("Sensor Readings", "sensor", true, false));
  TEST_ASSERT_TRUE(lineMatches("sensor readings", "SENSOR", true, false));
}

static void test_invert_keeps_non_matching_lines(void) {
  TEST_ASSERT_TRUE(lineMatches("keep this", "drop", false, true));
  TEST_ASSERT_FALSE(lineMatches("drop this", "drop", false, true));
}

static void test_invert_with_icase(void) {
  TEST_ASSERT_TRUE(lineMatches("SKIP THAT", "keep", true, true));
  TEST_ASSERT_FALSE(lineMatches("keep this", "keep", true, true));
  TEST_ASSERT_FALSE(lineMatches("KEEP THIS", "keep", true, true));
}

static void test_empty_pattern_matches_every_line(void) {
  TEST_ASSERT_TRUE(lineMatches("anything", "", false, false));
  TEST_ASSERT_FALSE(lineMatches("anything", "", false, true));
}

static void test_pattern_longer_than_line(void) {
  TEST_ASSERT_FALSE(lineMatches("short", "much longer pattern", false, false));
}

static void test_match_at_line_boundaries(void) {
  TEST_ASSERT_TRUE(lineMatches("abc start", "abc", false, false));
  TEST_ASSERT_TRUE(lineMatches("start abc", "abc", false, false));
  TEST_ASSERT_TRUE(lineMatches("has abc inside", "abc", false, false));
}

static void test_dot_is_literal_not_regex(void) {
  TEST_ASSERT_TRUE(lineMatches("a.b", ".", false, false));
  TEST_ASSERT_FALSE(lineMatches("axb", "a.b", false, false));
}

static void test_star_is_literal_not_regex(void) {
  TEST_ASSERT_FALSE(lineMatches("abc", "a*c", false, false));
  TEST_ASSERT_TRUE(lineMatches("a*c", "a*c", false, false));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_literal_substring_found);
  RUN_TEST(test_literal_substring_missing);
  RUN_TEST(test_default_is_case_sensitive);
  RUN_TEST(test_icase_matches_any_case);
  RUN_TEST(test_invert_keeps_non_matching_lines);
  RUN_TEST(test_invert_with_icase);
  RUN_TEST(test_empty_pattern_matches_every_line);
  RUN_TEST(test_pattern_longer_than_line);
  RUN_TEST(test_match_at_line_boundaries);
  RUN_TEST(test_dot_is_literal_not_regex);
  RUN_TEST(test_star_is_literal_not_regex);
  return UNITY_END();
}
