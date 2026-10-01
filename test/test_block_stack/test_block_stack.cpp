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

// --- while -----------------------------------------------------------------

static void test_while_true_cond_runs_body(void) {
  BlockStack s;
  TEST_ASSERT_NULL(s.onWhile(100, 150, true));
  TEST_ASSERT_FALSE(s.skipping());
  TEST_ASSERT_EQUAL_INT(1, (int)s.depth());
  TEST_ASSERT_TRUE(s.topIsWhile());
  TEST_ASSERT_TRUE(s.topRunning());
  TEST_ASSERT_EQUAL_INT(100, s.topLineStart());
}

static void test_while_false_cond_skips(void) {
  BlockStack s;
  TEST_ASSERT_NULL(s.onWhile(100, 150, false));
  TEST_ASSERT_TRUE(s.skipping());
  TEST_ASSERT_TRUE(s.topIsWhile());
  TEST_ASSERT_FALSE(s.topRunning());
}

static void test_while_end_true_jumps_back(void) {
  BlockStack s;
  s.onWhile(100, 150, true);
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::End, true));
  TEST_ASSERT_EQUAL_INT(1, (int)s.depth());   // frame kept for next pass
  TEST_ASSERT_EQUAL_INT(150, s.lastJumpTarget());
}

static void test_while_end_false_pops(void) {
  BlockStack s;
  s.onWhile(100, 150, true);
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::End, false));
  TEST_ASSERT_EQUAL_INT(0, (int)s.depth());
  TEST_ASSERT_EQUAL_INT(-1, s.lastJumpTarget());
  TEST_ASSERT_FALSE(s.skipping());
}

static void test_while_skipped_end_pops_without_jump(void) {
  BlockStack s;
  s.onWhile(100, 150, false);
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::End, true));  // cond ignored while skipping
  TEST_ASSERT_EQUAL_INT(0, (int)s.depth());
  TEST_ASSERT_EQUAL_INT(-1, s.lastJumpTarget());
}

static void test_jump_target_cleared_by_next_event(void) {
  BlockStack s;
  s.onWhile(100, 150, true);
  s.onEvent(BlockEvent::End, true);
  TEST_ASSERT_EQUAL_INT(150, s.lastJumpTarget());
  s.onEvent(BlockEvent::Other, false);
  TEST_ASSERT_EQUAL_INT(-1, s.lastJumpTarget());
}

static void test_while_iteration_cap_exceeded(void) {
  BlockStack s;
  s.onWhile(100, 150, true);
  const char *err = nullptr;
  for (size_t i = 0; i < harixos::api::kMaxWhileIterations; ++i) {
    err = s.onEvent(BlockEvent::End, true);
    TEST_ASSERT_NULL(err);
    TEST_ASSERT_EQUAL_INT(1, (int)s.depth());
  }
  err = s.onEvent(BlockEvent::End, true);  // one past the cap
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("while loop iteration limit exceeded", err);
  TEST_ASSERT_EQUAL_INT(0, (int)s.depth());
  TEST_ASSERT_EQUAL_INT(-1, s.lastJumpTarget());
}

static void test_while_time_cap_exceeded(void) {
  BlockStack s;
  s.onWhile(100, 150, true, /*nowMs=*/1000);
  const char *err = s.onEvent(BlockEvent::End, true,
                              1000 + harixos::api::kMaxWhileMs + 1);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("while loop time limit exceeded", err);
  TEST_ASSERT_EQUAL_INT(0, (int)s.depth());
  TEST_ASSERT_EQUAL_INT(-1, s.lastJumpTarget());
}

static void test_while_time_cap_at_boundary_still_loops(void) {
  BlockStack s;
  s.onWhile(100, 150, true, /*nowMs=*/1000);
  const char *err = s.onEvent(BlockEvent::End, true,
                              1000 + harixos::api::kMaxWhileMs);
  TEST_ASSERT_NULL(err);
  TEST_ASSERT_EQUAL_INT(150, s.lastJumpTarget());
}

static void test_else_on_while_errors(void) {
  BlockStack s;
  s.onWhile(100, 150, true);
  const char *err = s.onEvent(BlockEvent::Else, false);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("else without matching if", err);
}

static void test_while_inside_skipped_if_is_skipped(void) {
  BlockStack s;
  s.onEvent(BlockEvent::If, false);          // now skipping
  TEST_ASSERT_NULL(s.onWhile(100, 150, true));  // cond irrelevant
  TEST_ASSERT_TRUE(s.skipping());
  TEST_ASSERT_EQUAL_INT(2, (int)s.depth());
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::End, false));  // pops while
  TEST_ASSERT_EQUAL_INT(1, (int)s.depth());
  TEST_ASSERT_TRUE(s.skipping());
  TEST_ASSERT_EQUAL_INT(-1, s.lastJumpTarget());
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::End, false));  // pops if
  TEST_ASSERT_EQUAL_INT(0, (int)s.depth());
}

static void test_break_pops_to_nearest_while(void) {
  BlockStack s;
  s.onWhile(100, 150, true);
  s.onEvent(BlockEvent::If, true);           // if nested inside while
  TEST_ASSERT_NULL(s.onBreak());
  // The if's `end` line still lies ahead: frames above the while must be
  // kept (marked not-running) so each `end` pops its own frame in order.
  TEST_ASSERT_EQUAL_INT(2, (int)s.depth());
  TEST_ASSERT_TRUE(s.skipping());
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::End, false));  // closes the if
  TEST_ASSERT_EQUAL_INT(1, (int)s.depth());
  TEST_ASSERT_TRUE(s.skipping());            // while still walking
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::End, false));  // closes the while
  TEST_ASSERT_EQUAL_INT(0, (int)s.depth());
}

static void test_break_prevents_else_revival(void) {
  BlockStack s;
  s.onWhile(100, 150, true);
  s.onEvent(BlockEvent::If, true);           // break taken inside if-true
  TEST_ASSERT_NULL(s.onBreak());
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::Else, false));  // must not flip back
  TEST_ASSERT_TRUE(s.skipping());
  TEST_ASSERT_EQUAL_INT(2, (int)s.depth());
}

static void test_break_targets_nearest_of_nested_whiles(void) {
  BlockStack s;
  s.onWhile(100, 150, true);                 // outer
  s.onWhile(200, 250, true);                 // inner
  TEST_ASSERT_NULL(s.onBreak());
  TEST_ASSERT_EQUAL_INT(2, (int)s.depth());
  TEST_ASSERT_TRUE(s.skipping());            // inner walking to its end
  TEST_ASSERT_NULL(s.onEvent(BlockEvent::End, false));  // closes inner
  TEST_ASSERT_EQUAL_INT(1, (int)s.depth());
  TEST_ASSERT_FALSE(s.skipping());           // outer still running
}

static void test_break_without_while_errors(void) {
  BlockStack s;
  const char *err = s.onBreak();
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("break outside while loop", err);
}

static void test_break_inside_if_without_while_errors(void) {
  BlockStack s;
  s.onEvent(BlockEvent::If, true);
  const char *err = s.onBreak();
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("break outside while loop", err);
  TEST_ASSERT_EQUAL_INT(1, (int)s.depth());  // stack untouched
}

static void test_while_at_depth_limit_errors(void) {
  BlockStack s;
  for (size_t i = 0; i < kMaxBlockDepth; ++i) {
    TEST_ASSERT_NULL(s.onEvent(BlockEvent::If, true));
  }
  const char *err = s.onWhile(100, 150, true);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("Block nesting limit exceeded", err);
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
  RUN_TEST(test_while_true_cond_runs_body);
  RUN_TEST(test_while_false_cond_skips);
  RUN_TEST(test_while_end_true_jumps_back);
  RUN_TEST(test_while_end_false_pops);
  RUN_TEST(test_while_skipped_end_pops_without_jump);
  RUN_TEST(test_jump_target_cleared_by_next_event);
  RUN_TEST(test_while_iteration_cap_exceeded);
  RUN_TEST(test_while_time_cap_exceeded);
  RUN_TEST(test_while_time_cap_at_boundary_still_loops);
  RUN_TEST(test_else_on_while_errors);
  RUN_TEST(test_while_inside_skipped_if_is_skipped);
  RUN_TEST(test_break_pops_to_nearest_while);
  RUN_TEST(test_break_prevents_else_revival);
  RUN_TEST(test_break_targets_nearest_of_nested_whiles);
  RUN_TEST(test_break_without_while_errors);
  RUN_TEST(test_break_inside_if_without_while_errors);
  RUN_TEST(test_while_at_depth_limit_errors);
  return UNITY_END();
}
