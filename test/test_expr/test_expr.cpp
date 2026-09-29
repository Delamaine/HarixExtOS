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

static void test_unary_minus_standalone(void) {
  double v = 0;
  TEST_ASSERT_TRUE(evaluateArithmetic("-5", v));
  TEST_ASSERT_EQUAL_DOUBLE(-5, v);
}

static void test_unary_minus_after_operator(void) {
  double v = 0;
  TEST_ASSERT_TRUE(evaluateArithmetic("2 * -3", v));
  TEST_ASSERT_EQUAL_DOUBLE(-6, v);
}

static void test_double_unary(void) {
  double v = 0;
  TEST_ASSERT_TRUE(evaluateArithmetic("--5", v));
  TEST_ASSERT_EQUAL_DOUBLE(5, v);
}

static void test_less_than(void) {
  double v = 0;
  TEST_ASSERT_TRUE(evaluateArithmetic("1 < 2", v));
  TEST_ASSERT_EQUAL_DOUBLE(1, v);
}

static void test_greater_than_false(void) {
  double v = 0;
  TEST_ASSERT_TRUE(evaluateArithmetic("3 > 4", v));
  TEST_ASSERT_EQUAL_DOUBLE(0, v);
}

static void test_less_equal(void) {
  double v = 0;
  TEST_ASSERT_TRUE(evaluateArithmetic("2 <= 2", v));
  TEST_ASSERT_EQUAL_DOUBLE(1, v);
}

static void test_greater_equal(void) {
  double v = 0;
  TEST_ASSERT_TRUE(evaluateArithmetic("3 >= 4", v));
  TEST_ASSERT_EQUAL_DOUBLE(0, v);
}

static void test_equality_true(void) {
  double v = 0;
  TEST_ASSERT_TRUE(evaluateArithmetic("2 == 2", v));
  TEST_ASSERT_EQUAL_DOUBLE(1, v);
}

static void test_inequality(void) {
  double v = 0;
  TEST_ASSERT_TRUE(evaluateArithmetic("1 != 2", v));
  TEST_ASSERT_EQUAL_DOUBLE(1, v);
}

static void test_comparison_lower_precedence_than_arithmetic(void) {
  double v = 0;
  TEST_ASSERT_TRUE(evaluateArithmetic("1 + 2 < 4", v));
  TEST_ASSERT_EQUAL_DOUBLE(1, v);
}

static void test_equality_lowest_precedence(void) {
  double v = 0;
  TEST_ASSERT_TRUE(evaluateArithmetic("3 > 2 == 1", v));  // (3>2)==1
  TEST_ASSERT_EQUAL_DOUBLE(1, v);
}

static void test_comparison_over_unbalanced_parens(void) {
  double v = 0;
  TEST_ASSERT_FALSE(evaluateArithmetic("(1 < 2", v));
}

static void test_operand_overflow_fails(void) {
  // 130 operands with no operators: valStack would need 130 slots but
  // MAXTOK is 128, so the guard must trip and return false without writing
  // out of bounds.
  char expr[512];
  size_t pos = 0;
  for (int i = 0; i < 130; ++i) {
    if (i > 0) expr[pos++] = ' ';
    expr[pos++] = '1';
  }
  expr[pos] = '\0';

  double v = 0;
  TEST_ASSERT_FALSE(evaluateArithmetic(expr, v));
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
  RUN_TEST(test_unary_minus_standalone);
  RUN_TEST(test_unary_minus_after_operator);
  RUN_TEST(test_double_unary);
  RUN_TEST(test_less_than);
  RUN_TEST(test_greater_than_false);
  RUN_TEST(test_less_equal);
  RUN_TEST(test_greater_equal);
  RUN_TEST(test_equality_true);
  RUN_TEST(test_inequality);
  RUN_TEST(test_comparison_lower_precedence_than_arithmetic);
  RUN_TEST(test_equality_lowest_precedence);
  RUN_TEST(test_comparison_over_unbalanced_parens);
  RUN_TEST(test_operand_overflow_fails);
  return UNITY_END();
}
