# WiFi Persistence Gaps Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every WiFi connect path writes credentials to `/harixos/settings.cfg`, so the device re-joins after a reboot regardless of how it was connected.

**Architecture:** `shellSettings` currently lives inside `main.cpp`'s anonymous namespace and therefore cannot be seen from `wifi_api.cpp`. Move it into the settings module as a proper `extern`, then have the two non-saving paths write through it. No new files.

**Tech Stack:** C++ (Arduino ESP8266 core 3.30102.0), PlatformIO 6.2.0.

**Spec:** `docs/superpowers/specs/2026-09-29-hx-language-and-cron-design.md` §4 — the plan argues from §4; the spec travels with it.

## Global Constraints

- **Build gate after every task:** `pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini` must be **4/4 SUCCESS** with no new warnings from `src/`. Final verification uses `pio run -t clean` first, then a full rebuild.
- If `pio` is not on PATH, use the full path `C:\Users\delam\AppData\Roaming\Python\Python312\Scripts\pio.exe`.
- **No new dependencies.** `platformio.ini` has no `lib_deps` and must not gain one.
- `default_envs = esp01_1m` (the smallest target) must not change.
- LSP/clangd diagnostics such as `'Arduino.h' file not found` are environmental noise — clangd lacks PlatformIO include paths. **They are not failures.** `pio run` is the only gate.
- Branch from `main` (currently `e5980d7`). Do not push.
- WiFi persistence is file-based only: the project never calls `WiFi.persistent(true)`. Do not introduce SDK-level persistence.

## Review Focus

These are the failure modes the spec implies but no host test can catch (WiFi requires hardware), so each is pinned to a **serial checklist step** in the owning task rather than a unit test.

1. **A failed connect must not write settings.** An empty or partial credential pair written to `settings.cfg` would make `tryAutoWifi()` loop on a bad SSID at every boot. *Pinned in Task 3, Step 4.*
2. **Cancelling the `serve` prompt must not write settings.** The prompt is cancellable (blank SSID) and can time out; neither path may touch flash. *Pinned in Task 2, Step 4.*
3. **`loadSettings()` must still run before `tryAutoWifi()` at boot.** Moving the `shellSettings` definition must not reorder `setup()`. *Pinned in Task 1, Step 4.*
4. **`WiFiAPI::connect()` is reachable from scripts and the scheduler**, so a scheduled task could write flash on every firing — it must save only on success, never on failure. *Pinned in Task 3, Step 4.*

---

### Task 1: Move `shellSettings` into the settings module

**Files:**
- Modify: `src/apps/settings/settings.h` (append after line 17)
- Modify: `src/apps/settings/settings.cpp` (top of file, after the existing `kSettingsPath` definition)
- Modify: `src/main.cpp:42` (delete the definition)

**Interfaces:**
- Produces: `extern AppSettings shellSettings;` declared in `namespace harixos` inside `settings.h`. Tasks 2 and 3 read and write this object; `main.cpp` keeps using the symbol unqualified because it already has `using namespace harixos`-equivalent access via its existing calls to `harixos::loadSettings()`.
- Consumes: nothing new. `settings.h` already `#pragma once`-guarded and includes `<Arduino.h>`.

- [ ] **Step 1: Declare the extern**

Add to `src/apps/settings/settings.h`, after the `printSettings` declaration at line 17 and before the closing `}  // namespace harixos`:

```cpp
extern AppSettings shellSettings;
```

- [ ] **Step 2: Define it in `settings.cpp`**

Add to `src/apps/settings/settings.cpp`, inside `namespace harixos`:

```cpp
AppSettings shellSettings;
```

Do **not** add an initializer — the struct's in-class defaults (`bannerEnabled = true`, `timezone = "UTC0"`, etc.) apply, and `setup()` overwrites it via `loadSettings()`.

- [ ] **Step 3: Delete the old definition**

Remove line 42 of `src/main.cpp`, `harixos::AppSettings shellSettings;`. Leave the `namespace {` block and surrounding lines untouched.

- [ ] **Step 4: Build and verify no reordering**

Run:
```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS.** A missing or duplicate definition surfaces as a linker error (`multiple definition of ... shellSettings` or `undefined reference`), so a green link is the proof.

Then confirm `setup()` ordering is untouched — this is Review Focus item 3:
```
grep -n "loadSettings\|tryAutoWifi" src/main.cpp
```
Expected: `loadSettings()` on an earlier line than `tryAutoWifi()`, exactly as before (`:2087` before `:2099`).

- [ ] **Step 5: Commit**

```bash
git add src/apps/settings/settings.h src/apps/settings/settings.cpp src/main.cpp
git commit -m "refactor(settings): move shellSettings out of main.cpp anonymous namespace

It has internal linkage at main.cpp:42, so wifi_api.cpp cannot reach it.
Precondition for saving credentials from the API connect path."
```

---

### Task 2: Save credentials from the `serve` interactive prompt

**Files:**
- Modify: `src/main.cpp:1007-1012` (the success branch of `handleServe`'s WiFi prompt)

**Interfaces:**
- Consumes: `harixos::shellSettings` and `harixos::saveSettings(const AppSettings&)` from Task 1. `main.cpp` already includes `apps/settings/settings.h` and already calls `harixos::saveSettings` at `:856`, so no include changes are needed.

- [ ] **Step 1: Save on the success path only**

The current block is:

```cpp
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("WiFi connection failed."));
      return;
    }
    Serial.println(F("Connected."));
```

Insert the save immediately **after** `Serial.println(F("Connected."));` and before the closing brace of the `if (WiFi.status() != WL_CONNECTED)` outer block:

```cpp
    shellSettings.wifiSSID = ssid;
    shellSettings.wifiPassword = pass;
    harixos::saveSettings(shellSettings);
    Serial.println(F("WiFi credentials saved. Auto-connect enabled."));
```

Placement matters: it must sit **after** the `WL_CONNECTED` guard so the failure branch returns before touching flash (Review Focus 1 and 2 both depend on this).

- [ ] **Step 2: Build**

Run:
```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS.**

- [ ] **Step 3: Confirm no other write path was disturbed**

```
grep -n "saveSettings" src/main.cpp src/api/wifi_api.cpp
```
Expected before this task's successors: save calls only where previously present (`:627`, `:646`, `:660`, `:666`, `:856`, `:1631`) plus the one just added.

- [ ] **Step 4: Serial checklist — this is Review Focus items 1 and 2**

Flash `esp01_1m`, open serial at 115200, then:

1. `wifi disconnect`, power-cycle so auto-connect has no stored network, `serve` → when prompted, enter a valid SSID and password.
   Expected: `Connected.` then `WiFi credentials saved. Auto-connect enabled.`
2. Power cycle.
   Expected: `Auto-connect: Scanning for '<SSID>'...` and the device joins without re-prompting.
3. `cat /harixos/settings.cfg`
   Expected: `wifiSSID=` and `wifiPassword=` lines populated with what you entered.
4. **Negative:** power-cycle with no stored network, `serve`, and this time enter a **blank** SSID.
   Expected: `serve cancelled.` — and `cat /harixos/settings.cfg` shows the **previously stored** values, unchanged.
5. **Negative:** enter an SSID that will not connect, wait out the 10 s timeout.
   Expected: `WiFi connection failed.` — and `settings.cfg` still unchanged.

Steps 4 and 5 are the actual review; steps 1–3 only prove the happy path.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "fix(wifi): persist credentials from the serve prompt

The interactive SSID/password prompt called WiFi.begin and reported
success without ever writing settings.cfg, so the join was lost on reboot."
```

---

### Task 3: Save credentials from `WiFiAPI::connect()`

**Files:**
- Modify: `src/api/wifi_api.cpp` (add include near line 1-10; edit the success branch at `:64-65`)

**Interfaces:**
- Consumes: `harixos::shellSettings` and `harixos::saveSettings(const AppSettings&)` from Task 1.
- Produces: `WiFiAPI::connect(ssid, password, timeoutMs)` gains a side effect — on success it writes `/harixos/settings.cfg`. This is the path taken by `wifi connect` inside a `.hx` script (`script_engine.cpp:127-134`) and inside a scheduled command.

- [ ] **Step 1: Include the settings header**

Add to `src/api/wifi_api.cpp`, with the other includes at the top of the file:

```cpp
#include "../apps/settings/settings.h"
```

- [ ] **Step 2: Save on the success branch only**

Current code at `:64-68`:

```cpp
  if (WiFi.status() == WL_CONNECTED) {
    return ApiResult(API_OK, "Connected to " + ssid);
  } else {
    return ApiResult(API_WIFI_CONNECTION_FAILED, "Failed: " + wifiStatusToString(WiFi.status()));
  }
```

Change the success branch to save before returning:

```cpp
  if (WiFi.status() == WL_CONNECTED) {
    shellSettings.wifiSSID = ssid;
    shellSettings.wifiPassword = password;
    harixos::saveSettings(shellSettings);
    return ApiResult(API_OK, "Connected to " + ssid);
  } else {
    return ApiResult(API_WIFI_CONNECTION_FAILED, "Failed: " + wifiStatusToString(WiFi.status()));
  }
```

The `else` branch must remain untouched — Review Focus 4 and the spec's §4 note that saving on failure would store a credential pair that never worked.

- [ ] **Step 3: Build**

Run:
```
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: **4/4 SUCCESS.**

- [ ] **Step 4: Serial checklist — this is Review Focus items 1 and 4**

1. Clear the saved network first: `wifi disconnect`, then delete or edit `/harixos/settings.cfg` so auto-connect cannot mask the result.
2. Install and run a script containing `wifi connect <ssid> <password>`.
   Expected: the script reports success, and `cat /harixos/settings.cfg` now shows those credentials.
3. Power cycle.
   Expected: `Auto-connect: Scanning for '<ssid>'...` and the device joins — proving the *script* path, not the shell path, persisted it.
4. **Negative:** run `wifi connect` with a bad password from a script.
   Expected: script reports connection failure, and `settings.cfg` is **unchanged** (still shows step 2's values, or nothing).
5. Regression: run `wifi connect <ssid> <password>` at the shell prompt.
   Expected: still saves, as it did before this plan.

- [ ] **Step 5: Commit**

```bash
git add src/api/wifi_api.cpp
git commit -m "fix(wifi): persist credentials from WiFiAPI::connect

wifi connect executed inside a .hx script or a scheduled command goes
through WiFiAPI::connect, which returned success without writing
settings.cfg — so those joins were lost on reboot."
```

---

### Task 4: Final verification

**Files:** none modified.

- [ ] **Step 1: Clean rebuild**

```
pio run -t clean -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
pio run -e esp8266_generic -e esp01_1m -e nodemcuv2 -e d1_mini
```
Expected: clean reports 4/4 SUCCESS, then rebuild reports **4/4 SUCCESS** with no `error:` and no `warning:` lines originating from `src/`.

Record the `RAM:` and `Flash:` figures. Expected on `esp01_1m`: RAM `35936 bytes` / 43.9%, Flash `510115 bytes` / 67.0% — these three tasks add at most a few dozen bytes of code, so a jump of more than ~1 KB means something unexpected got linked.

- [ ] **Step 2: Re-run the full WiFi checklist**

Run every numbered step from Task 2 Step 4 and Task 3 Step 4 in one session against the final build, in order. Any divergence from the stated expectations is a failure of this plan.

- [ ] **Step 3: Confirm no dependency was added and nothing is uncommitted**

```
grep -n "lib_deps" platformio.ini
git status --short
git log --oneline main..HEAD
```
Expected: `lib_deps` matches nothing; `git status --short` is empty; three commits, one per task.
