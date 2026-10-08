# `info` Extension + Crash Log Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Capture why the previous run ended (reset reason + exception registers) into a persistent capped log at boot, surface it plus FS usage and synced time through the existing `info` command, and print a warning line at boot only when the previous reset was abnormal.

**Architecture:** Two layers. `kernel/crash_log.{h,cpp}` is pure, host-testable logic (abnormal classification, line formatting, boot-count parsing, append-with-cap) with no Arduino dependencies — same pattern as `relay_logic`/`sensor_logic`. `main.cpp` is the only device glue: read `ESP.getResetInfoPtr()` + `/crash.log` right after LittleFS mounts, rewrite the file, and extend `handleInfo()` reusing the already-existing `printSystemInfo()`/`printWifiStatus()`/`HttpDownloader::getStorageInfo()`/`readText()` helpers.

**Tech Stack:** C++ (ESP8266 Arduino core + native g++), Unity tests via PlatformIO `native` env, LittleFS.

**Spec:** Bounded path — in-chat design approved 2026-10-08 (no spec file). Design decisions recorded here: extend `info` (NOT a new `sysinfo` command — `info` already exists at main.cpp:369); crash log at `/crash.log`, 8-line cap, `reason|exccause|epc1|excvaddr|depc|boots=N` format; boot prints the line only if previous reset was abnormal; full history readable via existing `cat /crash.log`.

## Global Constraints

- Device build gate: `python -m platformio run -e nodemcuv2` → `SUCCESS`. Native test gate: `python -m platformio test -e native` → all suites PASSED (baseline **170/170**; after Task 1 → **177/177**).
- `kernel/crash_log.{h,cpp}` must compile on host: includes limited to `<stdint.h>`, `<stdio.h>`, `<string.h>`, `<inttypes.h>` — no `Arduino.h`, no `String`, no `user_interface.h`. It is added to `[env:native] build_src_filter` (platformio.ini:68).
- Format strings use `PRIu32`/`PRIX32` macros for `uint32_t` fields (plain `%lu`/`%lx` breaks on host — `uint32_t` is `unsigned int` there).
- Constants: `kCrashLineMax = 96`, `kCrashMaxLines = 8`, `kCrashLogMax = 768` (8 × 96), defined `constexpr` in `crash_log.h`.
- Crash-log path: `/crash.log` (root level, like `/sensors.conf`). Any `writeText` failure during boot capture is non-fatal — boot continues.
- LSP/clangd diagnostics in this repo (`Arduino.h not found`, undeclared `Serial`/`String`) are pre-existing noise; the two PlatformIO gates above are the truth.
- Commit once per task, message style `feat(scope): ...`.

## Review Focus

1. **Crash loop trims oldest, keeps newest, and boot count still increments** — after 9 boots the file holds boots 2–9 (8 lines) and the next append counts 10. Test: `test_append_caps_to_eight` + `test_boot_count` (last `boots=` wins) in Task 1.
2. **Format portability** — the exact line string must render identically on host and xtensa (uppercase hex, no `%lx` mismatch warnings). Test: `test_format_line_exact` (Task 1) + nodemcuv2 build (Task 2).
3. **First flash / missing file** — `/crash.log` absent must produce `boots=1` and create the file, not crash or skip. Test: `test_boot_count` empty-input case (Task 1) + device verify step (Task 2).
4. **Normal resets stay silent** — power-on (0), software restart (4), deep-sleep wake (5), external reset (6) must NOT print the "Previous run ended" line at boot; only watchdog/exception (1/2/3) may. Test: `test_abnormal_classification` (Task 1) + device verify (normal `reboot` is silent, Task 2).
5. **Boot capture never blocks boot** — missing file (`readText` empty), corrupted content, or `writeText` failure must leave setup() running normally. Code rule in Task 2 Step 1 (no early return/abort on any failure) + device verify (device reaches prompt with log deleted).

---

### Task 1: `crash_log` pure core (host-tested)

**Files:**
- Create: `src/kernel/crash_log.h`, `src/kernel/crash_log.cpp`
- Create: `test/test_crashlog/test_crashlog.cpp`
- Modify: `platformio.ini:68` (`[env:native] build_src_filter` — append `+<kernel/crash_log.cpp>`)

**Interfaces:**
- Consumes: nothing (pure C).
- Produces (namespace `harixos`, used by Task 2):
  - `bool crashReasonIsAbnormal(uint32_t reasonCode)` — true for 1 (WDT), 2 (exception), 3 (soft WDT); false for 0/4/5/6.
  - `int crashBootCount(const char* existing)` — boots value of the **last** `boots=N` in `existing`; 0 if absent/corrupt.
  - `int crashFormatLine(char* out, size_t n, const char* reasonStr, uint32_t exccause, uint32_t epc1, uint32_t excvaddr, uint32_t depc, int boots)` — writes `reason|<exccause decimal>|0x<epc1>|0x<excvaddr>|0x<depc>|boots=N` (hex uppercase); returns bytes written or **-1 if truncated**.
  - `int crashAppendLine(char* out, size_t n, const char* existing, const char* line)` — appends `line` as a new `\n`-terminated line, drops oldest lines beyond `kCrashMaxLines`; returns new line count, or **-1 if result exceeds `n`** (then `out[0] = '\0'`).
  - Constants `kCrashLineMax`, `kCrashMaxLines`, `kCrashLogMax`.

- [ ] **Step 1: Write the failing test**

Create `test/test_crashlog/test_crashlog.cpp` (Unity, `main()` pattern copied from `test/test_sensor/test_sensor.cpp`):

```cpp
#include <unity.h>
#include <string.h>
#include "kernel/crash_log.h"

using namespace harixos;

static void test_format_line_exact(void) {
  char b[kCrashLineMax];
  int r = crashFormatLine(b, sizeof(b), "Exception", 2, 0x4020abcd,
                          0x3ffffffe, 0x0, 5);
  const char* want = "Exception|2|0x4020ABCD|0x3FFFFFFE|0x0|boots=5";
  TEST_ASSERT_EQUAL_INT((int)strlen(want), r);
  TEST_ASSERT_EQUAL_STRING(want, b);
}

static void test_format_line_too_small(void) {
  char b[8];
  TEST_ASSERT_EQUAL_INT(-1, crashFormatLine(b, sizeof(b), "Exception", 2,
                                            0x4020abcd, 0x3ffffffe, 0x0, 5));
}

static void test_abnormal_classification(void) {
  TEST_ASSERT_TRUE(crashReasonIsAbnormal(1));   // hardware WDT
  TEST_ASSERT_TRUE(crashReasonIsAbnormal(2));   // exception
  TEST_ASSERT_TRUE(crashReasonIsAbnormal(3));   // software WDT
  TEST_ASSERT_FALSE(crashReasonIsAbnormal(0));  // power-on
  TEST_ASSERT_FALSE(crashReasonIsAbnormal(4));  // software restart
  TEST_ASSERT_FALSE(crashReasonIsAbnormal(5));  // deep-sleep wake
  TEST_ASSERT_FALSE(crashReasonIsAbnormal(6));  // external reset
}

static void test_boot_count(void) {
  TEST_ASSERT_EQUAL_INT(0, crashBootCount(""));
  TEST_ASSERT_EQUAL_INT(0, crashBootCount("garbage no counter\n"));
  TEST_ASSERT_EQUAL_INT(7, crashBootCount("Exception|2|0x0|0x0|0x0|boots=7\n"));
  // last line wins
  TEST_ASSERT_EQUAL_INT(9, crashBootCount(
      "Exception|2|0x0|0x0|0x0|boots=7\n"
      "Power on|0|0x0|0x0|0x0|boots=9\n"));
}

static void test_append_empty(void) {
  char out[kCrashLogMax];
  int n = crashAppendLine(out, sizeof(out), "", "L1");
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_STRING("L1\n", out);
}

static void test_append_caps_to_eight(void) {
  char out[kCrashLogMax];
  int n = crashAppendLine(out, sizeof(out),
      "L1\nL2\nL3\nL4\nL5\nL6\nL7\nL8\n", "L9");
  TEST_ASSERT_EQUAL_INT(8, n);
  TEST_ASSERT_EQUAL_STRING("L2\nL3\nL4\nL5\nL6\nL7\nL8\nL9\n", out);
}

static void test_append_overflow(void) {
  char out[8];
  out[0] = 'x';
  TEST_ASSERT_EQUAL_INT(-1, crashAppendLine(out, sizeof(out), "abcdefghij", "L1"));
  TEST_ASSERT_EQUAL_STRING("", out);
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_format_line_exact);
  RUN_TEST(test_format_line_too_small);
  RUN_TEST(test_abnormal_classification);
  RUN_TEST(test_boot_count);
  RUN_TEST(test_append_empty);
  RUN_TEST(test_append_caps_to_eight);
  RUN_TEST(test_append_overflow);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m platformio test -e native`
Expected: FAIL to compile — `kernel/crash_log.h: No such file or directory`.

- [ ] **Step 3: Implement `src/kernel/crash_log.h` and `src/kernel/crash_log.cpp`**

`crash_log.h`: `#pragma once`, includes `<stdint.h>`, `<stddef.h>`, `namespace harixos { ... }` with the four constants (`constexpr size_t`) and the four declarations from the Interfaces block.

`crash_log.cpp` notes (the signatures + tests leave the bodies open, except these fixed decisions):
- Format string, exactly: `"%s|%" PRIu32 "|0x%" PRIX32 "|0x%" PRIX32 "|0x%" PRIX32 "|boots=%d"`; wrap the snprintf result: `(r < 0 || (size_t)r >= n) ? -1 : r`.
- `crashReasonIsAbnormal`: `reasonCode >= 1 && reasonCode <= 3`.
- `crashBootCount`: find the **last** `"boots="` occurrence (scan-forward loop, since `strrchr` only finds one char), `atoi` the digits after it; any failure → 0.
- `crashAppendLine`: build into a temp buffer of size `n`: copy `existing`, add `'\n'` if non-empty and missing one, append `line` + `'\n'`; count lines (`'\n'` count); while count > `kCrashMaxLines` skip past the first line; if final length ≥ `n` → `out[0]='\0'; return -1`; copy out and return count.

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m platformio test -e native`
Expected: `7 test cases` from `test_crashlog` PASSED; overall **177 test cases: 177 succeeded**.

- [ ] **Step 5: Commit**

```bash
git add src/kernel/crash_log.h src/kernel/crash_log.cpp test/test_crashlog/test_crashlog.cpp platformio.ini
git commit -m "feat(crash): pure crash_log core with native tests"
```

---

### Task 2: device wiring (boot capture + `info` extension + docs)

**Files:**
- Modify: `src/main.cpp` — includes (top), `handleInfo()` at :369-373, help line at :1851, boot block after LittleFS mount (~:2468-2473)
- Modify: `Documentation/Commands.md:7`
- Modify: `Documentation/Getting-Started.md:74`

**Interfaces:**
- Consumes (Task 1): `harixos::crashReasonIsAbnormal`, `crashBootCount`, `crashFormatLine`, `crashAppendLine`, `kCrashLineMax`, `kCrashLogMax`.
- Consumes (existing): `harixos::readText`/`harixos::writeText` (filesystem.h:22-23, already included main.cpp:25); `HttpDownloader::getStorageInfo(uint32_t&, uint32_t&)` (http_downloader.h:38, already included main.cpp:29); `resetReasonToString()` (main.cpp:161); `bytesToHuman()` (main.cpp, used at :263); `time(nullptr)` threshold `1000000000` (pattern at main.cpp:1647).
- Produces: extended `info` output (FS line, `Last reset:` line, `Time:` line if synced); `/crash.log` written every boot; boot warning line when previous reset abnormal.

- [ ] **Step 1: Boot capture block in `setup()`**

Add `#include "kernel/crash_log.h"` to main.cpp's include list (after `kernel/filesystem/filesystem.h`, main.cpp:25). Immediately after the LittleFS mount block (main.cpp ~:2473, before `expr::setResolver`), insert:

```cpp
{
  const rst_info* ri = ESP.getResetInfoPtr();
  char existing[kCrashLogMax];
  String prev = harixos::readText("/crash.log");
  strncpy(existing, prev.c_str(), sizeof(existing) - 1);
  existing[sizeof(existing) - 1] = '\0';
  char line[kCrashLineMax];
  harixos::crashFormatLine(line, sizeof(line), resetReasonToString().c_str(),
                           ri->exccause, ri->epc1, ri->excvaddr, ri->depc,
                           harixos::crashBootCount(existing) + 1);
  char updated[kCrashLogMax];
  if (harixos::crashAppendLine(updated, sizeof(updated), existing, line) > 0) {
    harixos::writeText("/crash.log", updated, false);  // non-fatal on failure
  }
  if (harixos::crashReasonIsAbnormal(ri->reason)) {
    Serial.printf("Previous run ended abnormally: %s\r\n", line);
  }
}
```

Rules: no early return / no abort on any failure inside this block; `ESP.getResetInfoPtr()` needs no extra include (pulled by `Arduino.h` → add `#include <user_interface.h>` only if the device build says otherwise).

- [ ] **Step 2: Extend `handleInfo()` (main.cpp:369-373)**

After the existing three calls, add:

```cpp
uint32_t total = 0, used = 0;
HttpDownloader::getStorageInfo(total, used);
Serial.printf("  Filesystem: %s used of %s\r\n", bytesToHuman(used).c_str(),
              bytesToHuman(total).c_str());
String crashLog = harixos::readText("/crash.log");
// print the last non-empty '\n'-terminated line as "  Last reset: <line>";
// skip the whole block when the file is empty
time_t now = time(nullptr);
if (now >= 1000000000) {  // same threshold handleTime uses
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
  Serial.printf("  Time: %s\r\n", buf);
}
```

(Implement the last-line extraction as one small loop over the two final newlines; exact wording of the `Last reset:` prefix pinned above.)

- [ ] **Step 3: Help + docs**

- main.cpp:1851 → `    Serial.println(F("  info                 Show system, WiFi, GPIO, FS, and last reset"));`
- `Documentation/Commands.md:7` → `` - `info` — Show system, WiFi, GPIO, filesystem, last reset and (if synced) current time ``
- `Documentation/Getting-Started.md:74` → `` - `info` — system, Wi-Fi, filesystem and last-reset summary ``

- [ ] **Step 4: Run both gates**

Run: `python -m platformio run -e nodemcuv2` → Expected: `SUCCESS`.
Run: `python -m platformio test -e native` → Expected: `177 test cases: 177 succeeded`.

- [ ] **Step 5: Flash and verify on device**

Run: `python -m platformio run -e nodemcuv2 --target upload` (port COM3; if `Access is denied`, close the serial monitor). Verify:
1. Boot reaches the prompt even with `/crash.log` deleted first (`rm /crash.log` on the previous session) — Review Focus 5.
2. A normal `reboot` prints **no** `Previous run ended abnormally:` line — Review Focus 4.
3. `info` shows the `Filesystem:` line, `Last reset:` line, and `Time:` line (after `time sync`) — the deliverable itself.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp Documentation/Commands.md Documentation/Getting-Started.md
git commit -m "feat(info): boot crash-log capture + extend info with FS, last reset, time"
```
