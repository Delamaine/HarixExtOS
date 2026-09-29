#include <unity.h>

#include "api/block_stack.h"

using harixos::api::BlockEvent;
using harixos::api::BlockStack;
using harixos::api::kMaxBlockDepth;

void setUp(void) {}
void tearDown(void) {}

static void test_top_level_runs(void) {
  BlockStack s;
  TEST_ASSERT_FALSE(s.skipping());
  TEST_ASSERT_EQUAL_INT((int)0, (int)s.depth());
}

static void test_true_condition_runs_body(void) {
  BlockStack s;
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::If, true));
  TEST_ASSERT_FALSE(s.skipping());
}

static void test_false_condition_skips(void) {
  BlockStack s;
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::If, false));
  TEST_ASSERT_TRUE(s.skipping());
}

static void test_else_switches_to_running(void) {
  BlockStack s;
  s.onEvent(BlockEvent::If, false);
  TEST_ASSERT_TRUE(s.skipping());
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::Else, false));
  TEST_ASSERT_FALSE(s.skipping());
}

static void test_else_after_true_switches_to_skipping(void) {
  BlockStack s;
  s.onEvent(BlockEvent::If, true);
  TEST_ASSERT_FALSE(s.skipping());
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::Else, false));
  TEST_ASSERT_TRUE(s.skipping());
}

static void test_end_after_true_branch_closes(void) {
  BlockStack s;
  s.onEvent(BlockEvent::If, true);
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::End, false));
  TEST_ASSERT_EQUAL_INT((int)0, (int)s.depth());
  TEST_ASSERT_EQUAL_INT(0, s.unclosedCount());
}

static void test_end_after_false_branch_skips_to_close(void) {
  BlockStack s;
  s.onEvent(BlockEvent::If, false);
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::End, false));
  TEST_ASSERT_EQUAL_INT((int)0, (int)s.depth());
  TEST_ASSERT_FALSE(s.skipping());
}

static void test_nested_if_inside_skipped_branch(void) {
  BlockStack s;
  s.onEvent(BlockEvent::If, false);
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::If, true));
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::End, false));
  TEST_ASSERT_EQUAL_INT((int)1, (int)s.depth());
  TEST_ASSERT_TRUE(s.skipping());
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::End, false));
  TEST_ASSERT_EQUAL_INT((int)0, (int)s.depth());
}

static void test_else_inside_skipped_branch_ignored(void) {
  BlockStack s;
  s.onEvent(BlockEvent::If, false);
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::If, false));
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::Else, false));
  TEST_ASSERT_TRUE(s.skipping());
  TEST_ASSERT_EQUAL_INT((int)2, (int)s.depth());
}

static void test_other_event_while_skipping_does_not_unskip(void) {
  BlockStack s;
  s.onEvent(BlockEvent::If, false);
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::Other, false));
  TEST_ASSERT_TRUE(s.skipping());
}

static void test_else_without_if_errors(void) {
  BlockStack s;
  const char *err = s.onEvent(BlockEvent::Else, false);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("else without matching if", err);
}

static void test_end_without_if_errors(void) {
  BlockStack s;
  const char *err = s.onEvent(BlockEvent::End, false);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("end without matching if", err);
}

static void test_depth_overflow_errors(void) {
  BlockStack s;
  for (size_t i = 0; i < kMaxBlockDepth; ++i) {
    TEST_ASSERT_NULL(s.onEvent(BlockEvent::If, true));
  }
  TEST_ASSERT_EQUAL_INT((int)kMaxBlockDepth, (int)s.depth());
  const char *err = s.onEvent(BlockEvent::If, true);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("Block nesting limit exceeded", err);
}

static void test_unclosed_count_reports(void) {
  BlockStack s;
  s.onEvent(BlockEvent::If, true);
  TEST_ASSERT_EQUAL_INT(1, s.unclosedCount());
}

static void test_reset_clears(void) {
  BlockStack s;
  s.onEvent(BlockEvent::If, true);
  s.reset();
  TEST_ASSERT_EQUAL_INT((int)0, (int)s.depth());
  TEST_ASSERT_FALSE(s.skipping());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_top_level_runs);
  RUN_TEST(test_true_condition_runs_body);
  RUN_TEST(test_false_condition_skips);
  RUN_TEST(test_else_switches_to_running);
  RUN_TEST(test_else_after_true_switches_to_skipping);
  RUN_TEST(test_end_after_true_branch_closes);
  RUN_TEST(test_end_after_false_branch_skips_to_close);
  RUN_TEST(test_nested_if_inside_skipped_branch);
  RUN_TEST(test_else_inside_skipped_branch_ignored);
  RUN_TEST(test_other_event_while_skipping_does_not_unskip);
  RUN_TEST(test_else_without_if_errors);
  RUN_TEST(test_end_without_if_errors);
  RUN_TEST(test_depth_overflow_errors);
  RUN_TEST(test_unclosed_count_reports);
  RUN_TEST(test_reset_clears);
  return UNITY_END();
}
