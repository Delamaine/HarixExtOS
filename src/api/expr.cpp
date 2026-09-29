#include "expr.h"

#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace harixos {
namespace api {
namespace expr {
namespace {

constexpr size_t kMaxToks = 128;
constexpr size_t kMaxNumberChars = 31;

// Moved from stack to static storage: 1,152 B of locals would otherwise
// re-materialise under deep script/run nesting. Guarded because two
// concurrent evaluations would corrupt each other.
double valStack[kMaxToks];
char opStack[kMaxToks];
bool s_inUse = false;

struct WorkspaceGuard {
  bool held;
  WorkspaceGuard() : held(false) {
    if (!s_inUse) {
      s_inUse = true;
      held = true;
    }
  }
  ~WorkspaceGuard() {
    if (held) s_inUse = false;
  }
};

// Operator tokens. Two-character comparisons are given distinct single-char
// representations so they fit the op stack.
//   '*' '/' = 4 | '+' '-' = 3 | '<' '>' 'L' 'G' = 2 | 'E' 'N' = 1
// where L is <=, G is >=, E is ==, N is !=.
bool isOperatorChar(char c) {
  return c == '+' || c == '-' || c == '*' || c == '/' ||
         c == '<' || c == '>' || c == 'L' || c == 'G' ||
         c == 'E' || c == 'N';
}

int prec(char op) {
  if (op == '*' || op == '/') return 4;
  if (op == '+' || op == '-') return 3;
  if (op == '<' || op == '>' || op == 'L' || op == 'G') return 2;
  if (op == 'E' || op == 'N') return 1;
  return 0;
}

double applyOp(double a, double b, char op) {
  switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': return b == 0 ? NAN : a / b;
    case '<': return a < b;
    case '>': return a > b;
    case 'L': return a <= b;
    case 'G': return a >= b;
    case 'E': return a == b;
    case 'N': return a != b;
  }
  return NAN;
}

// Longest-match scan. Returns the operator char, or '\0' if the input at
// `i` is not an operator; `len` receives how many characters it consumed.
char matchOperator(const char *s, size_t i, size_t n, size_t &len) {
  const char c = s[i];
  const char next = (i + 1 < n) ? s[i + 1] : '\0';

  if (c == '<' && next == '=') { len = 2; return 'L'; }
  if (c == '>' && next == '=') { len = 2; return 'G'; }
  if (c == '=' && next == '=') { len = 2; return 'E'; }
  if (c == '!' && next == '=') { len = 2; return 'N'; }
  if (c == '<' || c == '>' || c == '+' || c == '-' || c == '*' || c == '/') {
    len = 1;
    return c;
  }
  len = 0;
  return '\0';
}

}  // namespace

bool evaluateArithmetic(const char *expression, double &out) {
  if (expression == nullptr) return false;

  WorkspaceGuard guard;
  if (!guard.held) return false;  // re-entrant call; never fires in practice

  double *val = valStack;
  char *ops = opStack;
  int vTop = -1;
  int oTop = -1;
  // True where a value is expected: at the start, after '(', and after an
  // operator. That is exactly where a unary minus is legal.
  bool expectOperand = true;

  size_t i = 0;
  const size_t n = strlen(expression);
  while (i < n) {
    const char c = expression[i];
    if (isspace((unsigned char)c)) { ++i; continue; }

    if (c == '(') {
      if (oTop + 1 >= (int)kMaxToks) return false;
      ops[++oTop] = c;
      ++i;
      expectOperand = true;
      continue;
    }

    if (c == ')') {
      while (oTop >= 0 && ops[oTop] != '(') {
        if (vTop < 1) return false;
        double b = val[vTop--];
        double a = val[vTop--];
        char op = ops[oTop--];
        val[++vTop] = applyOp(a, b, op);
      }
      if (oTop >= 0 && ops[oTop] == '(') --oTop;
      ++i;
      expectOperand = false;
      continue;
    }

    size_t opLen = 0;
    const char op = matchOperator(expression, i, n, opLen);
    if (op != '\0') {
      // Unary minus: operand position. Emit `0 - x` by pushing 0 first and
      // pushing '-' WITHOUT the normal pop loop — popping here would bind the
      // pending operator to the 0 we just pushed (2 * -3 would become 0).
      if (op == '-' && expectOperand) {
        if (vTop + 1 >= (int)kMaxToks) return false;
        val[++vTop] = 0.0;
        if (oTop + 1 >= (int)kMaxToks) return false;
        ops[++oTop] = '-';
        i += opLen;
        expectOperand = true;
        continue;
      }

      while (oTop >= 0 && isOperatorChar(ops[oTop]) &&
             prec(ops[oTop]) >= prec(op)) {
        if (vTop < 1) return false;
        double b = val[vTop--];
        double a = val[vTop--];
        char popped = ops[oTop--];
        val[++vTop] = applyOp(a, b, popped);
      }
      if (oTop + 1 >= (int)kMaxToks) return false;
      ops[++oTop] = op;
      i += opLen;
      expectOperand = true;
      continue;
    }

    // number
    if (isdigit((unsigned char)c) || c == '.') {
      char numBuf[kMaxNumberChars + 1];
      size_t len = 0;
      while (i < n && (isdigit((unsigned char)expression[i]) || expression[i] == '.')) {
        if (len >= kMaxNumberChars) return false;  // too long: fail, never overflow
        numBuf[len++] = expression[i++];
      }
      numBuf[len] = '\0';
      if (vTop + 1 >= (int)kMaxToks) return false;
      val[++vTop] = atof(numBuf);
      expectOperand = false;
      continue;
    }

    return false;  // unknown char
  }

  while (oTop >= 0) {
    if (ops[oTop] == '(' || ops[oTop] == ')') return false;
    if (vTop < 1) return false;
    double b = val[vTop--];
    double a = val[vTop--];
    char op = ops[oTop--];
    val[++vTop] = applyOp(a, b, op);
  }

  if (vTop != 0) return false;
  out = val[vTop];
  return true;
}

}  // namespace expr
}  // namespace api
}  // namespace harixos
