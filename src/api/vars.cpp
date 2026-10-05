#include "vars.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#endif
#include <cctype>
#include <cstring>
#include <cstdio>

#include "expr.h"

namespace harixos {
namespace api {
namespace vars {
namespace {

struct Slot {
  char name[kMaxNameLength + 1];
  double value;
  char valueStr[kMaxStringValue];
  ValueType type;
  bool used;
};

Slot slots[kMaxVars];

int findIndex(const char *name) {
  if (name == nullptr) return -1;
  for (size_t i = 0; i < kMaxVars; ++i) {
    if (slots[i].used && strncmp(slots[i].name, name, kMaxNameLength + 1) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int firstFreeIndex() {
  for (size_t i = 0; i < kMaxVars; ++i) {
    if (!slots[i].used) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace

bool isValidName(const char *name) {
  if (name == nullptr || name[0] == '\0') return false;

  const size_t len = strlen(name);
  if (len > kMaxNameLength) return false;

  const char c0 = name[0];
  const bool firstOk = (c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || c0 == '_';
  if (!firstOk) return false;

  for (size_t i = 1; i < len; ++i) {
    const char c = name[i];
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '_';
    if (!ok) return false;
  }
  return true;
}

bool set(const char *name, double value) {
  if (!isValidName(name)) return false;

  int idx = findIndex(name);
  if (idx < 0) {
    idx = firstFreeIndex();
    if (idx < 0) return false;
    strncpy(slots[idx].name, name, kMaxNameLength);
    slots[idx].name[kMaxNameLength] = '\0';
    slots[idx].used = true;
  }
  slots[idx].value = value;
  slots[idx].type = ValueType::kNumber;
  snprintf(slots[idx].valueStr, kMaxStringValue, "%.10g", value);
  return true;
}

bool setString(const char *name, const char *str) {
  if (!isValidName(name) || str == nullptr) return false;

  int idx = findIndex(name);
  if (idx < 0) {
    idx = firstFreeIndex();
    if (idx < 0) return false;
    strncpy(slots[idx].name, name, kMaxNameLength);
    slots[idx].name[kMaxNameLength] = '\0';
    slots[idx].used = true;
  }
  strncpy(slots[idx].valueStr, str, kMaxStringValue - 1);
  slots[idx].valueStr[kMaxStringValue - 1] = '\0';
  slots[idx].type = ValueType::kString;
  return true;
}

bool get(const char *name, double &out) {
  const int idx = findIndex(name);
  if (idx < 0) return false;
  out = slots[idx].value;
  return true;
}

bool getString(const char *name, char *out, size_t outSize) {
  if (out == nullptr || outSize == 0) return false;
  const int idx = findIndex(name);
  if (idx < 0) return false;
  if (slots[idx].type != ValueType::kString) return false;
  strncpy(out, slots[idx].valueStr, outSize - 1);
  out[outSize - 1] = '\0';
  return true;
}

void clear() {
  for (size_t i = 0; i < kMaxVars; ++i) {
    slots[i].used = false;
    slots[i].name[0] = '\0';
    slots[i].value = 0;
    slots[i].valueStr[0] = '\0';
    slots[i].type = ValueType::kNumber;
  }
}

size_t count() {
  size_t n = 0;
  for (size_t i = 0; i < kMaxVars; ++i) {
    if (slots[i].used) ++n;
  }
  return n;
}

const char *nameAt(size_t index) {
  if (index >= kMaxVars || !slots[index].used) return "";
  return slots[index].name;
}

double valueAt(size_t index) {
  if (index >= kMaxVars) return 0;
  return slots[index].value;
}

ValueType typeAt(size_t index) {
  if (index >= kMaxVars) return ValueType::kNumber;
  return slots[index].type;
}

void listValues(void (*printer)(const char *line, void *ctx), void *ctx) {
  if (!printer) return;
  for (size_t i = 0; i < kMaxVars; ++i) {
    if (slots[i].used) {
      char buf[128];
      if (slots[i].type == ValueType::kString) {
        snprintf(buf, sizeof(buf), "%s=\"%s\"", slots[i].name, slots[i].valueStr);
      } else {
        snprintf(buf, sizeof(buf), "%s=%.10g", slots[i].name, slots[i].value);
      }
      printer(buf, ctx);
    }
  }
}

#ifdef ARDUINO

bool save(const char *path) {
  if (path == nullptr || path[0] == '\0') return false;
  
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  
  for (size_t i = 0; i < kMaxVars; ++i) {
    if (slots[i].used) {
      if (slots[i].type == ValueType::kString) {
        f.printf("S:%s=%s\n", slots[i].name, slots[i].valueStr);
      } else {
        f.printf("N:%s=%.10g\n", slots[i].name, slots[i].value);
      }
    }
  }
  
  f.close();
  return true;
}

bool load(const char *path) {
  if (path == nullptr || path[0] == '\0') return false;
  
  if (!LittleFS.exists(path)) return false;
  
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  
  clear();
  
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    
    if (line.startsWith("N:")) {
      int eq = line.indexOf('=', 2);
      if (eq > 2) {
        String name = line.substring(2, eq);
        String val = line.substring(eq + 1);
        set(name.c_str(), val.toFloat());
      }
    } else if (line.startsWith("S:")) {
      int eq = line.indexOf('=', 2);
      if (eq > 2) {
        String name = line.substring(2, eq);
        int endQuote = line.indexOf('"', eq + 1);
        String val;
        if (endQuote > eq) {
          val = line.substring(eq + 2, endQuote - eq - 2);
        } else {
          val = line.substring(eq + 1);
        }
        setString(name.c_str(), val.c_str());
      }
    }
  }
  
  f.close();
  return true;
}

#else  // !ARDUINO — host builds have no filesystem

bool save(const char *path) { (void)path; return false; }
bool load(const char *path) { (void)path; return false; }

#endif  // ARDUINO

void clearAll() {
  clear();
}

bool delByName(const char *name) {
  if (name == nullptr || name[0] == '\0') return false;
  int idx = findIndex(name);
  if (idx < 0) return false;
  slots[idx].used = false;
  slots[idx].name[0] = '\0';
  slots[idx].value = 0;
  slots[idx].valueStr[0] = '\0';
  slots[idx].type = ValueType::kNumber;
  return true;
}

namespace {
const char kUsage[] =
    "Usage: vars [list|set|get|del|save|load|clear] [args...]";
const char kUnknownAction[] =
    "Unknown vars action: list, set, get, del, save, load, clear";
char g_errorBuf[96];

bool ieq(const char *a, const char *b) {
  while (*a != '\0' && *b != '\0') {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

// Copies `in` into `out`, dropping leading and trailing whitespace.
void copyTrimmed(char *out, size_t cap, const char *in) {
  while (*in == ' ' || *in == '\t') ++in;
  size_t len = strlen(in);
  while (len > 0 && (in[len - 1] == ' ' || in[len - 1] == '\t')) --len;
  if (len >= cap) len = cap - 1;
  memcpy(out, in, len);
  out[len] = '\0';
}
}  // namespace

const char *runCommand(const char *args,
                       void (*emit)(const char *line, void *ctx), void *ctx) {
  if (emit == nullptr || args == nullptr) return kUsage;

  while (*args == ' ' || *args == '\t') ++args;
  if (*args == '\0') return kUsage;

  // Split into the action word and the (space-skipped) remainder.
  const char *sep = strchr(args, ' ');
  const char *rest = (sep != nullptr) ? sep : args + strlen(args);
  while (*rest == ' ' || *rest == '\t') ++rest;

  char action[kMaxNameLength + 1];
  size_t actionLen = (sep != nullptr) ? (size_t)(sep - args) : strlen(args);
  if (actionLen >= sizeof(action)) actionLen = sizeof(action) - 1;
  memcpy(action, args, actionLen);
  action[actionLen] = '\0';

  char text[256];

  if (ieq(action, "list")) {
    emit("Variables:", ctx);
    listValues(emit, ctx);
    snprintf(text, sizeof(text), "Count: %zu/%zu", count(), kMaxVars);
    emit(text, ctx);
    return nullptr;
  }

  if (ieq(action, "set")) {
    if (*rest == '\0') return "Usage: vars set <name>=<value>";
    copyTrimmed(text, sizeof(text), rest);
    double value = 0;
    const char *err = expr::assign(text, &value);
    if (err != nullptr) return err;
    char name[kMaxNameLength + 1];
    const char *expression = nullptr;
    if (!expr::parseSet(text, name, sizeof(name), &expression)) {
      return "Usage: vars set <name>=<value>";
    }
    snprintf(text, sizeof(text), "%s = %.10g", name, value);
    emit(text, ctx);
    return nullptr;
  }

  if (ieq(action, "get")) {
    if (*rest == '\0') return "Usage: vars get <name>";
    char name[kMaxNameLength + 1];
    copyTrimmed(name, sizeof(name), rest);
    double v = 0;
    if (get(name, v)) {
      char line[kMaxNameLength + 32];
      snprintf(line, sizeof(line), "%s = %.10g", name, v);
      emit(line, ctx);
      return nullptr;
    }
    snprintf(g_errorBuf, sizeof(g_errorBuf), "%s not found.", name);
    return g_errorBuf;
  }

  if (ieq(action, "del") || ieq(action, "delete")) {
    if (*rest == '\0') return "Usage: vars del <name>";
    copyTrimmed(text, sizeof(text), rest);
    if (delByName(text)) {
      snprintf(g_errorBuf, sizeof(g_errorBuf), "%s deleted.", text);
      emit(g_errorBuf, ctx);
      return nullptr;
    }
    snprintf(g_errorBuf, sizeof(g_errorBuf), "%s not found.", text);
    return g_errorBuf;
  }

  if (ieq(action, "save")) {
    if (save(kPersistPath)) {
      emit("Variables saved.", ctx);
      return nullptr;
    }
    return "Failed to save variables.";
  }

  if (ieq(action, "load")) {
    if (load(kPersistPath)) {
      snprintf(text, sizeof(text), "Variables loaded (%zu).", count());
      emit(text, ctx);
      return nullptr;
    }
    return "No variables to load.";
  }

  if (ieq(action, "clear")) {
    clear();
    emit("All variables cleared.", ctx);
    return nullptr;
  }

  return kUnknownAction;
}

}  // namespace vars
}  // namespace api
}  // namespace harixos
