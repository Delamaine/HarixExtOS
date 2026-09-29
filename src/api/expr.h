#pragma once

#include <stddef.h>

namespace harixos {
namespace api {
namespace expr {

constexpr size_t kMaxExpandedLength = 256;

// Pure arithmetic evaluation. No variable expansion, no Arduino headers.
bool evaluateArithmetic(const char *expression, double &out);

// Substitution of $name / value tokens. Implemented in Task 5.
bool expand(const char *expression, char *out, size_t outCapacity,
            bool (*resolve)(const char *token, size_t tokenLen, double &out));
void setResolver(bool (*resolve)(const char *token, size_t tokenLen, double &out));

// expand() followed by evaluateArithmetic(). Implemented in Task 5.
bool evaluate(const char *expression, double &out);

}  // namespace expr
}  // namespace api
}  // namespace harixos
