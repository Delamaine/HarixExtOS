#include <unity.h>

#include <cmath>
#include <cstdio>

#include "api/expr.h"

using harixos::api::expr::evaluateArithmetic;

void setUp(void) {}
void tearDown(void) {}

static void test_precedence_multiply_over_add(void) {
  double v = 0;
  TEST_ASSERT_TRUE(evaluateArithmetic("1+2*3", v));
  TEST_ASSERT_EQUAL_DOUBLE(7, v);
}

static void test_parentheses_override(void) {
  double v = 0;
  TEST_ASSERT_TRUE(evaluateArithmetic("(1+2)*3", v));
  TEST_ASSERT_EQUAL_DOUBLE(9, v);
}

static void test_left_associative_subtraction(void) {
  double v = 0;
  TEST_ASSERT_TRUE(evaluateArithmetic("2-3-4", v));
  TEST_ASSERT_EQUAL_DOUBLE(-5, v);
}

static void test_decimal_division(void) {
  double v = 0;
  TEST_ASSERT_TRUE(evaluateArithmetic("10/4", v));
  TEST_ASSERT_EQUAL_DOUBLE(2.5, v);
}

static void test_division_by_zero_is_nan(void) {
  double v = 0;
  TEST_ASSERT_TRUE(evaluateArithmetic("1/0", v));
  TEST_ASSERT_TRUE(std::isnan(v));
}

static void test_empty_expression_fails(void) {
  double v = 0;
  TEST_ASSERT_FALSE(evaluateArithmetic("", v));
}

static void test_trailing_operator_fails(void) {
  double v = 0;
  TEST_ASSERT_FALSE(evaluateArithmetic("1+", v));
}

static void test_unknown_char_fails(void) {
  double v = 0;
  TEST_ASSERT_FALSE(evaluateArithmetic("abc", v));
}

static void test_unbalanced_paren_fails(void) {
  double v = 0;
  TEST_ASSERT_FALSE(evaluateArithmetic("(1+2", v));
}

static void test_whitespace_ignored(void) {
  double v = 0;
  TEST_ASSERT_TRUE(evaluateArithmetic("  1  +  2 ", v));
  TEST_ASSERT_EQUAL_DOUBLE(3, v);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_precedence_multiply_over_add);
  RUN_TEST(test_parentheses_override);
  RUN_TEST(test_left_associative_subtraction);
  RUN_TEST(test_decimal_division);
  RUN_TEST(test_division_by_zero_is_nan);
  RUN_TEST(test_empty_expression_fails);
  RUN_TEST(test_trailing_operator_fails);
  RUN_TEST(test_unknown_char_fails);
  RUN_TEST(test_unbalanced_paren_fails);
  RUN_TEST(test_whitespace_ignored);
  return UNITY_END();
}
