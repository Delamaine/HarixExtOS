#pragma once

#include <stddef.h>

namespace harixos {
namespace api {
namespace vars {

constexpr size_t kMaxVars = 16;
constexpr size_t kMaxNameLength = 16;

bool isValidName(const char *name);
bool set(const char *name, double value);   // false if invalid name or full
bool get(const char *name, double &out);    // false if undefined; out untouched
void clear();
size_t count();
const char *nameAt(size_t index);           // "" for an unused slot
double valueAt(size_t index);

}  // namespace vars
}  // namespace api
}  // namespace harixos
