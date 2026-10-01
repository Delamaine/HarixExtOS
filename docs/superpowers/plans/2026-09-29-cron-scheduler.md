# Cron Scheduler Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Scheduled tasks are expressed as 6-field cron, survive a reboot, and can run any script-engine command.

**Architecture:** A new Arduino-free `cron` module parses expressions into bitmasks once and matches them against `struct tm`; `Scheduler` stores `CronSpec` instead of `HH:MM:SS`/`millis()` deadlines, auto-saves to `/harixos/schedule.cfg`, and reloads at boot. `ScriptEngine` gains ~20 keywords that delegate to the existing API and filesystem layers.

**Tech Stack:** C++17, Arduino ESP8266 core 3.30102.0, PlatformIO 6.2.0, PlatformIO `native` 1.2.1 + Unity 2.6.1, MinGW-w64 GCC 16.2.0.

**Spec:** `docs/superpowers/specs/2026-09-29-hx-language-and-cron-design.md` §7, §8.

## Global Constraints

- **Build gate after every task:** `pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini` must be **4/4 SUCCESS**, no new warnings from `src/`.
- **Host test gate after every task touching `cron`:** `pio test -e native` reports 0 failures.
- If `pio` is not on PATH: `C:\Users\delam\AppData\Roaming\Python\Python312\Scripts\pio.exe`.
- **`g++` is not on PATH in a fresh shell.** Prepend the user PATH first:
  `$env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")`
- **No `lib_deps`.** The cron parser is hand-rolled; adding a scheduling library is out of scope (spec §2).
- `[env:native]` stays out of `default_envs`; the four hardware envs are unchanged.
- `build_src_filter` for `native` must **not** include `scheduler.cpp`, `script_engine.cpp`, or anything needing Arduino headers.
- **Field ranges — copy verbatim:** sec 0–59, min 0–59, hour 0–23, dom 1–31, month 1–12, dow 0–6 with **0 = Sunday**. No `JAN` names, no `dow = 7`, no `@reboot`.
- **`dom`/`dow` are AND'ed** (spec §7.2) — this deliberately deviates from standard cron.
- LSP/clangd `'Arduino.h' file not found` errors are environmental noise.
- Branch from `main`. Do not push.

## Review Focus

1. **`schedule add` must not truncate the command at `kMaxTokens = 12`.** Six cron fields plus `schedule add` is already 8 tokens, so the existing `cmd.tokens[3..]` reassembly (`main.cpp:1703-1707`) leaves only 4 tokens for the command and silently drops the rest — the same class of bug fixed for `calc` in commit `e5980d7`. *Pinned in Task 5, Step 5 (serial) — parsing from the raw line is a design requirement, see Task 5 Step 1.*
2. **`dom` AND `dow` semantics.** Standard cron ORs them; users copying crontab expressions will get fewer firings than they expect. The behaviour must be intentional and tested, not accidental. *Pinned in Task 2, Step 2 (`test_and_semantics_when_both_restricted`).*
3. **A command containing spaces must survive the save/load round trip.** The file format gives the command everything after field 6, so `*/5 * * * * reboot now please` must reload verbatim. *Pinned in Task 4, Step 2 (`test_split_line_keeps_command_spaces`).*
4. **One malformed line must not abort loading the rest.** A hand-edited or truncated `schedule.cfg` should lose that entry, not the whole schedule. *Pinned in Task 4, Step 5 (serial) — the load loop touches LittleFS and is not host-reachable.*
5. **Cron never fires without valid wall-clock time.** `time(nullptr)` returns a small value before NTP sync, and today `Scheduler::update` silently does nothing (`scheduler.cpp:71-72`). A user who adds a task and sees no action must be told why. *Pinned in Task 5, Step 6 (serial).*

---

### Task 1: Cron parser

**Files:**
- Create: `src/api/cron.h`, `src/api/cron.cpp`
- Create: `test/test_cron/test_cron.cpp`
- Modify: `platformio.ini` — append `+<api/cron.cpp>` to `build_src_filter`

**Interfaces:**
- Produces (consumed by Tasks 2, 4, 5):
  ```cpp
  namespace harixos { namespace api { namespace cron {
  struct Spec {
    uint64_t sec;         // bit n = second n
    uint64_t minute;      // bit n = minute n
    uint32_t hour;        // bit n = hour n
    uint32_t dom;         // bit n = day-of-month n (bits 1..31)
    uint16_t month;       // bit n = month n (bits 1..12)
    uint8_t  dow;         // bit n = weekday n (bits 0..6, 0 = Sunday)
    bool domRestricted;   // true when the dom field was not '*'
    bool dowRestricted;
  };
  // Returns nullptr on success, else a static message.
  // *badField receives the 0-based failing field index, or -1 if the
  // failure is not field-specific (wrong count).
  const char *parse(const char *expression, Spec &out, int *badField);
  }}}}
  ```
  Static error messages: `"expected 6 fields"`, `"unsupported syntax"`, `"value out of range"`, `"step must be >= 1"`, `"reversed range"`.
- Consumes: nothing. No Arduino headers, no `String` — `struct tm` is not needed until Task 2.

- [ ] **Step 1: Branch and write the failing tests**

Create branch `feat/cron-scheduler` from `main`, then create `test/test_cron/test_cron.cpp`:

| Test name | Input | Assertion |
|---|---|---|
| `test_parse_all_stars` | `* * * * * *` | nullptr; every sec/min bit set, all hour/dom/month/dow bits set, both `*Restricted` false |
| `test_parse_single_second` | `30 * * * * *` | only bit 30 of `sec` set |
| `test_parse_range` | `0-10 * * * * *` | `sec` bits 0..10 set, 11 clear |
| `test_parse_step_from_star_sec` | `*/10 * * * * *` | `sec` bits 0,10,20,30,40,50 set |
| `test_parse_step_from_range` | `0-30/10 * * * * *` | `sec` bits 0,10,20,30 set |
| `test_parse_list` | `1,3,5 * * * * *` | `sec` bits 1,3,5 set |
| `test_parse_list_of_ranges` | `0-2,5 * * * * *` | `sec` bits 0,1,2,5 set |
| `test_parse_dom_step_starts_at_one` | `* * * */2 * *` | `dom` bits 1,3,5,…,31 set; bit 0 unset — **spec §7.1 edge rule** |
| `test_parse_dow_step_starts_at_zero` | `* * * * * */2` | `dow` bits 0,2,4,6 set |
| `test_parse_dow_zero_is_sunday` | `* * * * * 0` | `dow` bit 0 set |
| `test_parse_dow_six_is_saturday` | `* * * * * 6` | `dow` bit 6 set |
| `test_parse_rejects_five_fields` | `* * * * *` | message `"expected 6 fields"`, `*badField == -1` |
| `test_parse_rejects_seven_fields` | `* * * * * * *` | `"expected 6 fields"`, `-1` |
| `test_parse_rejects_second_60` | `60 * * * * *` | `"value out of range"`, `*badField == 0` |
| `test_parse_rejects_hour_24` | `* * 24 * * *` | `"value out of range"`, `*badField == 2` |
| `test_parse_rejects_dom_32` | `* * * 32 * *` | `"value out of range"`, `*badField == 3` |
| `test_parse_rejects_month_13` | `* * * * 13 *` | `"value out of range"`, `*badField == 4` |
| `test_parse_rejects_dow_7` | `* * * * * 7` | `"value out of range"`, `*badField == 5` |
| `test_parse_rejects_reversed_range` | `10-5 * * * * *` | `"reversed range"`, `*badField == 0` |
| `test_parse_rejects_zero_step` | `*/0 * * * * *` | `"step must be >= 1"`, `*badField == 0` |
| `test_parse_rejects_garbage` | `foo * * * * *` | `"unsupported syntax"`, `*badField == 0` |
| `test_parse_sets_dom_restricted` | `* * * 15 * *` | `domRestricted == true`, `dowRestricted == false` |
| `test_parse_sets_dow_restricted` | `* * * * * 1` | `dowRestricted == true` |
| `test_parse_accepts_extra_whitespace` | `  */5   0 9  * * 1 ` | nullptr, parses correctly |

- [ ] **Step 2: Run tests to verify they fail**

Refresh PATH, then:
```
pio test -e native
```
Expected: **FAIL** — `cron.h: No such file or directory`.

- [ ] **Step 3: Implement `cron::parse`**

Split on whitespace expecting exactly 6 tokens (fewer or more → `"expected 6 fields"`). For each token, split on `,` into ranges; each range is `*`, `n`, `a-b`, optionally followed by `/step`. Expand into the corresponding mask using each field's own minimum: sec/min/hour/dow start at 0, dom and month start at 1.

Set `domRestricted` / `dowRestricted` when the corresponding token is anything other than a bare `*`. Reject `a > b` and `step < 1` before writing any bits, so a failed parse never leaves `out` half-populated — the caller must be able to treat `out` as undefined on failure without clearing it.

- [ ] **Step 4: Run tests to verify they pass**

```
pio test -e native
```
Expected: `24 test cases: 24 succeeded`.

- [ ] **Step 5: Build firmware**

```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS.**

- [ ] **Step 6: Commit**

```bash
git add src/api/cron.h src/api/cron.cpp test/test_cron/test_cron.cpp platformio.ini
git commit -m "feat(cron): parse 6-field cron expressions into bitmasks

Hand-rolled: platformio.ini has no lib_deps and the 1 MB ESP-01 target
cannot afford a scheduling library. Masks are built here once rather
than re-parsing every scheduler tick."
```

---

### Task 2: Cron matcher

**Files:**
- Modify: `src/api/cron.h`, `src/api/cron.cpp`
- Modify: `test/test_cron/test_cron.cpp`

**Interfaces:**
- Produces (consumed by Task 5):
  ```cpp
  // timeinfo comes from localtime_r(). Returns true on a match.
  bool matches(const Spec &spec, const struct tm &timeinfo);
  ```
- Consumes: `Spec` from Task 1. Requires `<time.h>` for `struct tm`, available on both host and target.

- [ ] **Step 1: Write the failing tests**

Build a `struct tm` helper in the test file that fills every field from a `(sec, min, hour, dom, month, dow)` tuple so each test states its clock explicitly.

| Test name | Spec | Clock | Expected |
|---|---|---|---|
| `test_matches_every_second` | `* * * * * *` | any | true |
| `test_matches_exact_second` | `30 * * * * *` | sec 30 | true |
| `test_rejects_wrong_second` | `30 * * * * *` | sec 31 | false |
| `test_matches_second_list` | `0,15,30,45 * * * * *` | sec 15 | true |
| `test_rejects_hour_mismatch` | `0 0 9 * * *` | 09:00:01 vs 10:00:00 | false / false |
| `test_rejects_month_mismatch` | `0 0 9 1 * 3` | month 4, dom 1, dow 3 | false |
| `test_only_dom_restricted_matches_any_dow` | `0 0 9 15 * *` | dom 15, dow 4 | true |
| `test_only_dow_restricted_matches_any_dom` | `0 0 9 * * 1` | dom 20, dow 1 | true |
| `test_and_semantics_when_both_restricted` | `0 0 9 * * 1` | dom 15 (a Monday), dow 1 | **true** |
| `test_and_semantics_rejects_when_only_dow_matches` | `0 0 9 * * 1` | dom 16 (a Tuesday), dow 2 | **false** |
| `test_and_semantics_rejects_when_only_dom_matches` | `0 0 9 * * 1` | dom 15 but dow 4 | **false** |

`test_and_semantics_*` is **Review Focus 2** — these three together are the whole of the deviation from standard cron, and must be present even if they feel redundant.

- [ ] **Step 2: Run tests to verify they fail**

```
pio test -e native
```
Expected: **FAIL** — `matches` undefined.

- [ ] **Step 3: Implement `cron::matches`**

Bit-test `spec.sec` against `timeinfo.tm_sec`, `spec.minute` against `tm_min`, `spec.hour` against `tm_hour`, `spec.month` against `tm_mon + 1`, `spec.dow` against `tm_wday`.

Day handling carries the semantics:
- If `!domRestricted && !dowRestricted` → day matches.
- Else compute `domMatch = domRestricted && bit(test.dom)` and `dowMatch = dowRestricted && bit(test.dow)`; the day matches when `domMatch && dowMatch`.

Note this yields `true` for the unrestricted side of a partially-restricted pair (a bare `*` restricts nothing, so it always "matches"), which is what the three `and_semantics` tests assert.

- [ ] **Step 4: Run tests to verify they pass**

```
pio test -e native
```
Expected: `35 test cases: 35 succeeded` (24 + 11).

- [ ] **Step 5: Build firmware**

```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS.**

- [ ] **Step 6: Commit**

```bash
git add src/api/cron.h src/api/cron.cpp test/test_cron/test_cron.cpp
git commit -m "feat(cron): match Spec against struct tm

dom and dow are AND'ed rather than OR'ed as in standard cron — the
spec pins predictability over crontab interoperability, so both
restricted fields must match."
```

---

### Task 3: Restructure `ScheduledTask`

**Files:**
- Modify: `src/kernel/scheduler/scheduler.h` (the `ScheduledTask` struct at `:13-21` and the member declarations at `:48-55`)
- Modify: `src/kernel/scheduler/scheduler.cpp` (constructors and `update`)

**Interfaces:**
- Consumes: `cron::Spec`, `cron::parse` from Task 1; `cron::matches` from Task 2.
- Produces (consumed by Tasks 4 and 5):
  ```cpp
  struct ScheduledTask {
    int id;
    cron::Spec spec;
    String expression;   // original 6-field text, for list and save
    String command;
  };
  // Scheduler gains, alongside add/remove/list/update:
  bool save();
  bool load();
  ```

Delete `enum TaskType`, `hour`, `minute`, `second`, and `executeAtMillis` — with cron-only scheduling there is no `TASK_ONCE` left (spec §3, §7.6).

- [ ] **Step 1: Rewrite the struct and constructors**

Replace the two constructors' bodies in `scheduler.cpp:13-37`. `addDailyTask(hh,mm,ss,cmd)` and `addOnceTask(delayMs,cmd)` are both removed; they are replaced in Task 5 by a single `int add(const char *expression, const String &command)` that returns the new id or `-1` when full.

Add `bool save();` and `bool load();` declarations now (implemented in Task 4) so this task's build stays green.

- [ ] **Step 2: Rewrite `update()`**

Replace the `hour`/`minute`/`second` comparison at `scheduler.cpp:86-90` and the `millis()` comparison at `:96-103` with a single loop calling `cron::matches(tasks[i].spec, timeinfo)`.

Keep `MAX_TASKS = 20` and the existing per-second dedup at `:74-81` — it already tracks hour, minute, and second, so second-level cron is covered without change.

Keep the `now >= 1000000000` guard at `:71-72` unchanged in this task; printing a warning from it is Task 5's responsibility.

Task self-deletion on fire: with no `TASK_ONCE` there is no self-deleting task, so remove that branch (`:96-103`). A matching task now stays registered and fires again on its next match — which is what cron means.

- [ ] **Step 3: Build firmware**

```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS.** `handleSchedule` in `main.cpp` still calls the removed constructors at this point — if it fails to link, that is expected and means Step 4 cannot be skipped.

- [ ] **Step 4: Update `handleSchedule` call sites minimally**

Until Task 5 rewrites the shell syntax, `main.cpp:1686-1754` must at least compile. Replace the `HH:MM` and `+Ns` branches with a single path that passes the raw command remainder through to `Scheduler::add`, deferring all syntax validation to Task 5. Do not expand scope here.

- [ ] **Step 5: Build and confirm no regression**

```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS.**

- [ ] **Step 6: Commit**

```bash
git add src/kernel/scheduler/scheduler.h src/kernel/scheduler/scheduler.cpp src/main.cpp
git commit -m "refactor(scheduler): store CronSpec instead of HH:MM and millis deadlines

TASK_ONCE deadlines were millis()-relative and therefore meaningless
after a reboot; cron has no equivalent, so the type is removed rather
than serialized."
```

---

### Task 4: Persistence

**Files:**
- Modify: `src/api/cron.h`, `src/api/cron.cpp` (add `splitLine`)
- Modify: `src/kernel/scheduler/scheduler.cpp` (implement `save`/`load`)
- Modify: `src/main.cpp` (call `systemScheduler.load()` in `setup()`)

**Interfaces:**
- Produces (consumed by Task 5):
  ```cpp
  // Splits "<6 cron fields> <command...>" into its two halves.
  // cronOut receives the first six whitespace-separated fields.
  // *commandOut points into `line` at the text after field 6.
  // Returns nullptr on success, else "expected 6 fields" / "missing command".
  const char *splitLine(const char *line, char *cronOut, size_t cronCap,
                        const char **commandOut);

  // The exact inverse, used by save() so the line format has one definition.
  // Writes "<expression> <command>" plus a trailing '\n' as load() expects.
  // Returns nullptr, or "line too long" if it would not fit in cap.
  const char *makeLine(const char *expression, const char *command,
                       char *out, size_t cap);
  ```
- Consumes: `cron::parse` (Task 1), `harixos::writeText` / `readText` / `exists` (`src/kernel/filesystem/filesystem.h`), `ScheduledTask` (Task 3).

**ID rule (removes a spec ambiguity):** the file contains no id column — format is strictly `<cron> <command>` per spec §7.5. On load, ids are assigned as `1..N` in file order, and `nextId = count + 1`. A reboot with an unchanged file therefore reproduces identical ids, which satisfies spec §7.5's "ids stay stable across reboots"; ids are only reshuffled by a `schedule remove`, not by a power cycle.

- [ ] **Step 1: Write the failing tests**

| Test name | Input | Assertion |
|---|---|---|
| `test_split_line_typical` | `*/5 * * * * * reboot` | `cronOut == "*/5 * * * * *"`, `*commandOut == "reboot"`, returns nullptr |
| `test_split_line_keeps_command_spaces` | `0 30 14 * * * settings save now` | `*commandOut == "settings save now"` — **Review Focus 3** |
| `test_split_line_exact_six_fields_no_command` | `* * * * * *` | returns `"missing command"` |
| `test_split_line_five_fields` | `* * * * *` | returns `"expected 6 fields"` |
| `test_split_line_extra_internal_whitespace` | `*/5   * *  * *   heap` | `cronOut` normalises to `*/5 * * * * *`, command `heap` |
| `test_make_line_round_trips` | `makeLine("*/5 * * * * *", "settings save now", ...)` then `splitLine` then `cron::parse` | line equals `*/5 * * * * * settings save now\n`; split recovers command `settings save now`; parsed `Spec` field-by-field equals `parse("*/5 * * * * *")` — **spec §9.1 round trip, the exact seam a reboot crosses** |

Note `splitLine` takes a line whose **first six fields are the cron expression**, so an every-five-seconds entry is written `*/5 * * * * * reboot` — seven tokens total before the command. `test_split_line_typical` is the one that pins this.

- [ ] **Step 2: Run tests to verify they fail**

```
pio test -e native
```
Expected: **FAIL** — `splitLine` undefined.

- [ ] **Step 3: Implement `splitLine`, `makeLine`, then `Scheduler::save` and `Scheduler::load`**

`splitLine` writes at most `cronCap` bytes including the NUL and points `commandOut` into the caller's buffer after skipping whitespace. It normalises runs of internal whitespace between the six fields to single spaces, so `cronOut` is canonical.

`makeLine` writes `expression`, a single space, `command`, and a trailing `'\n'`, returning `"line too long"` rather than truncating.

`save()` builds content by looping over `tasks[i]` and calling `makeLine(tasks[i].expression.c_str(), tasks[i].command.c_str(), scratch, sizeof(scratch))` into a local `char scratch[192]`, appending each to one `String`, then calls `harixos::writeText("/harixos/schedule.cfg", content.c_str(), false)`. Returns false only if `writeText` fails. Declare `constexpr size_t kMaxSaveLine = 192;` in `cron.h` — `scheduler.cpp` cannot see `main.cpp`'s anonymous-namespace `kMaxLineLength`, so the save line budget must live with the pure module. **Do not inline the concatenation** — `makeLine` existing is what makes `test_make_line_round_trips` a real test of the save/load seam rather than a test of a duplicated expression.

`load()` reads the file with `harixos::readText`; if `harixos::exists()` is false it returns true silently (mirroring `settings.cpp:14-16`). Otherwise it splits on `'\n'`, and for each non-empty line calls `splitLine`, then `cron::parse`, then appends. **A failure in either call skips that line with a `Serial.printf` warning and continues** — this is Review Focus 4. Finally it sets `nextId = taskCount + 1`.

- [ ] **Step 4: Run host tests to verify they pass**

```
pio test -e native
```
Expected: `41 test cases: 41 succeeded` (35 + 6).

- [ ] **Step 5: Wire the load and prove the malformed-line tolerance**

Add `systemScheduler.load();` to `setup()` immediately after `shellSettings = harixos::loadSettings();` (`src/main.cpp:2087`) so both reads happen after `LittleFS.begin()`.

Build:
```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS.**

Serial — **Review Focus 4**: with two valid tasks saved, hand-edit `/harixos/settings.cfg`'s sibling using `write /harixos/schedule.cfg` to insert a garbage line (`this is not cron`) between them, then reboot. Expected: a warning naming the bad line, **both** valid tasks present in `schedule list`, `nextId` continuing past them.

- [ ] **Step 6: Commit**

```bash
git add src/api/cron.h src/api/cron.cpp src/kernel/scheduler/scheduler.cpp src/main.cpp test/test_cron/test_cron.cpp
git commit -m "feat(scheduler): persist tasks to /harixos/schedule.cfg

Line format is '<6 cron fields> <command>' — the command owns everything
after field 6 so it may contain spaces. One bad line is skipped with a
warning rather than aborting the load."
```

---

### Task 5: Shell syntax, auto-save, and time warning

**Files:**
- Modify: `src/main.cpp` (`handleSchedule` at `:1686-1754`, help text at `:1860-1871`, `setup()` for the time warning)
- Modify: `src/kernel/scheduler/scheduler.cpp` (`update()` warning)

**Interfaces:**
- Consumes: everything from Tasks 1–4.
- Produces: the `schedule` command surface as specified — **6-field cron only**. `HH:MM` and `+Ns` are removed with no sugar (spec §7.7).

- [ ] **Step 1: Parse from the raw line, not `TokenizedLine`**

This is **Review Focus 1** and cannot be done through `cmd.tokens`.

`kMaxTokens` is 12 (`src/main.cpp:35`). `schedule add` plus six cron fields consumes 8, leaving 4 tokens for the command — and `main.cpp:1703-1707` currently rejoins `tokens[3..count]`, silently truncating at the cap.

Rewrite `handleSchedule` to take the raw `line` (change the call site at `main.cpp:1957` from `handleSchedule(cmd)` to `handleSchedule(line)`, matching how `handleCalc` already receives the raw line after commit `e5980d7`). Strip the `schedule add ` prefix, then feed the remainder to `cron::splitLine`.

- [ ] **Step 2: Implement the subcommands**

| Command | Behaviour |
|---|---|
| `schedule` / `schedule list` | `systemScheduler.listTasks(Serial)`, printing id, the 6-field expression, and the command |
| `schedule add <6 fields> <command>` | `splitLine` → `cron::parse`; on failure print the message plus `field N`; on success `Scheduler::add(...)`; if it returns `-1` print `Scheduler full.`; **then `save()`** |
| `schedule remove <id>` | `removeTask(id)`; **then `save()`** |

Both mutating paths auto-save immediately, mirroring `settings banner/update/timezone` (`src/main.cpp:627,646,660`). There is no `schedule save` command.

Rewrite the help block at `main.cpp:1860-1871` with 6-field examples and remove every `HH:MM` and `+Ns` reference.

- [ ] **Step 3: Build**

```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS.**

- [ ] **Step 4: Serial — rejection cases**

```
schedule add 61 * * * * * heap        -> error naming field 0, not registered
schedule add * * 24 * * * heap        -> error naming field 2
schedule add * * * * * *             -> missing command error
schedule add */5 * * * *             -> expected 6 fields error
schedule list                         -> empty / unchanged
```

- [ ] **Step 5: Serial — Review Focus 1, command truncation**

```
schedule add * * * * * * reboot and then some extra words here to exceed the token cap
schedule list
```
Expected: the full command `reboot and then some extra words here to exceed the token cap` is stored and displayed intact. If only the first four words survive, this task has failed — fall back to raw-line parsing and re-run.

Then confirm persistence:
```
schedule add 0 30 14 * * * settings save
schedule list                         -> 2 tasks
[reboot]
schedule list                         -> still 2 tasks, ids unchanged (1 and 2)
cat /harixos/schedule.cfg             -> 2 lines, each '<cron> <command>'
```

- [ ] **Step 6: Serial — Review Focus 2 and 5**

**AND semantics:** `schedule add 0 0 9 15 * 1 ping` on a device with correct time, then observe over the relevant days that it fires only when the date is the 15th **and** the weekday is Monday — not on either condition alone. (`dom` must be restricted or the observation is undecidable: a bare `*` matches every date, so `0 0 9 * * 1` fires on every Monday.)

**Time warning — Review Focus 5:** before NTP has synced, add a matching task. Expected: `systemScheduler.update()` prints `[Scheduler] no valid system time, cron not running` at most once per boot, instead of silently doing nothing. After `time sync`, confirm tasks begin firing.

- [ ] **Step 7: Serial — a failed scheduled command reports an error**

```
schedule add * * * * * * cat /nope
```
Expected: when it fires, `[Scheduler] Executing task #N: cat /nope` is followed by `[ERROR] cat /nope: cat: file not found` and then the prompt — not by silence. "Produced no output" and "failed" must be distinguishable, or every scheduled `cat`/`rm`/`settings` failure reads as a broken scheduler.

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp src/kernel/scheduler/scheduler.cpp
git commit -m "feat(scheduler): 6-field cron command surface with auto-save

Parses the raw line rather than TokenizedLine — schedule add plus six
fields already consumes 8 of kMaxTokens=12, so token-based reassembly
would silently truncate the command. HH:MM and +Ns sugar removed."
```

---

### Task 6: Widen the script keyword list

**Files:**
- Modify: `src/api/script_engine.cpp` (dispatch chain in `executeCommand` at `:205-237`, plus new private handlers)
- Modify: `src/api/script_engine.h` (private handler declarations)

**Interfaces:**
- Consumes: `harixos::writeText` / `readText` / `makeDirectory` / `removePath` / `copyFile` / `movePath` / `listDirectory` / `touch` / `exists` (`src/kernel/filesystem/filesystem.h`); `harixos::saveSettings` / `printSettings` / `shellSettings` (`src/apps/settings/settings.h`); `GpioAPI`, `SystemAPI`, `WiFiAPI` (already used by `script_engine.cpp`); `ESP.getFreeHeap()`, `analogRead(A0)`, `ESP.restart()`.
- Produces: the widened keyword set below, available both to `.hx` scripts and to `schedule add`.

**Add these 20 keywords:** `heap`, `uptime`, `chip`, `info`, `adc`, `calc`, `pwd`, `cd`, `ls`, `mkdir`, `touch`, `rm`, `cp`, `mv`, `cat`, `write`, `append`, `settings`, `time`, `reboot`. (`set` is already wired by Plan B Task 6, which runs first — do **not** add a second `set` branch; a duplicate `else if (cmd.name == "set")` compiles fine and silently shadows nothing, but it is dead code and review noise.)

**Do not add:** `serve`, `i2c`, `notepad`, `update`, `pull` (spec §8).

**Rule:** each handler delegates to the API/filesystem layer. **Never copy a handler body out of `main.cpp`** — duplicated logic will drift, and `main.cpp`'s `executeCommand` is already a 30 KB function.

- [ ] **Step 1: Group the handlers**

Add private declarations to `script_engine.h` and definitions to `script_engine.cpp`, grouped by what they delegate to:

| Group | Keywords | Delegate to |
|---|---|---|
| System values | `heap`, `uptime`, `chip`, `info`, `adc`, `calc` | `ESP.getFreeHeap()`, `SystemAPI::*`, `analogRead(A0)`, `expr::evaluate` |
| Filesystem | `pwd`, `cd`, `ls`, `mkdir`, `touch`, `rm`, `cp`, `mv`, `cat`, `write`, `append` | `harixos::*` filesystem helpers + a `currentWorkingDirectory` accessor |
| Settings & time | `settings`, `time`, `reboot` | `harixos::saveSettings`/`printSettings`, `configTime`/`time(nullptr)`, `ESP.restart()` |

`pwd`/`cd` need the working directory: `currentWorkingDirectory` currently lives in `main.cpp`'s anonymous namespace (`main.cpp:41`) with the same internal-linkage problem `shellSettings` had. Move it into `src/kernel/filesystem/filesystem.{h,cpp}` as `extern String currentWorkingDirectory;` — same shape as Plan A's Task 1.

- [ ] **Step 2: Add the dispatch branches**

Extend the `if/else if` chain in `ScriptEngine::executeCommand` with one branch per new keyword, matching the existing style at `script_engine.cpp:214-236`.

Update `ScriptEngine::printHelp` (`script_engine.h:27`) to list them, and update the `.hx` example header comment at `script_engine.cpp:10-16`.

- [ ] **Step 3: Build**

```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS**, and **no new warnings**. A duplicated handler that shadows a `main.cpp` one will usually still compile — the warning check is what catches accidental divergence.

- [ ] **Step 4: Serial — every new keyword inside a `.hx`**

Install a script exercising each group and run it:

```
print --- system ---
heap
uptime
chip
info
adc
calc 1 + 2 * 3                -> = 7

print --- fs ---
mkdir /tmpdemo
write /tmpdemo/a.txt hello
cat /tmpdemo/a.txt             -> hello
append /tmpdemo/a.txt world
cat /tmpdemo/a.txt             -> helloworld
ls /tmpdemo
cp /tmpdemo/a.txt /tmpdemo/b.txt
mv /tmpdemo/b.txt /tmpdemo/c.txt
rm /tmpdemo/c.txt
pwd

print --- settings/time ---
settings show
time
reboot                         -> run LAST; device restarts
```

Any `Unknown command:` line is a missing dispatch branch and fails this task.

- [ ] **Step 5: Serial — the reason this task exists**

```
schedule add */5 * * * * * heap
schedule add 0 0 9 * * * settings show
schedule add */30 * * * * * adc
```
Expected: all three accepted, and each fires on schedule producing real output. Before this task all three returned `Unknown command`.

- [ ] Re-run every `schedule add` example in `Documentation/Commands.md`, `SCRIPT-REFERENCE.md`, and spec §9.3 as written and confirm each is accepted (a 5-field example is rejected as `expected 6 fields`, which is how the defect fixed in review was caught).

- [ ] **Step 6: Confirm the declined unification was honoured**

Both spellings still work, unchanged: shell `gpio read 2` and script `gpio 2 read`. Do not touch them — spec §8 records this as a known wart accepted for this pass.

- [ ] **Step 7: Commit**

```bash
git add src/api/script_engine.h src/api/script_engine.cpp src/kernel/filesystem/filesystem.h src/kernel/filesystem/filesystem.cpp src/main.cpp
git commit -m "feat(script): widen keyword list so cron can schedule real commands

Each handler delegates to the API or filesystem layer rather than
copying main.cpp bodies, which would drift. currentWorkingDirectory
moves out of main.cpp's anonymous namespace for the same reason
shellSettings did."
```

---

### Task 7: Documentation and final verification

**Files:**
- Modify: `Documentation/Commands.md` (the `schedule` section)
- Modify: `SCRIPT-REFERENCE.md` (scheduled-command examples, available keywords)

- [ ] **Step 1: Rewrite the schedule documentation**

Replace every `HH:MM` and `+Ns` example with 6-field cron. Document: the field order and ranges, **dow 0 = Sunday**, per-field syntax (`*`, `n`, `a-b`, `*/n`, `a-b/n`, lists), the **AND** semantics of `dom`/`dow` with an explicit callout that this differs from Linux cron, `*/n` starting at each field's minimum, the requirement for valid system time, and auto-save with no `schedule save` command.

Update the script keyword list in `SCRIPT-REFERENCE.md` to the full set from spec §8 — the 20 this plan adds plus `set` from Plan B, for 21 total.

- [ ] **Step 2: Clean rebuild and record sizes**

```
pio run -t clean -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS**, no `error:` and no `warning:` from `src/`. Record `RAM:` and `Flash:`. Versus the pre-plan baseline of `35936 B` / `510115 B`, expect roughly +1.5 KB RAM (cron masks, wider `ScheduledTask`) and +8–15 KB flash. On `esp01_1m` the app must stay comfortably under 761,840 B — spec §2's budget says around 69% used.

- [ ] **Step 3: Full host test run**

```
pio test -e native
```
Expected: all suites pass, 0 failures — 41 `cron` cases (24 `parse` + 11 `matches` + 6 `splitLine`/`makeLine`) plus whatever Plan B's suites total.

- [ ] **Step 4: Full serial checklist**

Run every numbered item from spec §9.3 concerning `schedule` in one session against the final build, plus Tasks 5–6's own lists: rejection cases, the token-truncation probe, reboot persistence, AND semantics, the no-time warning, and every widened keyword.

- [ ] **Step 5: Confirm constraints held**

```
grep -n "lib_deps" platformio.ini
grep -n "HH:MM\|schedule add +\|addDailyTask\|addOnceTask\|TASK_DAILY\|TASK_ONCE" -r src Documentation SCRIPT-REFERENCE.md
git status --short
git log --oneline main..HEAD
```
Expected: no `lib_deps`; the second grep returns **nothing** (no surviving references to the removed syntax or types); clean tree; six commits.

- [ ] **Step 6: Commit**

```bash
git add Documentation/Commands.md SCRIPT-REFERENCE.md
git commit -m "docs(cron): move schedule syntax to 6-field cron with AND dom/dow

HH:MM and +Ns are removed entirely rather than kept alongside — two
syntaxes would let the same file mean different things on two firmwares."
```

Total: **7 commits** on the branch.
