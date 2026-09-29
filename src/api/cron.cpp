#include "cron.h"

#include <ctype.h>
#include <string.h>

namespace harixos {
namespace api {
namespace cron {
namespace {

const char kExpectedSix[] = "expected 6 fields";
const char kUnsupported[] = "unsupported syntax";
const char kOutOfRange[] = "value out of range";
const char kBadStep[] = "step must be >= 1";
const char kReversed[] = "reversed range";

// Inclusive bounds per field. Index is the 0-based field number.
struct FieldBounds {
  int lo;
  int hi;
};

const FieldBounds kBounds[6] = {
    {0, 59},  // sec
    {0, 59},  // minute
    {0, 23},  // hour
    {1, 31},  // dom
    {1, 12},  // month
    {0, 6},   // dow
};

void setBadField(int *badField, int value) {
  if (badField != nullptr) *badField = value;
}

bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
         c == '\v';
}

bool parseNum(const char *&p, int *out) {
  if (*p < '0' || *p > '9') return false;
  long v = 0;
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + (*p - '0');
    if (v > 100000) return false;
    ++p;
  }
  *out = (int)v;
  return true;
}

// Expands one comma-separated range into `mask`.
// Accepts `*`, `n`, `a-b`, each optionally followed by `/step`.
const char *parseRange(const char *&p, int lo, int hi, uint64_t *maskOut) {
  int a = 0;
  int b = 0;
  int step = 1;
  bool star = false;

  if (*p == '*') {
    star = true;
    a = lo;
    b = hi;
    ++p;
  } else {
    if (!parseNum(p, &a)) return kUnsupported;
    b = a;
    if (*p == '-') {
      ++p;
      if (!parseNum(p, &b)) return kUnsupported;
    }
  }

  if (*p == '/') {
    ++p;
    if (!parseNum(p, &step)) return kUnsupported;
  }

  // Validate before emitting any bit so a rejected range never half-fills
  // the accumulator the caller is about to commit.
  if (step < 1) return kBadStep;
  if (!star) {
    if (a > b) return kReversed;
    if (a < lo || b > hi) return kOutOfRange;
  }

  uint64_t mask = 0;
  for (int v = a; v <= b; v += step) mask |= (1ULL << v);
  *maskOut = mask;
  return nullptr;
}

// Expands a whole field token (a comma-separated list of ranges).
const char *parseField(const char *token, int lo, int hi, uint64_t *maskOut) {
  const char *p = token;
  uint64_t mask = 0;

  while (true) {
    uint64_t part = 0;
    const char *err = parseRange(p, lo, hi, &part);
    if (err != nullptr) return err;
    mask |= part;

    if (*p == ',') {
      ++p;
      if (*p == '\0') return kUnsupported;  // trailing comma
      continue;
    }
    break;
  }

  if (*p != '\0') return kUnsupported;
  *maskOut = mask;
  return nullptr;
}

}  // namespace

const char *parse(const char *expression, Spec &out, int *badField) {
  setBadField(badField, -1);
  if (expression == nullptr) return kExpectedSix;

  char tokens[6][64];
  int count = 0;

  const char *p = expression;
  while (true) {
    while (isSpace(*p)) ++p;
    if (*p == '\0') break;

    if (count == 6) {
      setBadField(badField, -1);
      return kExpectedSix;
    }

    size_t n = 0;
    while (*p != '\0' && !isSpace(*p)) {
      if (n >= sizeof(tokens[0]) - 1) {
        setBadField(badField, count);
        return kUnsupported;
      }
      tokens[count][n++] = *p++;
    }
    tokens[count][n] = '\0';
    ++count;
  }

  if (count != 6) {
    setBadField(badField, -1);
    return kExpectedSix;
  }

  uint64_t masks[6];
  for (int i = 0; i < 6; ++i) {
    const char *err = parseField(tokens[i], kBounds[i].lo, kBounds[i].hi, &masks[i]);
    if (err != nullptr) {
      setBadField(badField, i);
      return err;
    }
  }

  // Everything parsed: only now is it safe to touch `out`.
  out.sec = masks[0];
  out.minute = masks[1];
  out.hour = (uint32_t)masks[2];
  out.dom = (uint32_t)masks[3];
  out.month = (uint16_t)masks[4];
  out.dow = (uint8_t)masks[5];
  out.domRestricted = (strcmp(tokens[3], "*") != 0);
  out.dowRestricted = (strcmp(tokens[5], "*") != 0);
  setBadField(badField, -1);
  return nullptr;
}

bool matches(const Spec &spec, const struct tm &timeinfo) {
  if (((spec.sec >> timeinfo.tm_sec) & 1ULL) == 0) return false;
  if (((spec.minute >> timeinfo.tm_min) & 1ULL) == 0) return false;
  if (((spec.hour >> timeinfo.tm_hour) & 1U) == 0) return false;
  if (((spec.month >> (timeinfo.tm_mon + 1)) & 1U) == 0) return false;

  // dom and dow are AND'ed, unlike standard cron's OR. A bare '*' restricts
  // nothing and therefore counts as a match, so a partially restricted pair
  // reduces to whichever side is actually written out.
  const bool domOk =
      !spec.domRestricted || ((spec.dom >> timeinfo.tm_mday) & 1U) != 0;
  const bool dowOk =
      !spec.dowRestricted || ((spec.dow >> timeinfo.tm_wday) & 1U) != 0;
  return domOk && dowOk;
}

}  // namespace cron
}  // namespace api
}  // namespace harixos
