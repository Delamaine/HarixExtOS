#include "expr.h"

#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "vars.h"

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

using ResolverFn = bool (*)(const char *token, size_t tokenLen, double &out);
ResolverFn s_resolver = nullptr;

bool isIdentStart(char c) {
  return isalpha((unsigned char)c) != 0 || c == '_';
}

bool isIdentChar(char c) {
  return isalnum((unsigned char)c) != 0 || c == '_';
}

// Shortest form that parses back to the same double: %g for everyday values,
// %.17g only when %g would lose precision. Keeps "$x + 1" readable without
// silently rounding a stored value.
void formatDouble(double v, char *buf, size_t cap) {
  snprintf(buf, cap, "%g", v);
  if (strtod(buf, nullptr) != v) snprintf(buf, cap, "%.17g", v);
}

// Single implementation behind both the bool parseSet() and assign(), so
// each error message exists exactly once. Returns nullptr on success.
const char *parseSetImpl(const char *line, char *nameOut, size_t nameCap,
                         const char **expressionOut) {
  static const char kUsage[] = "Usage: set <name> = <expression>";
  if (line == nullptr || nameOut == nullptr || nameCap == 0 ||
      expressionOut == nullptr) {
    return kUsage;
  }

  const char *p = line;
  while (*p == ' ' || *p == '\t') ++p;

  const char *eq = strchr(p, '=');
  if (eq == nullptr) return kUsage;

  const char *nameStart = p;
  const char *nameEnd = eq;
  while (nameEnd > nameStart && (nameEnd[-1] == ' ' || nameEnd[-1] == '\t')) --nameEnd;
  const size_t nameLen = (size_t)(nameEnd - nameStart);
  if (nameLen == 0) return kUsage;
  if (nameLen + 1 > nameCap) return "Invalid variable name.";

  memcpy(nameOut, nameStart, nameLen);
  nameOut[nameLen] = '\0';
  if (!vars::isValidName(nameOut)) return "Invalid variable name.";

  const char *expression = eq + 1;
  while (*expression == ' ' || *expression == '\t') ++expression;
  if (*expression == '\0') return kUsage;

  *expressionOut = expression;
  return nullptr;
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

void setResolver(bool (*resolve)(const char *token, size_t tokenLen, double &out)) {
  s_resolver = resolve;
}

bool expand(const char *expression, char *out, size_t outCapacity,
            bool (*resolve)(const char *token, size_t tokenLen, double &out)) {
  if (expression == nullptr || out == nullptr || outCapacity == 0) return false;

  // Usable space is the smaller of the caller's buffer and the module's
  // hard cap; one byte of either is reserved for the terminator.
  size_t limit = outCapacity;
  if (limit > kMaxExpandedLength + 1) limit = kMaxExpandedLength + 1;

  size_t o = 0;
  const size_t n = strlen(expression);
  size_t i = 0;

  char num[48];
  while (i < n) {
    const char c = expression[i];

    if (c == '$') {                                  // variable reference
      ++i;
      if (i >= n || !isIdentStart(expression[i])) return false;
      const size_t start = i;
      while (i < n && isIdentChar(expression[i])) ++i;
      const size_t len = i - start;
      if (len > vars::kMaxNameLength) return false;

      char name[vars::kMaxNameLength + 1];
      memcpy(name, expression + start, len);
      name[len] = '\0';

      double v = 0;
      if (!vars::get(name, v)) return false;         // undefined is an error
      formatDouble(v, num, sizeof(num));
      const size_t numLen = strlen(num);
      if (o + numLen + 1 > limit) return false;
      memcpy(out + o, num, numLen);
      o += numLen;
      continue;
    }

    if (isIdentStart(c)) {                           // bare value token
      const size_t start = i;
      while (i < n && isIdentChar(expression[i])) ++i;
      // Optional numeric argument: the whitespace and digits that follow,
      // so the resolver sees the whole span "readpin 2".
      size_t j = i;
      while (j < n && (expression[j] == ' ' || expression[j] == '\t')) ++j;
      const size_t argStart = j;
      while (j < n && isdigit((unsigned char)expression[j])) ++j;
      if (j > argStart) i = j;

      if (resolve == nullptr) return false;
      double v = 0;
      if (!resolve(expression + start, i - start, v)) return false;
      formatDouble(v, num, sizeof(num));
      const size_t numLen = strlen(num);
      if (o + numLen + 1 > limit) return false;
      memcpy(out + o, num, numLen);
      o += numLen;
      continue;
    }

    if (strchr("+-*/()<>!=.", c) != nullptr || isspace((unsigned char)c) ||
        isdigit((unsigned char)c)) {                 // pass through
      if (o + 2 > limit) return false;
      out[o++] = c;
      ++i;
      continue;
    }

    return false;                                    // anything else fails
  }

  out[o] = '\0';
  return true;
}

bool evaluate(const char *expression, double &out) {
  char buf[kMaxExpandedLength + 1];
  if (!expand(expression, buf, sizeof(buf), s_resolver)) return false;
  return evaluateArithmetic(buf, out);
}

bool parseSet(const char *line, char *nameOut, size_t nameCap,
              const char **expressionOut) {
  return parseSetImpl(line, nameOut, nameCap, expressionOut) == nullptr;
}

const char *assign(const char *argsAfterSet, double *valueOut) {
  char name[vars::kMaxNameLength + 1];
  const char *expression = nullptr;

  // 1. Parse first: on failure nothing has been touched.
  const char *msg = parseSetImpl(argsAfterSet, name, sizeof(name), &expression);
  if (msg != nullptr) return msg;

  // 2. Evaluate next: an invalid expression must not clobber the old value.
  double v = 0;
  if (!evaluate(expression, v)) return "Invalid expression.";

  // 3. Store only after 1 and 2 both succeeded.
  if (!vars::set(name, v)) return "Too many variables.";

  if (valueOut != nullptr) *valueOut = v;
  return nullptr;
}

}  // namespace expr
}  // namespace api
}  // namespace harixos
