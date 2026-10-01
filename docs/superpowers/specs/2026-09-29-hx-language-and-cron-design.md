# Design: `.hx` language extensions + 6-field cron scheduler

Date: 2026-09-29
Status: approved (design sections reviewed individually)
Branch baseline: `fix/critical-bugs` (5 commits ahead of `main`)

## 1. Goal

Add four capabilities to HarixExtOS:

1. WiFi credentials persist across reboot for **every** connect path
2. Scheduled tasks persist across reboot, expressed as **6-field cron**
3. **Variables** in `.hx` scripts (e.g. hold a GPIO read)
4. **`if` / `else` / `end`** in `.hx` scripts

Success: after a power cycle the device re-joins WiFi and resumes its
schedule; a script can read a GPIO into a variable and branch on it.

## 2. Feasibility

Measured from built artifacts in `.pio/build/` (baseline after the
`fix/critical-bugs` work: 510,115 B flash / 35,936 B static DRAM).

The default environment is `esp01_1m` (`platformio.ini:12`), which is the
tightest target: app region 761,840 B (`eagle.flash.1m256.ld:13`), FS
262,144 B, DRAM 81,920 B.

| Resource | Free today | Estimated cost | Margin |
|---|---|---|---|
| App flash (`esp01_1m`) | 251,725 B | ~10–12 KB | ~20x |
| Static DRAM | 45,984 B | ~1.5 KB | ~30x |
| LittleFS (256 KB) | ~249 KB | <1 KB | ~250x |
| IRAM | 4,041 B (87.7% used) | **0** | n/a |

**Conclusion: it fits with roughly 20x margin.**

All new code lands in `.irom0.text` (flash-resident). IRAM is untouched
because nothing here is `IRAM_ATTR`. The only way to exhaust the budget
would be adding a library dependency; `platformio.ini` has no `lib_deps`
today, so the cron parser is hand-rolled.

## 3. Decisions

| Topic | Decision |
|---|---|
| Architecture | Shared layer `src/api/expr.{h,cpp}` + `src/api/vars.{h,cpp}`; both dispatchers call it |
| Variable scope | One **global** table shared by shell, scripts, and scheduler |
| Value capture | `set x = <expr>` with pre-expansion of value tokens and `$name` |
| `set` availability | Shell prompt **and** scripts |
| `if` availability | **Scripts only** (no multi-line buffering at the prompt) |
| Control flow | Forward-only `if` / `else` / `end` + `while` / `break` (added 2026-10-01); no `elif` |
| Scheduler format | 6-field cron (`sec min hour dom month dow`), the only syntax |
| `HH:MM` and `+Ns` | **Dropped** — breaking change, docs rewritten |
| `dom` / `dow` | **AND** semantics (deviates from standard cron, documented) |
| Scheduler persistence | Auto-save to `/harixos/schedule.cfg` on every add/remove |
| Scheduled commands | Widen `ScriptEngine` keyword list (not a bridge to the shell) |
| Verification | Host unit tests on a PlatformIO `native` env + 4-env build gate |

## 4. Workstream 1 — WiFi persistence gap fills

### Current state (already works)

`wifi connect` saves on success at `src/main.cpp:854-856` into
`/harixos/settings.cfg` via `harixos::saveSettings`
(`src/apps/settings/settings.cpp:71-82`). `setup()` reloads at
`src/main.cpp:2087`, and `tryAutoWifi()` re-joins at `src/main.cpp:2099`.

The SDK-level persistence flag is deliberately unused: the project never
calls `WiFi.persistent(true)`, so `/harixos/settings.cfg` is the sole
mechanism. This design does not change that.

### Gaps to fill

**Gap A — the `serve` interactive prompt.**
`src/main.cpp:995` calls `WiFi.begin(ssid, pass)` and reports success at
`:1011` without ever writing settings. Fix: on success, assign
`shellSettings.wifiSSID` / `.wifiPassword` and call
`harixos::saveSettings(shellSettings)` — same as `handleWifiConnect`.

**Gap B — `WiFiAPI::connect()`.**
`src/api/wifi_api.cpp:50-69` returns success at `:64-65` without saving.
This path is reached by `wifi connect` from a `.hx` script and from a
scheduled command, so those connects silently never persist. Fix: save on
success.

### Required refactor

`shellSettings` is declared at `src/main.cpp:42`, **inside** the anonymous
namespace opened at `:32`. It therefore has internal linkage and cannot be
referenced from `wifi_api.cpp` or `settings.cpp`.

Move it to `src/apps/settings`:

- `settings.h` gains `extern AppSettings shellSettings;`
- `settings.cpp` defines it
- `main.cpp` drops its own definition and keeps using the symbol unchanged

This is a precondition for Gap B, and is independently worthwhile: `serve`,
`wifi`, and `settings` all need the same object.

### Not in scope

- Saving on a **failed** connect attempt
- Making `wifi disconnect` clear stored credentials
- Removing the blocking scan at boot in `tryAutoWifi()`
- Calling `WiFi.setAutoReconnect(true)` from `tryAutoWifi()`

## 5. Workstream 2 — expression and variable layer

### 5.1 `src/api/vars.{h,cpp}`

One global symbol table, no scoping. What is `set` at the prompt is visible
in the next script run, which is what makes interactive testing useful.

```cpp
namespace harixos::api::vars {
constexpr size_t kMaxVars = 16;
constexpr size_t kMaxNameLength = 16;

bool set(const String &name, double value);
bool get(const String &name, double &out);   // false if undefined
void clear();
size_t count();
const String &nameAt(size_t index);
double valueAt(size_t index);
}
```

Storage: fixed array of `{String name; double value;}` = 16 x ~20 B
~ 320 B static.

Name rules: must match `[A-Za-z_][A-Za-z0-9_]*`, at most
`kMaxNameLength` chars. A name that fails validation is rejected with an
error, not silently renamed.

### 5.2 `src/api/expr.{h,cpp}`

The evaluator currently lives in `main.cpp`'s anonymous namespace at
`src/main.cpp:679-767`, which gives it internal linkage — other
translation units cannot call it. It must be moved out.

Dependency audit of `main.cpp:679-767` (`isOp`, `prec`, `applyOp`,
`evalExpression`): uses only Arduino `String`, `ctype`, `stdlib`, and
`math`. It does **not** touch `Serial`, `TokenizedLine`, `tokenize`,
`inputLine`, `currentWorkingDirectory`, or `shellSettings`. Extraction is
therefore near-free.

```cpp
namespace harixos::api::expr {
// Evaluates arithmetic, comparisons, and `$name` substitution.
bool evaluate(const String &expression, double &out);
}
```

`handleCalc` (`src/main.cpp:769-792`) stays in `main.cpp` — it is coupled
to `Serial`, to the `"calc"` prefix strip, and to raw-line input. It simply
calls `expr::evaluate`.

#### Operators

| Tier | Operators | Associativity |
|---|---|---|
| 1 (highest) | unary `-` | right |
| 2 | `*` `/` | left |
| 3 | `+` `-` | left |
| 4 | `<` `<=` `>` `>=` | left |
| 5 (lowest) | `==` `!=` | left |

Comparisons yield `1.0` / `0.0` so that `calc`, `set`, and `if` share one
evaluator.

**Unary minus.** Today `calc -5` fails: `-` is pushed to `opStack`, and at
drain time `vTop (0) < 1` forces `ok = false` (`src/main.cpp:757`). Fix by
the standard push-`0` trick — when `-` appears in operand position, push
`0` onto `valStack` before pushing `-`. This makes `-5` -> `0 - 5` and
`2 * -3` -> `2 * (0 - 3)` with no parser restructuring.

**Multi-char operators.** `isOp()` (`src/main.cpp:680-682`) takes a single
`char`, so `==`, `<=`, `>=`, `!=` require a tokenizer change: consume the
longest matching operator at the current position rather than one
character.

**`&&` and `||` are not implemented** in this pass. Nested `if` covers the
use case and the operators can be added later without breaking anything.

#### Static workspace

`evalExpression` allocates `MAXTOK = 128` doubles plus 128 chars —
1,152 B of locals (`src/main.cpp:701-703`). Under `kMaxScriptDepth = 8`
levels of `run` recursion this stacks up.

Evaluation is never reentrant: an `if` finishes evaluating before its body
runs, `set` never nests, `calc` never nests. Make the workspace `static`
with a simple in-use guard that returns an error if entered while busy.
This moves 1,152 B off the stack and onto the 46 KB of free DRAM.

#### Pre-expansion

Before the shunting-yard pass, the expression string is rewritten. The
parser therefore still sees only numbers and operators and needs no
identifier support.

Order matters:

1. **Bare value tokens** are replaced with their numeric value.
2. **`$name`** is replaced with the stored variable value.

Value tokens for the first cut:

| Token | Value |
|---|---|
| `heap` | `ESP.getFreeHeap()` |
| `adc` | `analogRead(A0)` |
| `readpin <n>` | `digitalRead(n)` |
| `uptime` | seconds since boot |
| `millis` | `millis()` |
| `time` | epoch seconds from `time(nullptr)` |

**Naming rule that keeps the two unambiguous: a bare word is always a
value token; a variable is always referenced as `$word`.** So
`set heap = 5` stores a variable under the name `heap`, but bare `heap`
still reads real free heap; the stored value is read back as `$heap`.

An **undefined `$name` is an error**, never a silent `0`. A value token
whose meaning cannot be produced (e.g. `readpin` with a non-numeric or
missing argument) is likewise an error.

Expression length is bounded by `kMaxLineLength = 160` at the shell
(`src/main.cpp:34`) and by the `MAXTOK` stack limits inside the evaluator.

### 5.3 `set` syntax

```
set <name> = <expression>
```

- The expression is **fully evaluated before the store**, so
  `set count = $count + 1` reads the previous value. Read-modify-write
  works; there is no partial-update window.
- Shell handler prints `<name> = <value>` for feedback.
- Script handler returns `ApiResult(API_OK, "")` so no `[OK]` line is
  emitted by `executeScript` (`src/api/script_engine.cpp:270-271`).
- Both call the same `vars` + `expr` functions.
- Malformed form (missing `=`, bad name, bad expression) prints an error
  and does not modify the variable.

Example session:

```
set x = 5          -> x = 5
set x = $x * 2 + 1 -> x = 11
set heap = 5       -> heap = 5      (variable named "heap")
set h = $heap      -> h = 5         (reads the variable)
set b = heap       -> b = 41984     (bare word reads real free heap)
```

## 6. Workstream 3 — `if` / `else` / `end`

### Syntax

```
set level = readpin 2
if $level == 1
  print LED is on
else
  print LED is off
end
```

Line-oriented and **forward-only** apart from `while`, which re-jumps to
its body at each matching `end` (added 2026-10-01, with `break`; guarded
by a 100,000-iteration and 5-minute cap). No `goto` or `elif`. `else` is
optional. Keywords are case-insensitive and must be the first token on
the line.

### Placement

Implemented in the index-based line loop already present in
`ScriptEngine::executeScript` (`src/api/script_engine.cpp:256-277`). That
loop scans by index and is already the natural seam for forward skipping;
no restructuring of `executeScript` is required.

Not available at the shell prompt — the shell would need to buffer lines
until `end`, which is a separate and larger change.

### Mechanism

```cpp
struct BlockFrame { bool running; bool seenElse; };
// Fixed-size array of kMaxBlockDepth frames, on executeScript's stack.
// No heap, no std::vector.
```

Guards in the **Guards** table below are checked first: `else` and `end`
on an empty stack are errors before any state transition is attempted.

**While running** (top frame `running == true`, or stack empty):

- `if <cond>` — evaluate; push `{condTrue, false}`
- `else` — flip `running`, set `seenElse = true`
- `end` — pop
- anything else — pass to `executeCommand`

**While skipping** (top frame `running == false`):

- `if` — push `{false, true}` so its `else` and `end` are swallowed
- `else` — if `!seenElse`, set `seenElse = true` and `running = true`
  (this is our own branch becoming active); otherwise ignore
- `end` — pop
- anything else — discard

### Guards

| Condition | Behaviour |
|---|---|
| `else` / `end` with empty stack | error, line counted |
| push with `frames.size() >= kMaxBlockDepth` (8) | error `"Block nesting limit exceeded"` |
| unclosed blocks at end of script | reported in the completion summary |

`kMaxBlockDepth` is deliberately **separate from `kMaxScriptDepth = 8`**
(commit `26304ce`), which caps `run` recursion via `ScriptDepthGuard`.
Conflating them would break an `if` inside a nested script.

### Ordering rule

Comment lines are filtered first (`src/api/script_engine.cpp:265`), and
**then** structural keywords are matched. Otherwise `# end` would close a
block.

### Truthiness

`ok && !isnan(v) && v != 0`.

A condition that fails to evaluate prints an error, increments the script's
error count, and is treated as **false** — consistent with the engine's
existing behaviour where errors do not abort the script
(`src/api/script_engine.cpp:267-274`).

### Cost

8 frames x 2 bools = 16 B on `executeScript`'s stack.

### Scheduler interaction

Blocks cannot be scheduled. The scheduler calls the single-line
`ScriptEngine::executeCommand` (`src/kernel/scheduler/scheduler.cpp:88`),
which will not recognise `if`. Correct by construction.

## 7. Workstream 4 — 6-field cron scheduler

### 7.1 Format

```
sec min hour dom month dow <command>
```

| Field | Range |
|---|---|
| sec | 0–59 |
| min | 0–59 |
| hour | 0–23 |
| dom | 1–31 |
| month | 1–12 (numbers only, no `JAN`/`FEB`) |
| dow | 0–6, **0 = Sunday** (no `7`) |

Per-field syntax: `*`, `n`, `a-b`, `*/n`, `a-b/n`, and comma lists such as
`1,3,5`. The six-field form is a HarixOS extension over standard 5-field
cron and must be documented as such.

There is no `@reboot`.

Parsing edge cases, decided here so the implementation and the tests agree:

| Rule | Behaviour |
|---|---|
| `*/n` start value | Begins at the field's **minimum**: sec/min/hour/dow -> 0, dom -> 1, month -> 1. So `*/2` in `dom` yields 1,3,5,… and in `dow` yields 0,2,4,6. |
| `a-b` wrapping | Ranges **do not wrap**. `5-1` is rejected; `fri-mon` is not expressible. |
| `a-b/n` remainder | Steps from `a` while `<= b`; a step that does not divide the range evenly simply stops early. |
| Negative / zero step | `n <= 0` is rejected. |
| Field count | Fewer than 6 cron fields, or no command after them, is rejected with a field-count error — never registered as a partial task. |
| Reversed range | `a > b` is rejected (consistent with no-wrapping). |

There is no `@reboot`.

### 7.2 `dom` / `dow` semantics: AND

Standard cron ORs `dom` and `dow` when both are restricted — a
long-standing and frequently surprising behaviour. This design **AND**s
them: both must match. Predictability is preferred over strict
interoperability, and the deviation is documented.

`domRestricted` / `dowRestricted` flags are recorded at parse time so the
`*` case is handled without special-casing the caller.

### 7.3 Parsing: once, into masks

Masks are built at add-time and at load-time, never per tick.

```cpp
struct CronSpec {                       // ~28 B
  uint64_t sec;                         // 60 bits
  uint64_t minute;                      // 60 bits
  uint32_t hour;                        // 24 bits
  uint32_t dom;                         // 31 bits (bits 1..31)
  uint16_t month;                       // 12 bits
  uint8_t  dow;                         // 7 bits
  bool domRestricted;
  bool dowRestricted;
};
```

The original expression string is retained alongside the masks for display
and persistence fidelity, so `*/5` round-trips as `*/5` rather than
`0,5,10,15,...`.

A malformed expression fails the whole `add` with an error naming the bad
field; it never partially registers a task.

### 7.4 Matching

`Scheduler::update` (`src/kernel/scheduler/scheduler.cpp:70-105`) replaces
the current `hour`/`minute`/`second` triple comparison with mask lookups
against `localtime_r()`.

- The existing per-second dedup at `:74-81` already tracks hour, minute,
  and second, so second-level cron works unchanged.
- The `now >= 1000000000` guard at `:71-72` is retained (daily and cron
  tasks cannot fire without valid wall-clock time), but it now prints an
  explicit warning instead of silently doing nothing.

### 7.5 Persistence

| Aspect | Decision |
|---|---|
| Path | `/harixos/schedule.cfg` (matches `/harixos/settings.cfg`) |
| Format | One task per line; **first 6 whitespace-separated fields are cron, the remainder is the command** |
| Writer | `harixos::writeText()` (`src/kernel/filesystem/filesystem.cpp:214`), which auto-creates parent dirs |
| When | **Auto-save on every add and remove**, mirroring `settings banner/update/timezone` (`src/main.cpp:627,646,660`). No `schedule save` command. |
| When (read) | `setup()`, immediately after `LittleFS.begin()` and next to `shellSettings` (`src/main.cpp:2087`) |
| IDs | `nextId` recomputed as `max(id) + 1` on load, so `schedule remove <id>` ids stay stable across reboots |
| Missing file | Silent, same as `settings.cpp:14-16` |
| Malformed line | Skip that line with a warning; do not abort the load |

Splitting on the first six whitespace fields is unambiguous precisely
because the cron portion is fixed-width in field count while the command
may contain arbitrary spaces.

### 7.6 Data structure changes

The `ScheduledTask` struct (`src/kernel/scheduler/scheduler.h:13-21`)
replaces `TaskType type`, `hour`, `minute`, `second`, and
`executeAtMillis` with `CronSpec spec` plus the retained expression
string. `id` and `command` are unchanged.

`MAX_TASKS = 20` is unchanged. Static cost moves from the measured 740 B
(`0x2E4`, `systemScheduler` symbol) to roughly 1.3 KB; cron strings
allocate heap only while tasks exist.

### 7.7 Breaking change

`schedule add 14:30 <cmd>` and `schedule add +5m <cmd>` are removed
outright. No compatibility layer, no sugar.

`Documentation/Commands.md` and `SCRIPT-REFERENCE.md` schedule sections
are rewritten with 6-field examples. No on-device migration is needed —
`/harixos/schedule.cfg` does not exist yet.

## 8. Workstream 5 — widen `ScriptEngine` keywords

Scheduled commands are dispatched through
`ScriptEngine::executeCommand` (`src/kernel/scheduler/scheduler.cpp:88`
and `:99`), which today recognises only `print`, `gpio`, `wifi`, `delay`,
`system`, `run`, `help`, `#`, and a `.hx` path heuristic
(`src/api/script_engine.cpp:205-237`). Everything else returns
`Unknown command`.

New keywords call the API/filesystem layer rather than duplicating
`main.cpp` handlers:

**Add:** `set`, `heap`, `uptime`, `chip`, `info`, `adc`, `pwd`, `cd`,
`ls`, `mkdir`, `touch`, `rm`, `cp`, `mv`, `cat`, `write`, `append`,
`settings`, `time`, `reboot`, `calc`

**Backing calls already exist:** `harixos::writeText` / `readText` /
`makeDirectory` / `removePath` / `copyFile` / `movePath` / `listDirectory`
/ `touch` / `exists` (`src/kernel/filesystem/filesystem.h`),
`harixos::saveSettings` / `printSettings` (`src/apps/settings/settings.h`),
`ESP.getFreeHeap()`, `analogRead(A0)`, `ESP.restart()`.

**Deferred:** `serve`, `i2c`, `notepad`, `update`, `pull` — entangled with
`main.cpp`'s HTTP and scan code, or meaningless without a human present.

**Known wart, unchanged:** shell `gpio read 2` versus script
`gpio 2 read` remain different. Unification was considered and declined
in this pass.

## 9. Verification

### 9.1 Host unit tests

Add a PlatformIO `native` environment and a host test runner covering the
pure modules, which have no hardware dependency:

**Expression evaluator**
- precedence: `1+2*3 == 7`, `(1+2)*3 == 9`, `2-3-4 == -5`
- unary minus: `-5`, `2 * -3`, `--5`
- comparisons: `1 < 2`, `2 == 2`, `1 != 2`, chained left-associativity
- division by zero -> `ok` true but `isnan`
- stack overflow -> `ok == false`
- pre-expansion: `$name`, undefined `$name` is an error
- value tokens substituted before parsing

**Variable store**
- set/get round trip
- 16 entries accepted, 17th rejected
- name validation: leading digit rejected, `$` rejected, over-length
  rejected, empty rejected
- `get` on an undefined name returns false without writing `out`

**Cron parser and matcher**
- each field syntax: `*`, `n`, `a-b`, `*/n`, `a-b/n`, `1,3,5`
- boundary values: `0 0 * * * *`, `59 59 23 31 12 6`
- out-of-range rejected: `60`, `24`, `32`, `13`, `7`
- AND semantics when both `dom` and `dow` are restricted
- either-or when only one is restricted
- round trip: parse, format, compare
- `*/10 * * * * *` matches seconds 0,10,20,30,40,50 only

**Block frame stack** (if extracted to a testable unit)
- skip-then-else, skip-through-end, nested-if-inside-skipped-branch
- `else` with empty stack, `end` with empty stack, depth overflow
- unclosed block detection

### 9.2 Build gate

After every change: `pio run -e esp8266_generic -e esp01_1m -e nodemcuv2
-e d1_mini` must be 4/4 SUCCESS with no new warnings from `src/`.
Final verification is a clean rebuild (`pio run -t clean` then build).

### 9.3 Serial checklist (hardware)

Run after the build gate, on a real device:

```
set x = 5                     -> x = 5
set y = $x * 2 + 1            -> y = 11
set z = $nope                 -> error: undefined variable
set a = readpin 2             -> a = 0 or 1
calc -5                       -> = -5          (currently fails)
calc 1 + 2                    -> = 3
calc $x + 1                   -> = 6

<hx script with if/else/end>  -> correct branch taken
<hx script with nested if>    -> correct branch taken
<if with bad expression>      -> error reported, treated as false, script continues
<if left unclosed>            -> reported in Script Complete summary

schedule add */5 * * * * * heap -> accepted; fires every 5s
schedule add 0 30 14 * * * reboot -> accepted
schedule add 30 14 * * * reboot   -> rejected, only 5 fields
schedule add 61 * * * * * x   -> rejected, bad field named
schedule list                 -> shows 6-field expression + id + command
schedule remove <id>          -> removed
[reboot]                      -> schedule list unchanged after boot

serve (wifi down, enter SSID/PW) -> credentials saved to settings.cfg
<script `wifi connect`>          -> credentials saved to settings.cfg
[reboot]                         -> auto-connects without re-entering
```

## 10. Out of scope

- `&&` and `||` operators
- `elif`
- `for` / `goto` (`while` / `break` added 2026-10-01)
- `@reboot`
- cron month names (`JAN`), `dow = 7`
- `HH:MM` and `+Ns` sugar (removed, not retained)
- Unifying shell and script `gpio` argument order
- `serve`, `i2c`, `notepad`, `update`, `pull` as script keywords
- Persisting variables across reboot
- Saving WiFi credentials on a failed connect
- Clearing credentials on `wifi disconnect`
- Removing the boot-time WiFi scan in `tryAutoWifi()`
- `WiFi.setAutoReconnect` on the auto-connect path
- JSON — no JSON library exists and `platformio.ini` has no `lib_deps`

## 11. Risks

| Risk | Mitigation |
|---|---|
| Script keyword widening duplicates `main.cpp` handlers and they drift | Call the existing API/filesystem layer; never copy handler bodies |
| Static evaluator workspace could be entered reentrantly later | In-use guard returns an error rather than corrupting state |
| 6-field cron is non-standard; users may paste 5-field crontab lines | Parser rejects with a field count error; docs state the extension |
| AND semantics differ from Linux cron | Documented explicitly in `Documentation/Commands.md` |
| `dom`/`dow` masks and second-level firing interact with the dedup window | Existing dedup already tracks h:m:s; covered by host tests |
| Widening `ScriptEngine` inflates it past the "one file, one purpose" line | Language control flow stays in `script_engine.cpp`; `expr`/`vars` are separate modules |
| Host test env adds a toolchain | `native` env is isolated in `platformio.ini`; the 4 hardware envs are untouched |
