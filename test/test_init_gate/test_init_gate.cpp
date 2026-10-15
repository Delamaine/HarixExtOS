#include <unity.h>

#include <cstdio>
#include <cstring>

#include "api/init_gate.h"

using harixos::api::clearInitDone;
using harixos::api::initAlreadyDone;
using harixos::api::markInitDone;

void setUp(void) { clearInitDone(); }
void tearDown(void) {}

static void test_unknown_path_not_done(void) {
  TEST_ASSERT_FALSE(initAlreadyDone("/apps/lamp-remote.hx"));
}

static void test_null_and_empty_are_ignored(void) {
  markInitDone(nullptr);
  markInitDone("");
  TEST_ASSERT_FALSE(initAlreadyDone(""));
  TEST_ASSERT_FALSE(initAlreadyDone(nullptr));
}

static void test_mark_then_done(void) {
  markInitDone("/apps/lamp-remote.hx");
  TEST_ASSERT_TRUE(initAlreadyDone("/apps/lamp-remote.hx"));
}

static void test_paths_are_distinct(void) {
  markInitDone("/apps/lamp-remote.hx");
  TEST_ASSERT_FALSE(initAlreadyDone("/apps/pump-interlock.hx"));
}

static void test_remark_is_noop(void) {
  markInitDone("/apps/lamp-remote.hx");
  markInitDone("/apps/lamp-remote.hx");
  TEST_ASSERT_TRUE(initAlreadyDone("/apps/lamp-remote.hx"));
}

static void test_clear_resets(void) {
  markInitDone("/apps/lamp-remote.hx");
  clearInitDone();
  TEST_ASSERT_FALSE(initAlreadyDone("/apps/lamp-remote.hx"));
}

static void test_overflow_beyond_table_ignored(void) {
  char path[64];
  for (int i = 0; i < 10; ++i) {
    snprintf(path, sizeof(path), "/apps/script%d.hx", i);
    markInitDone(path);
  }
  TEST_ASSERT_TRUE(initAlreadyDone("/apps/script7.hx"));
  TEST_ASSERT_FALSE(initAlreadyDone("/apps/script8.hx"));
  TEST_ASSERT_FALSE(initAlreadyDone("/apps/script9.hx"));
}

static void test_long_path_truncated_to_39_chars(void) {
  char path[100];
  memset(path, 'a', 99);
  path[99] = '\0';
  markInitDone(path);
  char prefix[40];
  memset(prefix, 'a', 39);
  prefix[39] = '\0';
  TEST_ASSERT_TRUE(initAlreadyDone(prefix));
  TEST_ASSERT_FALSE(initAlreadyDone(path));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_unknown_path_not_done);
  RUN_TEST(test_null_and_empty_are_ignored);
  RUN_TEST(test_mark_then_done);
  RUN_TEST(test_paths_are_distinct);
  RUN_TEST(test_remark_is_noop);
  RUN_TEST(test_clear_resets);
  RUN_TEST(test_overflow_beyond_table_ignored);
  RUN_TEST(test_long_path_truncated_to_39_chars);
  return UNITY_END();
}
