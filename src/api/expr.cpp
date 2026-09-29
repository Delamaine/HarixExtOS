#include "expr.h"

#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace harixos {
namespace api {
namespace expr {
namespace {

bool isOp(char c) {
  return c == '+' || c == '-' || c == '*' || c == '/';
}

int prec(char op) {
  if (op == '+' || op == '-') return 1;
  if (op == '*' || op == '/') return 2;
  return 0;
}

double applyOp(double a, double b, char op) {
  switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': return b == 0 ? NAN : a / b;
  }
  return NAN;
}

constexpr size_t kMaxNumberChars = 31;

}  // namespace

bool evaluateArithmetic(const char *expression, double &out) {
  if (expression == nullptr) return false;

  const size_t MAXTOK = 128;
  double valStack[MAXTOK];
  char opStack[MAXTOK];
  int vTop = -1;
  int oTop = -1;

  size_t i = 0;
  const size_t n = strlen(expression);
  while (i < n) {
    char c = expression[i];
    if (isspace((unsigned char)c)) { ++i; continue; }
    if (c == '(') {
      if (oTop + 1 >= (int)MAXTOK) return false;
      opStack[++oTop] = c; ++i; continue;
    }
    if (c == ')') {
      while (oTop >= 0 && opStack[oTop] != '(') {
        if (vTop < 1) return false;
        double b = valStack[vTop--];
        double a = valStack[vTop--];
        char op = opStack[oTop--];
        valStack[++vTop] = applyOp(a, b, op);
      }
      if (oTop >= 0 && opStack[oTop] == '(') --oTop;
      ++i; continue;
    }
    if (isOp(c)) {
      while (oTop >= 0 && isOp(opStack[oTop]) && prec(opStack[oTop]) >= prec(c)) {
        if (vTop < 1) return false;
        double b = valStack[vTop--];
        double a = valStack[vTop--];
        char op = opStack[oTop--];
        valStack[++vTop] = applyOp(a, b, op);
      }
      if (oTop + 1 >= (int)MAXTOK) return false;
      opStack[++oTop] = c;
      ++i; continue;
    }

    // number
    if (isdigit((unsigned char)c) || c == '.') {
      char numBuf[kMaxNumberChars + 1];
      size_t len = 0;
      while (i < n && (isdigit((unsigned char)expression[i]) || expression[i] == '.')) {
        if (len >= kMaxNumberChars) return false;  // literal too long: fail, never overflow
        numBuf[len++] = expression[i++];
      }
      numBuf[len] = '\0';
      double v = atof(numBuf);
      if (vTop + 1 >= (int)MAXTOK) return false;
      valStack[++vTop] = v;
      continue;
    }
    // unknown char
    return false;
  }

  while (oTop >= 0) {
    if (opStack[oTop] == '(' || opStack[oTop] == ')') return false;
    if (vTop < 1) return false;
    double b = valStack[vTop--];
    double a = valStack[vTop--];
    char op = opStack[oTop--];
    valStack[++vTop] = applyOp(a, b, op);
  }

  if (vTop != 0) return false;
  out = valStack[vTop];
  return true;
}

}  // namespace expr
}  // namespace api
}  // namespace harixos
