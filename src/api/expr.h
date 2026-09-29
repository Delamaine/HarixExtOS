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

// Parse " <name> = <expression>". `line` points at the text after the
// leading `set`. False on a missing '=', an invalid name, or an empty
// expression. On success writes a NUL-terminated name and points
// `expressionOut` at the right-hand text inside `line`.
bool parseSet(const char *line, char *nameOut, size_t nameCap,
              const char **expressionOut);

// parseSet, then evaluate, then vars::set, behind one call. Returns nullptr
// on success (writing *valueOut, which may be null) or a static message on
// failure. On failure no variable is modified.
const char *assign(const char *argsAfterSet, double *valueOut);

}  // namespace expr
}  // namespace api
}  // namespace harixos
