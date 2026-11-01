#include <unity.h>

#include <stdio.h>

#include "api/cmd_history.h"

using namespace harixos::api::history;

void setUp() { reset(); }
void tearDown() {}

void test_push_get_count() {
  TEST_ASSERT_EQUAL_size_t(0, count());
  push("one");
  push("two");
  TEST_ASSERT_EQUAL_size_t(2, count());
  TEST_ASSERT_EQUAL_STRING("two", get(1));
  TEST_ASSERT_EQUAL_STRING("one", get(2));
  TEST_ASSERT_NULL(get(3));
}

void test_push_skips_empty_and_immediate_repeat() {
  push("alpha");
  push("alpha");
  push("");
  TEST_ASSERT_EQUAL_size_t(1, count());
}

void test_at_absolute_numbering() {
  push("first");
  push("second");
  push("third");
  TEST_ASSERT_EQUAL_STRING("first", at(1));
  TEST_ASSERT_EQUAL_STRING("second", at(2));
  TEST_ASSERT_EQUAL_STRING("third", at(3));
  TEST_ASSERT_NULL(at(0));
  TEST_ASSERT_NULL(at(4));
}

void test_at_after_wrap() {
  for (int i = 0; i < 18; ++i) {  // capacity is 16: oldest two evicted
    char line[16];
    snprintf(line, sizeof(line), "cmd%02d", i);
    push(line);
  }
  TEST_ASSERT_EQUAL_size_t(16, count());
  TEST_ASSERT_EQUAL_STRING("cmd02", at(1));
  TEST_ASSERT_EQUAL_STRING("cmd17", at(16));
}

void test_expand_passes_non_bang_through() {
  char out[kMaxLine + 1];
  TEST_ASSERT_TRUE(expand("uptime", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("uptime", out);
  TEST_ASSERT_TRUE(expand("!5 uptime", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("!5 uptime", out);
  TEST_ASSERT_TRUE(expand("", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
}

void test_expand_valid_reference() {
  char out[kMaxLine + 1];
  push("first");
  push("second");
  TEST_ASSERT_TRUE(expand("!1", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("first", out);
  TEST_ASSERT_TRUE(expand("!2", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("second", out);
}

void test_expand_out_of_range() {
  char out[kMaxLine + 1];
  push("only");
  TEST_ASSERT_FALSE(expand("!2", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("no history entry 2", out);
  TEST_ASSERT_FALSE(expand("!0", out, sizeof(out)));
  TEST_ASSERT_FALSE(expand("!99999999999", out, sizeof(out)));  // saturates
}

void test_expand_literal_bang_forms() {
  char out[kMaxLine + 1];
  TEST_ASSERT_TRUE(expand("!", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("!", out);
  TEST_ASSERT_TRUE(expand("!5 x", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("!5 x", out);
}

void test_canexec_guards() {
  char line[] = "uptime";
  TEST_ASSERT_TRUE(canExec(line));
  TEST_ASSERT_TRUE(canExec("exec"));
  TEST_ASSERT_FALSE(canExec("exec 3"));
  TEST_ASSERT_FALSE(canExec("EXEC 3"));
  TEST_ASSERT_FALSE(canExec("  exec 12"));
  TEST_ASSERT_FALSE(canExec("!5"));
  TEST_ASSERT_FALSE(canExec(nullptr));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_push_get_count);
  RUN_TEST(test_push_skips_empty_and_immediate_repeat);
  RUN_TEST(test_at_absolute_numbering);
  RUN_TEST(test_at_after_wrap);
  RUN_TEST(test_expand_passes_non_bang_through);
  RUN_TEST(test_expand_valid_reference);
  RUN_TEST(test_expand_out_of_range);
  RUN_TEST(test_expand_literal_bang_forms);
  RUN_TEST(test_canexec_guards);
  return UNITY_END();
}
