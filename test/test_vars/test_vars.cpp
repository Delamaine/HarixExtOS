#include <unity.h>

#include <cstdio>
#include <cstring>

#include "api/vars.h"

using harixos::api::vars::clear;
using harixos::api::vars::count;
using harixos::api::vars::get;
using harixos::api::vars::isValidName;
using harixos::api::vars::runCommand;
using harixos::api::vars::set;

static char g_out[1024];
static size_t g_outLen;

static void captureLine(const char *line, void *) {
  int n = snprintf(g_out + g_outLen, sizeof(g_out) - g_outLen, "%s\n", line);
  if (n > 0) g_outLen += (size_t)n;
}

static void resetOut(void) {
  g_out[0] = '\0';
  g_outLen = 0;
}

void setUp(void) {
  clear();
  resetOut();
}
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

static void test_run_command_bare_returns_usage(void) {
  const char *err = runCommand("", captureLine, nullptr);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(err, "Usage: vars"));
}

static void test_run_command_null_returns_usage(void) {
  const char *err = runCommand(nullptr, captureLine, nullptr);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(err, "Usage: vars"));
}

static void test_run_command_unknown_action(void) {
  const char *err = runCommand("bogus", captureLine, nullptr);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(err, "Unknown vars action"));
}

static void test_run_command_set_stores_and_echoes(void) {
  const char *err = runCommand("set a=42", captureLine, nullptr);
  if (err != nullptr) {
    printf("runCommand(set) error: %s\n", err);
    TEST_FAIL_MESSAGE(err);
  }
  double v = 0;
  TEST_ASSERT_TRUE(get("a", v));
  TEST_ASSERT_EQUAL_DOUBLE(42, v);
  TEST_ASSERT_NOT_NULL(strstr(g_out, "a = 42"));
}

static void test_run_command_set_invalid_expression_is_error(void) {
  const char *err = runCommand("set a=", captureLine, nullptr);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_UINT(0, count());
}

static void test_run_command_get_emits_value(void) {
  runCommand("set a=42", captureLine, nullptr);
  resetOut();
  const char *err = runCommand("get a", captureLine, nullptr);
  TEST_ASSERT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(g_out, "a = 42"));
}

static void test_run_command_get_missing_is_error(void) {
  const char *err = runCommand("get nope", captureLine, nullptr);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(err, "nope not found"));
}

static void test_run_command_list_shows_header_count_and_value(void) {
  runCommand("set a=42", captureLine, nullptr);
  resetOut();
  const char *err = runCommand("list", captureLine, nullptr);
  TEST_ASSERT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(g_out, "Variables:"));
  TEST_ASSERT_NOT_NULL(strstr(g_out, "a=42"));
  TEST_ASSERT_NOT_NULL(strstr(g_out, "Count: 1/16"));
}

static void test_run_command_del_removes(void) {
  runCommand("set a=42", captureLine, nullptr);
  resetOut();
  const char *err = runCommand("del a", captureLine, nullptr);
  TEST_ASSERT_NULL(err);
  TEST_ASSERT_EQUAL_UINT(0, count());
  TEST_ASSERT_NOT_NULL(strstr(g_out, "a deleted."));
}

static void test_run_command_del_missing_is_error(void) {
  const char *err = runCommand("del nope", captureLine, nullptr);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(err, "nope not found"));
}

static void test_run_command_clear_emits_confirmation(void) {
  runCommand("set a=42", captureLine, nullptr);
  resetOut();
  const char *err = runCommand("clear", captureLine, nullptr);
  TEST_ASSERT_NULL(err);
  TEST_ASSERT_EQUAL_UINT(0, count());
  TEST_ASSERT_NOT_NULL(strstr(g_out, "All variables cleared."));
}

static void test_run_command_save_fails_without_filesystem(void) {
  const char *err = runCommand("save", captureLine, nullptr);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_NOT_NULL(strstr(err, "Failed to save"));
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
  RUN_TEST(test_run_command_bare_returns_usage);
  RUN_TEST(test_run_command_null_returns_usage);
  RUN_TEST(test_run_command_unknown_action);
  RUN_TEST(test_run_command_set_stores_and_echoes);
  RUN_TEST(test_run_command_set_invalid_expression_is_error);
  RUN_TEST(test_run_command_get_emits_value);
  RUN_TEST(test_run_command_get_missing_is_error);
  RUN_TEST(test_run_command_list_shows_header_count_and_value);
  RUN_TEST(test_run_command_del_removes);
  RUN_TEST(test_run_command_del_missing_is_error);
  RUN_TEST(test_run_command_clear_emits_confirmation);
  RUN_TEST(test_run_command_save_fails_without_filesystem);
  return UNITY_END();
}
