#include <unity.h>
#include <string.h>
#include "kernel/crash_log.h"

using namespace harixos;

static void test_format_line_exact(void) {
  char b[kCrashLineMax];
  int r = crashFormatLine(b, sizeof(b), "Exception", 2, 0x4020abcd,
                          0x3ffffffe, 0x0, 5);
  const char* want = "Exception|2|0x4020ABCD|0x3FFFFFFE|0x0|boots=5";
  TEST_ASSERT_EQUAL_INT((int)strlen(want), r);
  TEST_ASSERT_EQUAL_STRING(want, b);
}

static void test_format_line_too_small(void) {
  char b[8];
  TEST_ASSERT_EQUAL_INT(-1, crashFormatLine(b, sizeof(b), "Exception", 2,
                                            0x4020abcd, 0x3ffffffe, 0x0, 5));
}

static void test_abnormal_classification(void) {
  TEST_ASSERT_TRUE(crashReasonIsAbnormal(1));   // hardware WDT
  TEST_ASSERT_TRUE(crashReasonIsAbnormal(2));   // exception
  TEST_ASSERT_TRUE(crashReasonIsAbnormal(3));   // software WDT
  TEST_ASSERT_FALSE(crashReasonIsAbnormal(0));  // power-on
  TEST_ASSERT_FALSE(crashReasonIsAbnormal(4));  // software restart
  TEST_ASSERT_FALSE(crashReasonIsAbnormal(5));  // deep-sleep wake
  TEST_ASSERT_FALSE(crashReasonIsAbnormal(6));  // external reset
}

static void test_boot_count(void) {
  TEST_ASSERT_EQUAL_INT(0, crashBootCount(""));
  TEST_ASSERT_EQUAL_INT(0, crashBootCount("garbage no counter\n"));
  TEST_ASSERT_EQUAL_INT(7, crashBootCount("Exception|2|0x0|0x0|0x0|boots=7\n"));
  // last line wins
  TEST_ASSERT_EQUAL_INT(9, crashBootCount(
      "Exception|2|0x0|0x0|0x0|boots=7\n"
      "Power on|0|0x0|0x0|0x0|boots=9\n"));
}

static void test_append_empty(void) {
  char out[kCrashLogMax];
  int n = crashAppendLine(out, sizeof(out), "", "L1");
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_STRING("L1\n", out);
}

static void test_append_caps_to_eight(void) {
  char out[kCrashLogMax];
  int n = crashAppendLine(out, sizeof(out),
      "L1\nL2\nL3\nL4\nL5\nL6\nL7\nL8\n", "L9");
  TEST_ASSERT_EQUAL_INT(8, n);
  TEST_ASSERT_EQUAL_STRING("L2\nL3\nL4\nL5\nL6\nL7\nL8\nL9\n", out);
}

static void test_append_overflow(void) {
  char out[8];
  out[0] = 'x';
  TEST_ASSERT_EQUAL_INT(-1, crashAppendLine(out, sizeof(out), "abcdefghij", "L1"));
  TEST_ASSERT_EQUAL_STRING("", out);
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_format_line_exact);
  RUN_TEST(test_format_line_too_small);
  RUN_TEST(test_abnormal_classification);
  RUN_TEST(test_boot_count);
  RUN_TEST(test_append_empty);
  RUN_TEST(test_append_caps_to_eight);
  RUN_TEST(test_append_overflow);
  return UNITY_END();
}
