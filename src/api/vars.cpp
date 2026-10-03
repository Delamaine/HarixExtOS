#include "vars.h"

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <cstring>
#include <cstdio>

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

void listValues(void (*printer)(const char *line)) {
  if (!printer) return;
  for (size_t i = 0; i < kMaxVars; ++i) {
    if (slots[i].used) {
      char buf[128];
      if (slots[i].type == ValueType::kString) {
        snprintf(buf, sizeof(buf), "%s=\"%s\"", slots[i].name, slots[i].valueStr);
      } else {
        snprintf(buf, sizeof(buf), "%s=%.10g", slots[i].name, slots[i].value);
      }
      printer(buf);
    }
  }
}

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

}  // namespace vars
}  // namespace api
}  // namespace harixos
