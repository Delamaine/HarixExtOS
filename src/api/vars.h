#pragma once

#include <stddef.h>

namespace harixos {
namespace api {
namespace vars {

constexpr size_t kMaxVars = 16;
constexpr size_t kMaxNameLength = 16;
constexpr size_t kMaxStringValue = 64;
constexpr size_t kPersistPathLength = 64;
inline const char* kPersistPath = "/vars.dat";

enum class ValueType {
  kNumber = 0,
  kString = 1
};

bool isValidName(const char *name);
bool set(const char *name, double value);   // false if invalid name or full
bool setString(const char *name, const char *str);  // store a string value
bool get(const char *name, double &out);    // false if undefined; out untouched
bool getString(const char *name, char *out, size_t outSize);  // false if undefined
void clear();
size_t count();
const char *nameAt(size_t index);           // "" for an unused slot
double valueAt(size_t index);
ValueType typeAt(size_t index);             // numeric or string type
void listValues(void (*printer)(const char *line));
bool save(const char *path);                // persist to LittleFS
bool load(const char *path);                // restore from LittleFS
void clearAll();                            // clear all vars and reset types
bool delByName(const char *name);            // delete a variable by name

}  // namespace vars
}  // namespace api
}  // namespace harixos
