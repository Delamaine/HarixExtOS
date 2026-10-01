# `.hx` Language Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Scripts and the shell can store values in variables and branch with `if`/`else`/`end`.

**Architecture:** Three new Arduino-free modules under `src/api/` — `expr`, `vars`, `block_stack` — compiled both into firmware and into a PlatformIO `native` host environment for unit tests. `main.cpp`'s existing evaluator is extracted into `expr` and extended; `script_engine.cpp` gains only the structural keywords.

**Tech Stack:** C++17, Arduino ESP8266 core 3.30102.0, PlatformIO 6.2.0, PlatformIO `native` platform 1.2.1 + Unity 2.6.1, MinGW-w64 GCC 16.2.0.

**Spec:** `docs/superpowers/specs/2026-09-29-hx-language-and-cron-design.md` §5, §6 — the plan argues from the spec; the spec travels with it.

## Design refinement from the spec

The spec (§5.2) sketches `bool evaluate(const String &expression, double &out)`. This plan uses **`const char *`** instead, for one reason: Arduino `String` does not exist on the host, so a `String`-based signature would force a hand-written Arduino shim to be maintained in parallel. Taking `const char *` and storing variable names in fixed `char[]` makes all three modules compile with **zero Arduino headers**, which is what makes the host tests possible at all.

Behaviour is unchanged; only signatures differ. Callers pass `line.c_str()`. This is a deliberate, load-bearing deviation — do not "fix" it back to `String`.

## Global Constraints

- **Build gate after every task:** `pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini` must be **4/4 SUCCESS**, no new warnings from `src/`.
- **Host test gate after every task that touches `expr`, `vars`, or `block_stack`:** `pio test -e native` must report 0 failures.
- If `pio` is not on PATH, use `C:\Users\delam\AppData\Roaming\Python\Python312\Scripts\pio.exe`.
- **`g++` is not on PATH in a fresh shell.** Prepend the user PATH before running `pio test`:
  `$env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")`
- `[env:native]` must **never** appear in `default_envs`, and the four hardware envs must be byte-identical to their current definitions.
- **No `lib_deps`.** Unity is fetched by PlatformIO's test runner; it does not belong in `platformio.ini`.
- LSP/clangd `'Arduino.h' file not found` errors are environmental noise, not failures.
- Pinned constants — copy verbatim, do not renumber: `kMaxVars = 16`, `kMaxNameLength = 16`, `kMaxBlockDepth = 8`, `kMaxScriptDepth = 8`, `kMaxExpandedLength = 256`.
- Name pattern: `[A-Za-z_][A-Za-z0-9_]*`, max 16 chars.
- Branch from `main`. Do not push.

## Review Focus

1. **Undefined `$name` must error, never evaluate as 0.** A silently-zero condition takes the wrong branch with no visible failure — the worst outcome this feature can produce. *Pinned in Task 5, Step 2 (`test_expand_undefined_variable_fails`).*
2. **`set` must not clobber the variable when its expression is invalid.** `set x = 5` followed by `set x = $nope` must leave `x == 5`. *Pinned in Task 6, Step 2 (`test_set_leaves_variable_untouched_on_error`).*
3. **A nested `if` inside a skipped branch must not terminate the outer block.** The classic control-flow off-by-one. *Pinned in Task 7, Step 2 (`test_nested_if_inside_skipped_branch`).*
4. **`# end` must not close a block.** Comment filtering has to precede structural keyword matching, or a commented-out `end` silently truncates a script. *Pinned in Task 8, Step 3 (serial) — `executeScript`'s comment filter is not reachable from the host.*
5. **Precedence regressions.** `calc` already ships and users depend on `1+2*3 == 7`; extending the grammar with comparisons could easily break it. *Pinned in Task 3, Step 2 (characterization tests, kept green through Task 4).*
6. **Static workspace reentrancy guard has no natural trigger.** `evaluateArithmetic` is not reachable from a resolver or from `expand`, so no unit test can nest it. This is an accepted gap: verify by inspection in Task 4, Step 4, and keep it that way — introducing a nesting path would be a bug in itself.

---

### Task 1: Native test environment

**Files:**
- Modify: `platformio.ini` (append)
- Create: `test/test_smoke/test_smoke.cpp`

**Interfaces:**
- Produces: a working `pio test -e native` gate. Every later task adds its module to `build_src_filter` and its suite under `test/`.

- [ ] **Step 1: Branch and add the env**

Create the branch `feat/hx-language` from `main`, then append to `platformio.ini`:

```ini
[env:native]
platform = native
test_framework = unity
build_flags = -std=c++17
build_src_filter = -<*>
```

`build_src_filter = -<*>` excludes all of `src/` — nothing is host-compilable yet, and `src/main.cpp` must never reach the host compiler.

- [ ] **Step 2: Write a smoke test**

Create `test/test_smoke/test_smoke.cpp`:

```cpp
#include <unity.h>
void setUp(void) {}
void tearDown(void) {}
static void test_harness_runs(void) { TEST_ASSERT_EQUAL_INT(4, 2 + 2); }
int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_harness_runs);
  return UNITY_END();
}
```

- [ ] **Step 3: Run the host tests**

Refresh PATH as described in Global Constraints, then:
```
pio test -e native
```
Expected: `1 test cases: 1 succeeded`. First run also installs `native@1.2.1` and `Unity@2.6.1` into `.pio/`.

- [ ] **Step 4: Prove the hardware envs are undisturbed**

```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS.** `default_envs` still resolves to `esp01_1m`.

- [ ] **Step 5: Commit**

```bash
git add platformio.ini test/test_smoke/test_smoke.cpp
git commit -m "test: add PlatformIO native env with a Unity smoke test

Host-only scaffold for the pure modules (expr, vars, block_stack).
build_src_filter excludes all of src/ so main.cpp never reaches g++."
```

---

### Task 2: Variable store

**Files:**
- Create: `src/api/vars.h`, `src/api/vars.cpp`
- Create: `test/test_vars/test_vars.cpp`
- Modify: `platformio.ini` — change `build_src_filter = -<*>` to `build_src_filter = -<*> +<api/vars.cpp>`

**Interfaces:**
- Produces (consumed by Tasks 5, 6):
  ```cpp
  namespace harixos { namespace api { namespace vars {
  constexpr size_t kMaxVars = 16;
  constexpr size_t kMaxNameLength = 16;
  bool isValidName(const char *name);
  bool set(const char *name, double value);          // false if invalid name or full
  bool get(const char *name, double &out);           // false if undefined; out untouched
  void clear();
  size_t count();
  const char *nameAt(size_t index);                  // "" for an unused slot
  double valueAt(size_t index);
  }}}}
  ```
- Consumes: nothing.

- [ ] **Step 1: Write the failing tests**

Create `test/test_vars/test_vars.cpp` with `setUp()` calling `vars::clear()` and `tearDown()` empty. Tests:

| Test name | Assertion |
|---|---|
| `test_set_get_roundtrip` | `set("x", 42)` true; `get("x", v)` true; `v == 42` |
| `test_get_undefined_returns_false_and_leaves_out` | `v = 7`; `get("nope", v)` false; `v == 7` |
| `test_capacity_16_accepted_17th_rejected` | 16 `set`s true, `count() == 16`, 17th false |
| `test_valid_name_accepts_leading_underscore` | `isValidName("_a1")` true |
| `test_valid_name_rejects_leading_digit` | `isValidName("1x")` false |
| `test_valid_name_rejects_dollar` | `isValidName("$x")` false |
| `test_valid_name_rejects_spaces` | `isValidName("a b")` false |
| `test_valid_name_rejects_empty` | `isValidName("")` false |
| `test_valid_name_rejects_over_length` | 17-char name false; exactly 16 true |
| `test_set_overwrites_existing` | `set("x",1)`, `set("x",2)`, `get` → `2`, `count() == 1` |
| `test_clear_empties_store` | set then `clear()`; `count() == 0`, `get` false |

- [ ] **Step 2: Run tests to verify they fail**

Refresh PATH, then:
```
pio test -e native
```
Expected: **FAIL** — `vars.h: No such file or directory`.

- [ ] **Step 3: Implement `vars`**

Storage is a file-scope array of `{ char name[kMaxNameLength + 1]; double value; bool used; }`, no heap, no `String`. `set` copies with `strncpy`-equivalent into the first free slot, overwrites in place if the name already exists, and returns false when full. `isValidName` implements the `[A-Za-z_][A-Za-z0-9_]*` pattern and the length cap.

- [ ] **Step 4: Run tests to verify they pass**

```
pio test -e native
```
Expected: `11 test cases: 11 succeeded`.

- [ ] **Step 5: Build firmware**

```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS.** The module is unreferenced so far; it still must compile and link cleanly on-target.

- [ ] **Step 6: Commit**

```bash
git add src/api/vars.h src/api/vars.cpp test/test_vars/test_vars.cpp platformio.ini
git commit -m "feat(vars): add global symbol table for hx scripts

Fixed 16-entry array with char[17] names — no heap, no Arduino String,
so it compiles unchanged for host tests."
```

---

### Task 3: Extract the expression evaluator

**Files:**
- Create: `src/api/expr.h`, `src/api/expr.cpp`
- Modify: `src/main.cpp:679-792` (delete `isOp`, `prec`, `applyOp`, `evalExpression`; rewire `handleCalc`)
- Create: `test/test_expr/test_expr.cpp`
- Modify: `platformio.ini` — `build_src_filter = -<*> +<api/vars.cpp> +<api/expr.cpp>`

**Interfaces:**
- Produces (consumed by Tasks 4, 5, 6, 8):
  ```cpp
  namespace harixos { namespace api { namespace expr {
  constexpr size_t kMaxExpandedLength = 256;
  bool evaluateArithmetic(const char *expression, double &out);
  bool expand(const char *expression, char *out, size_t outCapacity,
              bool (*resolve)(const char *token, size_t tokenLen, double &out));
  void setResolver(bool (*resolve)(const char *token, size_t tokenLen, double &out));
  bool evaluate(const char *expression, double &out);
  }}}}
  ```
- Consumes: nothing yet.
- Modifies: `handleCalc` in `src/main.cpp` now calls `harixos::api::expr::evaluateArithmetic(expr.c_str(), res)` instead of the local `evalExpression`.

This step is **behaviour-preserving**. `expand`, `setResolver`, and `evaluate` are declared here but implemented in Task 5 — declare all three up front so Task 5 is a pure implementation step.

- [ ] **Step 1: Write characterization tests for current behaviour**

Create `test/test_expr/test_expr.cpp`. These pin the arithmetic behaviour that already ships and must not regress in Tasks 4–5:

| Test name | Assertion |
|---|---|
| `test_precedence_multiply_over_add` | `evaluateArithmetic("1+2*3", v)` true; `v == 7` |
| `test_parentheses_override` | `evaluateArithmetic("(1+2)*3", v)` true; `v == 9` |
| `test_left_associative_subtraction` | `evaluateArithmetic("2-3-4", v)` true; `v == -5` |
| `test_decimal_division` | `evaluateArithmetic("10/4", v)` true; `v == 2.5` |
| `test_division_by_zero_is_nan` | true returned **and** `isnan(v)` |
| `test_empty_expression_fails` | `evaluateArithmetic("", v)` false |
| `test_trailing_operator_fails` | `evaluateArithmetic("1+", v)` false |
| `test_unknown_char_fails` | `evaluateArithmetic("abc", v)` false |
| `test_unbalanced_paren_fails` | `evaluateArithmetic("(1+2", v)` false |
| `test_whitespace_ignored` | `evaluateArithmetic("  1  +  2 ", v)` true; `v == 3` |

Do **not** add a unary-minus test here — it would assert today's broken behaviour and be inverted in Task 4. Leave it for Task 4.

- [ ] **Step 2: Run tests to verify they fail**

```
pio test -e native
```
Expected: **FAIL** — `expr.h: No such file or directory`.

- [ ] **Step 3: Extract the evaluator**

Move `isOp`, `prec`, `applyOp`, `evalExpression` from `src/main.cpp:679-767` into `src/api/expr.cpp`, under `namespace harixos { namespace api { namespace expr {`.

Two adaptations are required:

- Signature becomes `bool evaluateArithmetic(const char *expression, double &out)`. Internally, replace `expr.length()` with `strlen(expression)`; indexing `expression[i]` is unchanged.
- The number accumulator `String num` becomes `char numBuf[32]` with an explicit length guard — a literal longer than 31 chars truncates and fails the parse rather than overflowing.

Delete the four functions from `main.cpp` and change `handleCalc` (`src/main.cpp:785-790`) to call `harixos::api::expr::evaluateArithmetic(expr.c_str(), res)`. Add `#include "api/expr.h"` to `main.cpp`.

Leave `handleCalc` itself in place — it is coupled to `Serial` and to the `"calc"` prefix strip.

- [ ] **Step 4: Run host tests to verify they pass**

```
pio test -e native
```
Expected: `10 test cases: 10 succeeded`.

- [ ] **Step 5: Build firmware and confirm `calc` still works**

```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS.**

Serial spot-check: `calc 1 + 2` → `= 3`, `calc 1 + 2 + 3 + ... + 10` → `= 55`, `calc 2*(3-4)/5` → `= -0.4`.

- [ ] **Step 6: Commit**

```bash
git add src/api/expr.h src/api/expr.cpp test/test_expr/test_expr.cpp src/main.cpp platformio.ini
git commit -m "refactor(expr): extract the shunting-yard evaluator out of main.cpp

It lived in main.cpp's anonymous namespace, so script_engine.cpp could
not link against it. Switched to const char* so it compiles for host tests."
```

---

### Task 4: Comparisons, unary minus, static workspace

**Files:**
- Modify: `src/api/expr.cpp`
- Modify: `test/test_expr/test_expr.cpp`

**Interfaces:**
- Unchanged from Task 3.

- [ ] **Step 1: Write the failing tests**

Add to `test/test_expr/test_expr.cpp`:

| Test name | Assertion |
|---|---|
| `test_unary_minus_standalone` | `evaluateArithmetic("-5", v)` true; `v == -5` |
| `test_unary_minus_after_operator` | `evaluateArithmetic("2 * -3", v)` true; `v == -6` |
| `test_double_unary` | `evaluateArithmetic("--5", v)` true; `v == 5` |
| `test_less_than` | `evaluateArithmetic("1 < 2", v)` true; `v == 1` |
| `test_greater_than_false` | `evaluateArithmetic("3 > 4", v)` true; `v == 0` |
| `test_less_equal` | `evaluateArithmetic("2 <= 2", v)` true; `v == 1` |
| `test_greater_equal` | `evaluateArithmetic("3 >= 4", v)` true; `v == 0` |
| `test_equality_true` | `evaluateArithmetic("2 == 2", v)` true; `v == 1` |
| `test_inequality` | `evaluateArithmetic("1 != 2", v)` true; `v == 1` |
| `test_comparison_lower_precedence_than_arithmetic` | `evaluateArithmetic("1 + 2 < 4", v)` true; `v == 1` |
| `test_equality_lowest_precedence` | `evaluateArithmetic("3 > 2 == 1", v)` true; `v == 1` — parses as `(3>2)==1` |
| `test_comparison_over_unbalanced_parens` | `evaluateArithmetic("(1 < 2", v)` false |
| `test_operand_overflow_fails` | expression with 130 operands → false, no crash |

- [ ] **Step 2: Run tests to verify they fail**

```
pio test -e native
```
Expected: **FAIL** — `test_unary_minus_standalone` fails with `v == 5` (or parse failure).

- [ ] **Step 3: Implement**

Three changes to `src/api/expr.cpp`:

- **Unary minus:** when `-` is encountered in operand position (start of input, after `(`, or after an operator), push `0.0` onto `valStack` first, then push `-` as a binary operator. This yields `-5 → 0-5` and `2*-3 → 2*(0-3)` with no restructuring.
- **Multi-char operators:** replace the `isOp(char)` single-character check with a longest-match scan at the current index that recognises `<=`, `>=`, `==`, `!=` before falling back to `<`, `>`. Comparison operators are pushed with precedence 2; arithmetic becomes `* /` = 4, `+ -` = 3, so comparisons sit strictly below arithmetic, and `==`/`!=` sit below the relational operators.
- **Static workspace:** change `double valStack[MAXTOK]` and `char opStack[MAXTOK]` from locals to file-scope `static`, guarded by a `static bool s_inUse`. On entry, if `s_inUse` is already true, set `out = false` and return immediately; otherwise set it for the duration and clear it on every exit path.

- [ ] **Step 4: Run host tests; inspect the reentrancy guard**

```
pio test -e native
```
Expected: `23 test cases: 23 succeeded` (10 from Task 3 + 13 new).

Then perform the **Review Focus item 6** inspection: confirm by reading `expr.cpp` that `evaluateArithmetic` is never invoked from inside `expand` or from a resolver callback, so the `s_inUse` guard cannot fire in production. Document the result in the commit message. There is deliberately no unit test for this path.

- [ ] **Step 5: Build firmware**

```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS.** Note the RAM delta: the workspace moved from stack to `.bss`, so static RAM should rise by ~1,152 B. Anything much larger means the workspace was duplicated.

- [ ] **Step 6: Commit**

```bash
git add src/api/expr.cpp test/test_expr/test_expr.cpp
git commit -m "feat(expr): add comparison operators, unary minus, static workspace

calc -5 previously failed (main.cpp:757 forced ok=false). Workspace moved
from 1152 B of locals to a guarded static so it cannot stack under deep
run-nesting; guard has no reachable trigger and is defensive only."
```

---

### Task 5: Pre-expansion (value tokens and `$name`)

**Files:**
- Modify: `src/api/expr.cpp` (implement `expand`, `setResolver`, `evaluate`)
- Create: `src/api/value_tokens.h`, `src/api/value_tokens.cpp` — **device only, never in `build_src_filter`**
- Modify: `src/main.cpp` — call `expr::setResolver(resolveDeviceValueToken)` in `setup()`
- Modify: `test/test_expr/test_expr.cpp`

**Interfaces:**
- Consumes: `vars::get` from Task 2; the Task 3 declarations of `expand`/`setResolver`/`evaluate`.
- Produces: the working `evaluate()` that Tasks 6 and 8 call. Resolver signature is
  `bool (*)(const char *token, size_t tokenLen, double &out)`.
  The resolver receives the **complete bare-word text including any numeric argument**, e.g. `"heap"` or `"readpin 2"`.
- `src/api/value_tokens.cpp` defines `bool resolveDeviceValueToken(const char *token, size_t tokenLen, double &out)` handling exactly: `heap`, `adc`, `readpin <n>`, `uptime`, `millis`, `time`.

`build_src_filter` must **not** gain `value_tokens.cpp` — it needs `ESP.getFreeHeap()` and friends and cannot compile on the host.

- [ ] **Step 1: Write the failing tests**

Use a file-scope fake resolver in `test/test_expr/test_expr.cpp`:

```cpp
static bool fakeResolve(const char *token, size_t tokenLen, double &out);
// returns 41984 for "heap", 1 for "readpin 2", false otherwise
```

`setUp()` calls `vars::clear()` and `expr::setResolver(fakeResolve)`.

| Test name | Assertion |
|---|---|
| `test_expand_substitutes_variable` | `set("x", 5)`; `expand("$x + 1", out, cap, fakeResolve)` → `out` equals `"5 + 1"` |
| `test_expand_undefined_variable_fails` | `expand("$nope", out, cap, fakeResolve)` **false** — Review Focus 1 |
| `test_expand_value_token_heap` | `expand("heap", ...)` → `"41984"` |
| `test_expand_value_token_with_argument` | `expand("readpin 2 + 1", ...)` → `"1 + 1"`; resolver must have received `"readpin 2"` |
| `test_expand_unknown_bare_word_fails` | `expand("banana", ...)` false |
| `test_expand_rejects_over_length_result` | input that expands past `kMaxExpandedLength` → false |
| `test_expand_leaves_pure_arithmetic_untouched` | `expand("1 + 2", ...)` → `"1 + 2"` |
| `test_evaluate_combines_expand_and_arithmetic` | `set("x", 5)`; `evaluate("$x * 2", v)` true; `v == 10` |
| `test_evaluate_fails_when_resolver_absent` | `setResolver(nullptr)`; `evaluate("heap", v)` false |
| `test_repeated_variable_in_one_expression` | `set("x", 3)`; `evaluate("$x + $x", v)` true; `v == 6` |

- [ ] **Step 2: Run tests to verify they fail**

```
pio test -e native
```
Expected: **FAIL** — `expand` is declared but not defined (link error).

- [ ] **Step 3: Implement `expand`, `setResolver`, `evaluate`**

`expand` walks the input left to right into a caller-supplied `char` buffer:

1. On `$`, read `[A-Za-z_][A-Za-z0-9_]*`, look it up via `vars::get`; if undefined, fail with `out` untouched by the caller's perspective (return false).
2. On an identifier start **not** preceded by `$`, consume the identifier plus a following numeric argument if present (`readpin 2`), pass the whole span to the resolver; if the resolver returns false, fail.
3. On `+ - * / ( ) < > = !` and digits, copy through.
4. Any other character fails.
5. Before every append, check remaining capacity against `kMaxExpandedLength`.

`setResolver` stores the function pointer in a file-scope variable, `nullptr` initially. `evaluate` is `expand` into a `char buf[kMaxExpandedLength + 1]` followed by `evaluateArithmetic(buf, out)`.

Then implement `resolveDeviceValueToken` in `src/api/value_tokens.cpp` for the six tokens, and call `expr::setResolver(resolveDeviceValueToken);` in `setup()` immediately after `LittleFS.begin()`.

- [ ] **Step 4: Run host tests to verify they pass**

```
pio test -e native
```
Expected: `33 test cases: 33 succeeded`.

- [ ] **Step 5: Build firmware and spot-check on hardware**

```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS.**

Serial spot-check: `calc heap` → a plausible byte count; `calc $nope` → `Invalid expression.`

- [ ] **Step 6: Commit**

```bash
git add src/api/expr.cpp src/api/value_tokens.h src/api/value_tokens.cpp src/main.cpp test/test_expr/test_expr.cpp
git commit -m "feat(expr): pre-expand \$name and value tokens before evaluation

Expansion happens ahead of the shunting-yard pass, so the parser still
sees only numbers and operators and needs no identifier support. Undefined
variables fail loudly rather than defaulting to zero."
```

---

### Task 6: `set` in the shell and in scripts

**Files:**
- Modify: `src/api/expr.h`, `src/api/expr.cpp` (add `parseSet`)
- Modify: `src/main.cpp` — add a `set` branch to `executeCommand`
- Modify: `src/api/script_engine.h`, `src/api/script_engine.cpp` — add `handleSetCommand` and its dispatch branch
- Modify: `test/test_expr/test_expr.cpp`

**Interfaces:**
- Consumes: `vars::set`, `expr::evaluate` from Tasks 2 and 5.
- Produces two things, both in `expr`:
  ```cpp
  bool parseSet(const char *line, char *nameOut, size_t nameCap,
                const char **expressionOut);
  const char *assign(const char *argsAfterSet, double *valueOut);
  ```
  `parseSet` handles `set <name> = <expr>` where `line` points at the text *after* the leading `set`; it returns false on a missing `=`, an invalid name, or an empty expression.

  `assign` runs the whole sequence — `parseSet`, then `expr::evaluate`, then `vars::set` — behind one call, returning `nullptr` on success or a static message on failure. **On failure no variable is modified.** Both dispatchers call `assign` rather than composing the three steps themselves, so Review Focus 2 has exactly one implementation and one test instead of two untested copies.

- [ ] **Step 1: Write the failing tests for the parser**

| Test name | Assertion |
|---|---|
| `test_parse_set_spaced` | `" x = 1+2"` → name `"x"`, expression `"1+2"` |
| `test_parse_set_unspaced` | `"x=1+2"` → name `"x"`, expression `"1+2"` |
| `test_parse_set_missing_equals` | `" x 1+2"` false |
| `test_parse_set_missing_expression` | `" x = "` false |
| `test_parse_set_invalid_name` | `" 1x = 1"` false |
| `test_parse_set_empty_name` | `" = 1"` false |
| `test_parse_set_expression_may_contain_spaces` | `" x = readpin 2 + 1"` → expression `"readpin 2 + 1"` |

Then the `assign` tests — these are the ones Review Focus 2 depends on:

| Test name | Assertion |
|---|---|
| `test_assign_success_stores_and_reports_value` | `assign("x = 1 + 2", &v)` returns nullptr; `v == 3`; `vars::get("x", v2)` true and `v2 == 3` |
| `test_assign_leaves_variable_untouched_on_error` | `vars::set("x", 5)`; `assign("x = $nope", &v)` returns non-null; `vars::get("x", v2)` true and `v2 == 5` — **Review Focus 2** |
| `test_assign_rejects_invalid_name_without_storing` | `assign("1bad = 1", &v)` non-null; `vars::count()` unchanged |
| `test_assign_rejects_missing_equals` | `assign(" x 1+2", &v)` non-null; `vars::count()` unchanged |

`setUp()` must call `vars::clear()`.

- [ ] **Step 2: Run tests to verify they fail**

```
pio test -e native
```
Expected: **FAIL** — `parseSet` undefined.

- [ ] **Step 3: Implement `parseSet` and `assign`**

Add both declarations to `src/api/expr.h` and both definitions to `src/api/expr.cpp`.

`parseSet` splits at the first `=`, validates the left side with `vars::isValidName`, rejects a right side that is empty after trimming, and on success writes the name into `nameOut` (bounded by `nameCap`) and points `expressionOut` at the trimmed right-hand text within the caller's buffer.

`assign` composes the three steps **in this order**:

```cpp
const char *assign(const char *argsAfterSet, double *valueOut) {
  // 1. parseSet  -> on failure return its message, nothing touched
  // 2. expr::evaluate(expression, v) -> on failure return "Invalid expression.",
  //                                     nothing touched
  // 3. vars::set(name, v)            -> only reached after 1 and 2 both succeeded
  // 4. *valueOut = v; return nullptr;
}
```

The ordering in step 2/3 **is** the Review Focus 2 guarantee. Do not reorder, do not store first, and do not catch-and-continue.

- [ ] **Step 4: Run host tests to verify they pass**

```
pio test -e native
```
Expected: `44 test cases: 44 succeeded` (33 from Task 5 + 7 `parseSet` + 4 `assign`).

- [ ] **Step 5: Wire `set` into the shell**

In `src/main.cpp`'s `executeCommand`, add a branch alongside the existing `calc` branch:

```cpp
} else if (command == F("set")) {
  handleSet(line);
}
```

`handleSet(const String &line)` (forward-declared with the other handlers near `main.cpp:343`) strips the leading `set ` and calls `expr::assign(rest.c_str(), &value)`. On success it prints `<name> = <value>` using `%.10g`; on failure it prints the returned message. It contains no parse or store logic of its own.

- [ ] **Step 6: Wire `set` into scripts**

Add a `static ApiResult handleSetCommand(const String &args, Stream &output);` declaration to `src/api/script_engine.h` beside the other handlers, its definition in `script_engine.cpp`, and a `} else if (cmd.name == "set") {` branch in `executeCommand` next to the existing `print`/`gpio` branches.

`args` arrives as the text after `set `, so it goes straight to `expr::assign(args.c_str(), &value)`. On success return `ApiResult(API_OK, "")` — an empty message, so `executeScript`'s `[OK]` printer (`script_engine.cpp:270-271`) stays silent. On failure return `ApiResult(API_INVALID_ARGUMENT, <message>)`.

Both paths are now a single `assign` call: **Review Focus 2 is enforced by `test_assign_leaves_variable_untouched_on_error`**, not by two code paths each remembering to check their return value.

- [ ] **Step 7: Build and run the serial checklist**

```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS.**

Serial, at the shell prompt:

```
set x = 5          -> x = 5
set x = $x * 2 + 1 -> x = 11
set x = $nope      -> error, and `set x = $x` afterwards still reports 11  (Review Focus 2)
set heap = 5       -> heap = 5
set b = heap       -> a free-heap byte count (bare word ≠ $heap)
set 1bad = 1       -> error: invalid name
```

Then install a `.hx` containing `set a = readpin 2` and `set b = $a + 1`, run it, and confirm no `[OK] set ...` lines appear.

- [ ] **Step 8: Commit**

```bash
git add src/api/expr.h src/api/expr.cpp src/main.cpp src/api/script_engine.h src/api/script_engine.cpp test/test_expr/test_expr.cpp
git commit -m "feat(lang): add set for variables in scripts and at the shell

Both dispatchers call one expr::assign, which evaluates before it stores
— so an invalid expression can never clobber the existing value."
```

---

### Task 7: Block stack

**Files:**
- Create: `src/api/block_stack.h`, `src/api/block_stack.cpp`
- Create: `test/test_block_stack/test_block_stack.cpp`
- Modify: `platformio.ini` — append `+<api/block_stack.cpp>` to `build_src_filter`

**Interfaces:**
- Produces (consumed by Task 8):
  ```cpp
  namespace harixos { namespace api {
  constexpr size_t kMaxBlockDepth = 8;
  enum class BlockEvent { If, Else, End, Other };
  class BlockStack {
   public:
    bool skipping() const;
    size_t depth() const;
    const char *onEvent(BlockEvent ev, bool cond);  // nullptr = ok, else static error text
    int unclosedCount() const;                      // 0 when balanced
    void reset();
   };
  }}}
  ```
  Error strings are `"else without matching if"`, `"end without matching if"`, `"Block nesting limit exceeded"` (spec §6 wording, copy verbatim).
- Consumes: nothing. No Arduino headers, no `String`.

- [ ] **Step 1: Write the failing tests**

| Test name | Assertion |
|---|---|
| `test_top_level_runs` | `skipping()` false at depth 0 |
| `test_true_condition_runs_body` | `onEvent(If, true)` ok; `skipping()` false |
| `test_false_condition_skips` | `onEvent(If, false)` ok; `skipping()` true |
| `test_else_switches_to_running` | If(false), then Else → `skipping()` false |
| `test_else_after_true_switches_to_skipping` | If(true), then Else → `skipping()` true |
| `test_end_after_true_branch_closes` | If(true), End → depth 0, `unclosedCount()` 0 |
| `test_end_after_false_branch_skips_to_close` | If(false), End → depth 0, `skipping()` false |
| `test_nested_if_inside_skipped_branch` | If(false), If(anything), End → still depth 1 and skipping; then End → depth 0 — **Review Focus 3** |
| `test_else_inside_skipped_branch_ignored` | If(false), If(false), Else → still skipping, depth 2 |
| `test_other_event_while_skipping_does_not_unskip` | If(false), then `onEvent(Other, false)` → still skipping |
| `test_else_without_if_errors` | `onEvent(Else, false)` at depth 0 → non-null, equals `"else without matching if"` |
| `test_end_without_if_errors` | `onEvent(End, false)` at depth 0 → non-null |
| `test_depth_overflow_errors` | 8 successful `onEvent(If, true)`, then a 9th → non-null, equals `"Block nesting limit exceeded"` |
| `test_unclosed_count_reports` | If(true) only → `unclosedCount() == 1` |
| `test_reset_clears` | If(true), `reset()` → depth 0, not skipping |

Note `test_nested_if_inside_skipped_branch` and `test_else_inside_skipped_branch_ignored` both pass `cond` while skipping — the implementation must ignore it.

- [ ] **Step 2: Run tests to verify they fail**

```
pio test -e native
```
Expected: **FAIL** — `block_stack.h: No such file or directory`.

- [ ] **Step 3: Implement `BlockStack`**

File-scope `Frame frames_[kMaxBlockDepth]` of `{bool running; bool seenElse;}` plus `size_t depth_`.

- `onEvent(If, cond)`: if `depth_ == kMaxBlockDepth` return the overflow error; if already `skipping()`, push `{false, true}` (nested, fully swallowed) and ignore `cond`; otherwise push `{cond, false}`.
- `onEvent(Else, ...)`: if `depth_ == 0` return the else error; if `frames_[depth_-1].seenElse` return `nullptr` (already consumed); otherwise flip `running` and set `seenElse = true`.
- `onEvent(End, ...)`: if `depth_ == 0` return the end error; else `--depth_`.
- `onEvent(Other, ...)`: always `nullptr` — the caller decides via `skipping()`.

- [ ] **Step 4: Run tests to verify they pass**

```
pio test -e native
```
Expected: `15 test cases: 15 succeeded`.

- [ ] **Step 5: Build firmware**

```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS.**

- [ ] **Step 6: Commit**

```bash
git add src/api/block_stack.h src/api/block_stack.cpp test/test_block_stack/test_block_stack.cpp platformio.ini
git commit -m "feat(lang): add forward-only block stack for if/else/end

Separate from ScriptDepthGuard's kMaxScriptDepth — that one caps run
recursion, this caps if nesting. Conflating them would break an if
inside a nested script."
```

---

### Task 8: `if` / `else` / `end` in `executeScript`

**Files:**
- Modify: `src/api/script_engine.cpp` (`executeScript`, lines ~240-283)

**Interfaces:**
- Consumes: `BlockStack` and `BlockEvent` from Task 7; `expr::evaluate` from Task 5.
- Produces: control flow inside `.hx` files. Deliberately **not** added to `executeCommand`, so a scheduled `if` still reports `Unknown command: if` — blocks must not be schedulable.

- [ ] **Step 1: Implement the routing**

Add a `harixos::api::BlockStack blocks;` local before the existing loop at `script_engine.cpp:256-277`, then for each line that survives the existing empty-`#` filter (`script_engine.cpp:265`), match the first word lowercased:

| First word | Action |
|---|---|
| `if` | if `blocks.skipping()`, call `onEvent(If, false)` and discard the line. Otherwise `expr::evaluate` the remainder into `double v`; on failure print `[ERROR] if <expr>: Invalid expression.`, increment `errorCount`, and use `false`. Push via `onEvent(If, ok && !isnan(v) && v != 0)`. |
| `else` | `blocks.onEvent(Else, false)`; if it returns non-null print `[ERROR] <line>: <message>` and increment `errorCount`. Discard the line. |
| `end` | same, with `End`. |
| anything else | if `blocks.skipping()`, discard silently. Otherwise call `executeCommand` as today. |

Any non-null result from `onEvent` is an error, counted exactly as `executeScript` already counts command errors.

- [ ] **Step 2: Report unclosed blocks**

After the loop, before the `--- Script Complete ---` line, if `blocks.unclosedCount() > 0` print `[ERROR] <n> unclosed if block(s)` and add it to `errorCount`.

- [ ] **Step 3: Build and run the serial checklist — this is Review Focus 4**

```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS.**

Install and run a `.hx` exercising each case:

```
# 1. true branch
set x = 1
if $x == 1
  print A
else
  print B
end                       -> prints A only

# 2. false branch
set x = 0
if $x == 1
  print A
else
  print B
end                       -> prints B only

# 3. nested if inside a skipped branch (Review Focus 3)
set x = 0
if $x == 1
  if $x == 2
    print DEEP
  end
  print INNER
else
  print B
end                       -> prints B only; no DEEP, no INNER

# 4. comment must not close a block (Review Focus 4)
if $x == 0
  print C
# end
  print D
end                       -> prints C and D; Script Complete shows 0 errors

# 5. bad condition
if notanexpression
  print X
end                       -> [ERROR] reported, X NOT printed, script continues

# 6. unclosed block
if $x == 0
  print Y                  -> [ERROR] 1 unclosed if block(s) in the summary
```

- [ ] **Step 4: Confirm blocks are not schedulable**

```
schedule add */5 * * * * if 1
```
Expected: `[ERROR] Unknown command: if`. (If Plan C is already merged, the syntax is 6-field; otherwise use the then-current form.) This confirms Task 7's interface note and prevents a block from being fired one line at a time.

- [ ] **Step 5: Run host tests as a regression gate**

```
pio test -e native
```
Expected: `44 test cases: 44 succeeded` — unchanged from Task 6, since this task touches no host-compilable module.

- [ ] **Step 6: Commit**

```bash
git add src/api/script_engine.cpp
git commit -m "feat(lang): add if/else/end to hx scripts

Structural keywords are handled in executeScript's line loop and never
reach executeCommand, so blocks cannot be scheduled one line at a time.
Comment filtering still precedes keyword matching, so '# end' is inert."
```

---

### Task 9: Final verification and language documentation

**Files:**
- Modify: `SCRIPT-REFERENCE.md` (add `set`, `if`, `else`, `end` sections)

- [ ] **Step 1: Update `SCRIPT-REFERENCE.md`**

Document `set <name> = <expression>`, the `$name` versus bare-value-token rule (with the `set heap = 5` / `$heap` / `heap` example from spec §5.3), the six value tokens, `if` / `else` / `end`, the 8-block nesting cap, and that `&&`/`||`/`elif`/`for` are not supported (`while`/`break` were added 2026-10-01 — see `SCRIPT-REFERENCE.md`).

- [ ] **Step 2: Clean rebuild**

```
pio run -t clean -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS**, no `error:` and no `warning:` from `src/`. Record `RAM:` and `Flash:` — versus the pre-plan baseline of `35936 B` / `510115 B`, expect roughly +1.5 KB RAM (static workspace + vars store + block stack) and +8–12 KB flash. A jump beyond ~20 KB flash means something unexpected got linked.

- [ ] **Step 3: Full host test run**

```
pio test -e native
```
Expected: all suites pass, 0 failures.

- [ ] **Step 4: Full serial checklist**

Run every numbered item from spec §9.3 that concerns `set`, `calc`, and `if` — Tasks 6 and 8 covered them individually, this pass re-runs them against the final build in one session.

- [ ] **Step 5: Confirm constraints held**

```
grep -n "lib_deps" platformio.ini
git status --short
git log --oneline main..HEAD
```
Expected: no `lib_deps`; clean tree; **nine commits** — one per task, including this one's docs.

- [ ] **Step 6: Commit**

```bash
git add SCRIPT-REFERENCE.md
git commit -m "docs(lang): document set and if/else/end in the script reference"
```
