#include <unity.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "api/expr.h"
#include "api/vars.h"

using harixos::api::expr::assign;
using harixos::api::expr::evaluateArithmetic;
using harixos::api::expr::evaluate;
using harixos::api::expr::expand;
using harixos::api::expr::parseSet;
using harixos::api::expr::setResolver;
using harixos::api::vars::clear;
using harixos::api::vars::set;

// Last token handed to the resolver, so tests can assert on the exact span
// expand() consumed (e.g. "readpin 2" rather than "readpin").
static char s_lastToken[64];

static bool fakeResolve(const char *token, size_t tokenLen, double &out) {
  size_t n = tokenLen < sizeof(s_lastToken) - 1 ? tokenLen : sizeof(s_lastToken) - 1;
  memcpy(s_lastToken, token, n);
  s_lastToken[n] = '\0';

  if (tokenLen == 4 && memcmp(token, "heap", 4) == 0) { out = 41984; return true; }
  if (tokenLen == 9 && memcmp(token, "readpin 2", 9) == 0) { out = 1; return true; }
  return false;
}

void setUp(void) {
  clear();
  s_lastToken[0] = '\0';
  setResolver(fakeResolve);
}
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

static void test_expand_substitutes_variable(void) {
  char out[128];
  TEST_ASSERT_TRUE(set("x", 5));
  TEST_ASSERT_TRUE(expand("$x + 1", out, sizeof(out), fakeResolve));
  TEST_ASSERT_EQUAL_STRING("5 + 1", out);
}

static void test_expand_undefined_variable_fails(void) {
  char out[128];
  TEST_ASSERT_FALSE(expand("$nope", out, sizeof(out), fakeResolve));
}

static void test_expand_value_token_heap(void) {
  char out[128];
  TEST_ASSERT_TRUE(expand("heap", out, sizeof(out), fakeResolve));
  TEST_ASSERT_EQUAL_STRING("41984", out);
}

static void test_expand_value_token_with_argument(void) {
  char out[128];
  TEST_ASSERT_TRUE(expand("readpin 2 + 1", out, sizeof(out), fakeResolve));
  TEST_ASSERT_EQUAL_STRING("1 + 1", out);
  TEST_ASSERT_EQUAL_STRING("readpin 2", s_lastToken);
}

static void test_expand_unknown_bare_word_fails(void) {
  char out[128];
  TEST_ASSERT_FALSE(expand("banana", out, sizeof(out), fakeResolve));
}

static void test_expand_rejects_over_length_result(void) {
  // 20 copies of a 15-digit value with spaces between = 319 chars, past
  // kMaxExpandedLength (256), even though out[] itself is large enough.
  char out[512];
  TEST_ASSERT_TRUE(set("x", 123456789012345.0));

  char in[128];
  size_t pos = 0;
  for (int i = 0; i < 20; ++i) {
    if (i > 0) in[pos++] = ' ';
    in[pos++] = '$';
    in[pos++] = 'x';
  }
  in[pos] = '\0';

  TEST_ASSERT_FALSE(expand(in, out, sizeof(out), fakeResolve));
}

static void test_expand_leaves_pure_arithmetic_untouched(void) {
  char out[128];
  TEST_ASSERT_TRUE(expand("1 + 2", out, sizeof(out), fakeResolve));
  TEST_ASSERT_EQUAL_STRING("1 + 2", out);
}

static void test_evaluate_combines_expand_and_arithmetic(void) {
  double v = 0;
  TEST_ASSERT_TRUE(set("x", 5));
  TEST_ASSERT_TRUE(evaluate("$x * 2", v));
  TEST_ASSERT_EQUAL_DOUBLE(10, v);
}

static void test_evaluate_fails_when_resolver_absent(void) {
  double v = 0;
  setResolver(nullptr);
  TEST_ASSERT_FALSE(evaluate("heap", v));
}

static void test_repeated_variable_in_one_expression(void) {
  double v = 0;
  TEST_ASSERT_TRUE(set("x", 3));
  TEST_ASSERT_TRUE(evaluate("$x + $x", v));
  TEST_ASSERT_EQUAL_DOUBLE(6, v);
}

static void test_parse_set_spaced(void) {
  char name[32];
  const char *expression = nullptr;
  TEST_ASSERT_TRUE(parseSet(" x = 1+2", name, sizeof(name), &expression));
  TEST_ASSERT_EQUAL_STRING("x", name);
  TEST_ASSERT_EQUAL_STRING("1+2", expression);
}

static void test_parse_set_unspaced(void) {
  char name[32];
  const char *expression = nullptr;
  TEST_ASSERT_TRUE(parseSet("x=1+2", name, sizeof(name), &expression));
  TEST_ASSERT_EQUAL_STRING("x", name);
  TEST_ASSERT_EQUAL_STRING("1+2", expression);
}

static void test_parse_set_missing_equals(void) {
  char name[32];
  const char *expression = nullptr;
  TEST_ASSERT_FALSE(parseSet(" x 1+2", name, sizeof(name), &expression));
}

static void test_parse_set_missing_expression(void) {
  char name[32];
  const char *expression = nullptr;
  TEST_ASSERT_FALSE(parseSet(" x = ", name, sizeof(name), &expression));
}

static void test_parse_set_invalid_name(void) {
  char name[32];
  const char *expression = nullptr;
  TEST_ASSERT_FALSE(parseSet(" 1x = 1", name, sizeof(name), &expression));
}

static void test_parse_set_empty_name(void) {
  char name[32];
  const char *expression = nullptr;
  TEST_ASSERT_FALSE(parseSet(" = 1", name, sizeof(name), &expression));
}

static void test_parse_set_expression_may_contain_spaces(void) {
  char name[32];
  const char *expression = nullptr;
  TEST_ASSERT_TRUE(parseSet(" x = readpin 2 + 1", name, sizeof(name), &expression));
  TEST_ASSERT_EQUAL_STRING("x", name);
  TEST_ASSERT_EQUAL_STRING("readpin 2 + 1", expression);
}

static void test_assign_success_stores_and_reports_value(void) {
  double v = 0;
  TEST_ASSERT_NULL(assign("x = 1 + 2", &v));
  TEST_ASSERT_EQUAL_DOUBLE(3, v);
  double v2 = 0;
  TEST_ASSERT_TRUE(harixos::api::vars::get("x", v2));
  TEST_ASSERT_EQUAL_DOUBLE(3, v2);
}

static void test_assign_leaves_variable_untouched_on_error(void) {
  TEST_ASSERT_TRUE(set("x", 5));
  double v = 0;
  TEST_ASSERT_NOT_NULL(assign("x = $nope", &v));
  double v2 = 0;
  TEST_ASSERT_TRUE(harixos::api::vars::get("x", v2));
  TEST_ASSERT_EQUAL_DOUBLE(5, v2);
}

static void test_assign_rejects_invalid_name_without_storing(void) {
  const size_t before = harixos::api::vars::count();
  double v = 0;
  TEST_ASSERT_NOT_NULL(assign("1bad = 1", &v));
  TEST_ASSERT_EQUAL_INT((int)before, (int)harixos::api::vars::count());
}

static void test_assign_rejects_missing_equals(void) {
  const size_t before = harixos::api::vars::count();
  double v = 0;
  TEST_ASSERT_NOT_NULL(assign(" x 1+2", &v));
  TEST_ASSERT_EQUAL_INT((int)before, (int)harixos::api::vars::count());
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
  RUN_TEST(test_expand_substitutes_variable);
  RUN_TEST(test_expand_undefined_variable_fails);
  RUN_TEST(test_expand_value_token_heap);
  RUN_TEST(test_expand_value_token_with_argument);
  RUN_TEST(test_expand_unknown_bare_word_fails);
  RUN_TEST(test_expand_rejects_over_length_result);
  RUN_TEST(test_expand_leaves_pure_arithmetic_untouched);
  RUN_TEST(test_evaluate_combines_expand_and_arithmetic);
  RUN_TEST(test_evaluate_fails_when_resolver_absent);
  RUN_TEST(test_repeated_variable_in_one_expression);
  RUN_TEST(test_parse_set_spaced);
  RUN_TEST(test_parse_set_unspaced);
  RUN_TEST(test_parse_set_missing_equals);
  RUN_TEST(test_parse_set_missing_expression);
  RUN_TEST(test_parse_set_invalid_name);
  RUN_TEST(test_parse_set_empty_name);
  RUN_TEST(test_parse_set_expression_may_contain_spaces);
  RUN_TEST(test_assign_success_stores_and_reports_value);
  RUN_TEST(test_assign_leaves_variable_untouched_on_error);
  RUN_TEST(test_assign_rejects_invalid_name_without_storing);
  RUN_TEST(test_assign_rejects_missing_equals);
  return UNITY_END();
}
