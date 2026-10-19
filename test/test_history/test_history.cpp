#include <unity.h>

#include <cstdio>
#include <cstring>

#include "api/cmd_history.h"

using harixos::api::history::count;
using harixos::api::history::get;
using harixos::api::history::kMaxLine;
using harixos::api::history::push;
using harixos::api::history::reset;

void setUp() { reset(); }
void tearDown() {}

void test_empty_after_reset() {
  TEST_ASSERT_EQUAL_UINT(0, count());
  TEST_ASSERT_NULL(get(1));
}

void test_push_one_newest_first() {
  push("heap");
  TEST_ASSERT_EQUAL_UINT(1, count());
  TEST_ASSERT_EQUAL_STRING("heap", get(1));
  TEST_ASSERT_NULL(get(2));
  TEST_ASSERT_NULL(get(0));
}

void test_push_order_newest_first() {
  push("first");
  push("second");
  push("third");
  TEST_ASSERT_EQUAL_UINT(3, count());
  TEST_ASSERT_EQUAL_STRING("third", get(1));
  TEST_ASSERT_EQUAL_STRING("second", get(2));
  TEST_ASSERT_EQUAL_STRING("first", get(3));
  TEST_ASSERT_NULL(get(4));
}

void test_skips_empty_and_null() {
  push("");
  push(nullptr);
  TEST_ASSERT_EQUAL_UINT(0, count());
}

void test_skips_immediate_duplicate() {
  push("heap");
  push("heap");
  TEST_ASSERT_EQUAL_UINT(1, count());
  TEST_ASSERT_EQUAL_STRING("heap", get(1));
}

void test_non_consecutive_repeat_stored() {
  push("a");
  push("b");
  push("a");
  TEST_ASSERT_EQUAL_UINT(3, count());
  TEST_ASSERT_EQUAL_STRING("a", get(1));
}

void test_ring_overflow_keeps_newest() {
  for (int i = 0; i < 20; ++i) {
    char buf[16];
    snprintf(buf, sizeof(buf), "cmd%d", i);
    push(buf);
  }
  TEST_ASSERT_EQUAL_UINT(16, count());
  TEST_ASSERT_EQUAL_STRING("cmd19", get(1));
  TEST_ASSERT_EQUAL_STRING("cmd4", get(16));   // oldest survivor
  TEST_ASSERT_NULL(get(17));
}

void test_long_line_truncated_to_cap() {
  char buf[kMaxLine + 20];
  memset(buf, 'x', sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  push(buf);
  const char* got = get(1);
  TEST_ASSERT_NOT_NULL(got);
  TEST_ASSERT_EQUAL_UINT(kMaxLine, strlen(got));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_after_reset);
  RUN_TEST(test_push_one_newest_first);
  RUN_TEST(test_push_order_newest_first);
  RUN_TEST(test_skips_empty_and_null);
  RUN_TEST(test_skips_immediate_duplicate);
  RUN_TEST(test_non_consecutive_repeat_stored);
  RUN_TEST(test_ring_overflow_keeps_newest);
  RUN_TEST(test_long_line_truncated_to_cap);
  return UNITY_END();
}
