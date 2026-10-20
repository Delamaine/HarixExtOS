#include <unity.h>

#include <cstdio>
#include <cstring>

#include "api/block_stack.h"

using harixos::api::BlockEvent;
using harixos::api::BlockStack;

void setUp(void) {}
void tearDown(void) {}

// try frame basics: pushes, pops on `end`, and normal catch entry.
static void test_try_pops_on_end(void) {
  BlockStack b;
  TEST_ASSERT_NULL(b.onTry(123));
  TEST_ASSERT_EQUAL_UINT(1, b.depth());
  TEST_ASSERT_FALSE(b.skipping());
  TEST_ASSERT_NULL(b.onEvent(BlockEvent::End, false));
  TEST_ASSERT_EQUAL_UINT(0, b.depth());
  TEST_ASSERT_EQUAL_INT(0, b.unclosedCount());
}

static void test_catch_skipped_when_try_succeeded(void) {
  BlockStack b;
  TEST_ASSERT_NULL(b.onTry(50));
  TEST_ASSERT_NULL(b.onCatch());  // reached with no error armed
  TEST_ASSERT_TRUE(b.skipping());  // catch body must NOT run
  TEST_ASSERT_NULL(b.onEvent(BlockEvent::End, false));
  TEST_ASSERT_EQUAL_UINT(0, b.depth());
}

// Core: error inside try's body jumps to catchStart; abandoned inner if
// frame is popped, the try frame stays until its `end`. The catch line,
// reached via the error jump, enters the catch body (not skipping).
static void test_error_in_try_jumps_to_catch(void) {
  BlockStack b;
  TEST_ASSERT_NULL(b.onTry(200));
  TEST_ASSERT_NULL(b.onEvent(BlockEvent::If, true));  // if inside try
  int jump = -1;
  TEST_ASSERT_TRUE(b.onError(jump));
  TEST_ASSERT_EQUAL_INT(200, jump);
  TEST_ASSERT_EQUAL_UINT(1, b.depth());  // if popped, try remains
  TEST_ASSERT_NULL(b.onCatch());         // engine lands on the catch line
  TEST_ASSERT_FALSE(b.skipping());       // catch body runs after an error
  TEST_ASSERT_NULL(b.onEvent(BlockEvent::End, false));
  TEST_ASSERT_EQUAL_UINT(0, b.depth());
}

// Unhandled error: no mutation at all (enclosing blocks survive).
static void test_error_unhandled_is_noop(void) {
  BlockStack b;
  TEST_ASSERT_NULL(b.onEvent(BlockEvent::If, true));
  int jump = -1;
  TEST_ASSERT_FALSE(b.onError(jump));
  TEST_ASSERT_EQUAL_UINT(1, b.depth());
  TEST_ASSERT_NULL(b.onEvent(BlockEvent::End, false));
  TEST_ASSERT_EQUAL_UINT(0, b.depth());
}

// Error inside a catch bubbles to the next OUTER try.
static void test_error_in_catch_bubbles_to_outer(void) {
  BlockStack b;
  TEST_ASSERT_NULL(b.onTry(10));   // outer, catch at 10
  TEST_ASSERT_NULL(b.onTry(200));  // inner, catch at 200
  TEST_ASSERT_NULL(b.onCatch());   // enter inner catch
  int jump = -1;
  TEST_ASSERT_TRUE(b.onError(jump));
  TEST_ASSERT_EQUAL_INT(10, jump);
  TEST_ASSERT_EQUAL_UINT(1, b.depth());  // inner try popped, outer stays
  TEST_ASSERT_NULL(b.onEvent(BlockEvent::End, false));
  TEST_ASSERT_EQUAL_UINT(0, b.depth());
}

// Error inside catch with no outer try: unhandled, frames survive so the
// catch block's `end` still closes normally.
static void test_error_in_catch_unhandled_keeps_frame(void) {
  BlockStack b;
  TEST_ASSERT_NULL(b.onTry(200));
  TEST_ASSERT_NULL(b.onCatch());
  int jump = -1;
  TEST_ASSERT_FALSE(b.onError(jump));
  TEST_ASSERT_EQUAL_UINT(1, b.depth());
  TEST_ASSERT_NULL(b.onEvent(BlockEvent::End, false));
  TEST_ASSERT_EQUAL_UINT(0, b.depth());
}

// try entered from a skipped region: catch must never run (consumed).
static void test_try_inside_skipped_if_catch_stays_skipped(void) {
  BlockStack b;
  TEST_ASSERT_NULL(b.onEvent(BlockEvent::If, false));  // skipped if
  TEST_ASSERT_NULL(b.onTry(77));
  TEST_ASSERT_NULL(b.onCatch());
  TEST_ASSERT_TRUE(b.skipping());  // catch body skipped
  TEST_ASSERT_NULL(b.onEvent(BlockEvent::End, false));  // catch end
  TEST_ASSERT_NULL(b.onEvent(BlockEvent::End, false));  // if end
  TEST_ASSERT_EQUAL_UINT(0, b.depth());
}

// break inside a try: marks the try consumed, so its catch never revives.
static void test_break_through_try_skips_catch(void) {
  BlockStack b;
  TEST_ASSERT_NULL(b.onWhile(0, 10, true));
  TEST_ASSERT_NULL(b.onTry(55));
  TEST_ASSERT_NULL(b.onBreak());
  int jump = -1;
  TEST_ASSERT_FALSE(b.onError(jump));  // consumed try does not catch
  TEST_ASSERT_NULL(b.onEvent(BlockEvent::End, false));  // catch end
  TEST_ASSERT_NULL(b.onEvent(BlockEvent::End, false));  // while end
  TEST_ASSERT_EQUAL_UINT(0, b.depth());
}

// try nested inside a running while: error caught, while frame survives.
static void test_error_in_try_inside_while_keeps_loop(void) {
  BlockStack b;
  TEST_ASSERT_NULL(b.onWhile(0, 10, true));
  TEST_ASSERT_NULL(b.onTry(99));
  int jump = -1;
  TEST_ASSERT_TRUE(b.onError(jump));
  TEST_ASSERT_EQUAL_INT(99, jump);
  TEST_ASSERT_EQUAL_UINT(2, b.depth());  // while + try
  TEST_ASSERT_NULL(b.onEvent(BlockEvent::End, false));  // try end pops
  TEST_ASSERT_EQUAL_UINT(1, b.depth());
  TEST_ASSERT_NULL(b.onEvent(BlockEvent::End, true));   // while loop-back
  TEST_ASSERT_EQUAL_UINT(1, b.depth());  // try re-entered on next pass
}

// Structural errors.
static void test_catch_without_try_is_error(void) {
  BlockStack b;
  TEST_ASSERT_NOT_NULL(b.onCatch());
  TEST_ASSERT_NULL(b.onEvent(BlockEvent::If, true));
  TEST_ASSERT_NOT_NULL(b.onCatch());  // top is if, not try
}

static void test_else_on_try_is_error(void) {
  BlockStack b;
  TEST_ASSERT_NULL(b.onTry(30));
  TEST_ASSERT_NOT_NULL(b.onEvent(BlockEvent::Else, true));
}

static void test_try_depth_limit(void) {
  BlockStack b;
  for (int i = 0; i < (int)harixos::api::kMaxBlockDepth; ++i) {
    TEST_ASSERT_NULL(b.onTry(i * 10));
  }
  TEST_ASSERT_NOT_NULL(b.onTry(999));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_try_pops_on_end);
  RUN_TEST(test_catch_skipped_when_try_succeeded);
  RUN_TEST(test_error_in_try_jumps_to_catch);
  RUN_TEST(test_error_unhandled_is_noop);
  RUN_TEST(test_error_in_catch_bubbles_to_outer);
  RUN_TEST(test_error_in_catch_unhandled_keeps_frame);
  RUN_TEST(test_try_inside_skipped_if_catch_stays_skipped);
  RUN_TEST(test_break_through_try_skips_catch);
  RUN_TEST(test_error_in_try_inside_while_keeps_loop);
  RUN_TEST(test_catch_without_try_is_error);
  RUN_TEST(test_else_on_try_is_error);
  RUN_TEST(test_try_depth_limit);
  return UNITY_END();
}
