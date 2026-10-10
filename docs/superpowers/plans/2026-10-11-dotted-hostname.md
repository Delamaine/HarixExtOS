# Dotted mDNS Hostname Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let `settings hostname esp8266.rimu` make the device resolve as `esp8266.rimu.local` on the LAN, with hostname validation on both settings paths.

**Architecture:** Three tasks: (1) a pure, natively-tested validator `harixos::api::hostname::valid`; (2) wire it into the CLI and load/notepad paths; (3) vendor the framework's `ESP8266mDNS` into `lib/` with a one-function patch so `_buildDomainForHost` splits the hostname into real DNS labels, proven by a device RED→GREEN resolution probe.

**Tech Stack:** ESP8266 Arduino core (LEAmDNS 1.2), PlatformIO (`nodemcuv2` + `native` env), Unity tests, PowerShell device probes (`Resolve-DnsName` + raw UDP PTR probe).

**Spec:** `docs/superpowers/specs/2026-10-11-dotted-hostname-design.md`

## Global Constraints

- Gates (every task): `cmd.exe /c "python -m platformio run -e nodemcuv2 && cmd.exe /c python -m platformio test -e native"` → SUCCESS + all tests green.
- Flash: `python -m platformio run -e nodemcuv2 --target upload` (COM3, 115200; close serial monitor first if `Access is denied`).
- Serial rules: send **LF only** (never `\r\n` — a stray `\n` breaks command parsing); wait up to 90 s for the boot prompt; command prompt is `HarixOS> `.
- LSP diagnostics (`Arduino.h not found`, `Unknown type name String`) are pre-existing noise on every file — ignore; PlatformIO is the only truth.
- Validation rules (spec): lowercase-first at call sites via `String::toLowerCase()`; valid = `[a-z0-9-]` chars, no empty/leading/trailing dots, total length (dots included) ≤ 63 — matching stock `_setHostname`'s budget so the lib's own check stays untouched.
- Vendored library: `lib/ESP8266mDNS/` copied from `framework-arduinoespressif8266/libraries/ESP8266mDNS` (version 1.2); **only** `_buildDomainForHost` may differ from upstream; provenance comment required above the patch.
- `Resolve-DnsName` performs mDNS only for A/AAAA `.local` names — service-chain checks must use the raw UDP PTR probe pattern (see Task 3).
- Existing device harnesses: `C:\Users\delam\AppData\Local\Temp\opencode\mdns_t2.py` (33 checks) and `mdns_t3.py` (modes `red`/`green`, 17 checks).
- Device hostname must end the plan back at `test1`.
- No new dependencies, no TXT/AP-mode changes, ESP32 out of scope.

## Review Focus

1. **Vendored lib never takes precedence** (PlatformIO may keep resolving `ESP8266mDNS` to the framework copy) → silently still broken. *Owned by Task 3, GREEN step:* `Resolve-DnsName -Type A esp8266.rimu.local` returning the device IP is only possible if the patched vendored copy is compiled in.
2. **Patch regresses single-label names or the probe path** (`_buildDomainForHost` is shared by A/SRV/PTR/probes) → `test1.local` stops resolving, conflict probes break. *Owned by Task 3:* `settings hostname test1` → resolves, then full `mdns_t2.py` (33/33) + `mdns_t3.py green` (17/17).
3. **Validation rejects legitimate names** (uppercase input, numeric labels, hyphen-edged labels). *Owned by Task 1 tests:* `test_valid_numeric_hyphen`, `test_rejects_uppercase` (raw uppercase rejected; call sites lowercase first) — and Task 2 device step `settings hostname RIMU` → saved as `rimu`.
4. **Invalid persisted hostname survives reload** (notepad writes `.rimu` → boot/`settings reload` feeds it to `MDNS.begin` unchanged). *Owned by Task 2:* notepad `hostname=.rimu` + `settings reload` → `settings show` falls back to `harixos-<chipid>`.
5. **Dotted service-instance/SRV mismatch** (instance stays one label with dots; SRV target must be the split host domain) → discovery finds the service but connection fails. *Owned by Task 3:* raw UDP PTR probe asserts SRV target `esp8266.rimu.local` and `curl http://esp8266.rimu.local:8266/` succeeds.

---

### Task 1: `api/hostname` validator (native TDD)

**Files:**
- Create: `src/api/hostname.h`, `src/api/hostname.cpp`
- Create: `test/test_hostname/test_hostname.cpp`
- Modify: `platformio.ini:68` (native `build_src_filter`: add `+<api/hostname.cpp>`)

**Interfaces:**
- Consumes: nothing.
- Produces: `bool harixos::api::hostname::valid(const char *name)` — pure, no Arduino includes, declared in `api/hostname.h`. Task 2 includes this header and lowercases its input before calling.

- [ ] **Step 1: Write the failing tests**

Create `test/test_hostname/test_hostname.cpp` following the `test/test_expr/test_expr.cpp` pattern (`#include <unity.h>`, `#include "api/hostname.h"`, `using harixos::api::hostname::valid;`, `main` with `UNITY_BEGIN()` / `RUN_TEST` / `return UNITY_END();`). Tests:

```cpp
void test_valid_single_label()        { TEST_ASSERT_TRUE(valid("esp8266")); }
void test_valid_dotted()              { TEST_ASSERT_TRUE(valid("esp8266.rimu")); }
void test_valid_numeric_hyphen()      { TEST_ASSERT_TRUE(valid("a1-b2"));
                                        TEST_ASSERT_TRUE(valid("-x-"));
                                        TEST_ASSERT_TRUE(valid("123")); }
void test_valid_total_63()            { char n[64]; memset(n, 'a', 63); n[63] = '\0';
                                        TEST_ASSERT_TRUE(valid(n)); }
void test_rejects_null()              { TEST_ASSERT_FALSE(valid(nullptr)); }
void test_rejects_empty()             { TEST_ASSERT_FALSE(valid("")); }
void test_rejects_uppercase()         { TEST_ASSERT_FALSE(valid("ESP8266"));
                                        TEST_ASSERT_FALSE(valid("ESP8266.rimu")); }
void test_rejects_leading_dot()       { TEST_ASSERT_FALSE(valid(".rimu")); }
void test_rejects_double_dot()        { TEST_ASSERT_FALSE(valid("a..b")); }
void test_rejects_trailing_dot()      { TEST_ASSERT_FALSE(valid("rimu.")); }
void test_rejects_bad_chars()         { TEST_ASSERT_FALSE(valid("a b"));
                                        TEST_ASSERT_FALSE(valid("a_b"));
                                        TEST_ASSERT_FALSE(valid("a!b")); }
void test_rejects_label_64()          { char n[65]; memset(n, 'a', 64); n[64] = '\0';
                                        TEST_ASSERT_FALSE(valid(n)); }
void test_rejects_total_64()          { char n[65]; memset(n, 'a', 63); n[63] = '.';
                                        n[64] = '\0';               /* 64 chars total */
                                        TEST_ASSERT_FALSE(valid(n)); }
```

Register all 13 in `main`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m platformio test -e native --filter test_hostname`
Expected: FAIL to build — `api/hostname.h: No such file or directory`.

- [ ] **Step 3: Implement `bool harixos::api::hostname::valid(const char *name)` in `src/api/hostname.cpp`**

Header: `namespace harixos { namespace api { namespace hostname { bool valid(const char *name); } } }`, include guard only, no Arduino/C++ std includes beyond `<stddef.h>` if needed. Body decision table (single pass over the string):

- `name == nullptr` or `name[0] == '\0'` → `false`
- total length > 63 → `false`
- any char outside `[a-z0-9-]` (`.`, uppercase, space, `_`, `!` …) → `false`
- `.` at position 0 or at the last position, or two `.` in a row → `false`
- otherwise `true`

(The per-label ≤63 rule is subsumed by total ≤63; do not add a redundant scan.)

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m platformio test -e native --filter test_hostname`
Expected: PASS, 13/13. Then full gates: expect **190/190** (177 existing + 13) and nodemcuv2 SUCCESS (validator not yet linked into firmware — that lands in Task 2).

- [ ] **Step 5: Commit**

```bash
git add src/api/hostname.h src/api/hostname.cpp test/test_hostname/test_hostname.cpp platformio.ini
git commit -m "feat(hostname): pure validator for dotted settings hostnames"
```

---

### Task 2: Validation on both settings paths

**Files:**
- Modify: `src/main.cpp:799-808` (`handleSettings` hostname action) + one `#include "api/hostname.h"` among the `api/` includes (~line 33)
- Modify: `src/apps/settings/settings.cpp:17-24` (`resolveHostname`) + `#include "../../api/hostname.h"` (relative style, matching line 3)

**Interfaces:**
- Consumes: `harixos::api::hostname::valid(const char*)` (Task 1).
- Produces: error string exactly `Invalid hostname: use a-z, 0-9, '-' labels separated by '.' (max 63 chars)`; `resolveHostname` keeps its `String resolveHostname(const String&)` signature and now returns lowercased-valid-or-default.

- [ ] **Step 1: Wire the CLI path**

In `main.cpp` hostname action (currently assigns `cmd.tokens[2]` raw at :804): copy `cmd.tokens[2]` into a `String`, `toLowerCase()`, and only then assign/save/restart-mDNS as today. On `valid()` false: print the exact error line above and leave the old hostname untouched. Do not alter the existing save + `mdnsApplyHostname` logic.

- [ ] **Step 2: Wire the load/notepad path**

`resolveHostname` (settings.cpp:17): if `length() > 0`, `toLowerCase()` a copy and return it when `valid()` passes; otherwise fall through to the existing `harixos-<chipid>` default. Covers file load, `settings reload`, and notepad edits with one guard.

- [ ] **Step 3: Build + flash**

Run gates (expect SUCCESS + 190/190), then upload.

- [ ] **Step 4: Device verification**

LF-only sends; expected outputs:

```
settings hostname .rimu        → Invalid hostname: use a-z, 0-9, '-' labels separated by '.' (max 63 chars)
settings hostname a..b         → same error
settings hostname a_b          → same error
settings hostname rimu.        → same error
settings hostname aaaa...(64)  → same error
settings show                  → Hostname: test1   (unchanged after every rejection)
settings hostname RIMU         → Settings saved. Hostname: rimu   (lowercased)
settings hostname test1        → Settings saved. Hostname: test1
```

Reload fallback: `notepad /harixos/settings.cfg` → edit the `hostname=` line to `.rimu` → `:wq` → `settings reload` → `settings show` → `Hostname: harixos-<chipid>` (the device's default). Restore with `settings hostname test1`.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp src/apps/settings/settings.cpp
git commit -m "feat(hostname): validate settings hostname on CLI and load paths"
```

---

### Task 3: Vendor LEAmDNS, patch label splitting, device GREEN

**Files:**
- Create: `lib/ESP8266mDNS/src/**`, `lib/ESP8266mDNS/library.properties` (verbatim copy of the framework's, version 1.2)
- Modify: `lib/ESP8266mDNS/src/LEAmDNS_Transfer.cpp` (`_buildDomainForHost`, currently ~line 982)
- Modify: `Documentation/Commands.md:109`

**Interfaces:**
- Consumes: dotted hostname accepted by `settings hostname` (Task 2); device harness scripts (Global Constraints).
- Produces: device resolves `esp8266.rimu.local`; patched vendored lib is the compiled `ESP8266mDNS`.

- [ ] **Step 1: RED — capture the broken baseline**

Device is running the Task 2 build with hostname `test1`.

```
settings hostname esp8266.rimu   → Settings saved. Hostname: esp8266.rimu
Resolve-DnsName -Type A esp8266.rimu.local
```

Expected: resolution **FAILS** (name not found / timeout). Record the exact failure text — this is the RED baseline: name accepted, wire format still wrong.

- [ ] **Step 2: Vendor the library**

```powershell
$src = "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif8266\libraries\ESP8266mDNS"
New-Item -ItemType Directory -Force lib\ESP8266mDNS | Out-Null
Copy-Item "$src\src" -Destination lib\ESP8266mDNS\ -Recurse
Copy-Item "$src\library.properties" -Destination lib\ESP8266mDNS\
```

Skip `examples/`, `keywords.txt`, `README.rst` per spec. Do not modify any file yet.

- [ ] **Step 3: Patch `_buildDomainForHost` (vendored copy only)**

Signature unchanged: `bool MDNSResponder::_buildDomainForHost(const char *p_pcHostname, stcMDNS_RRDomain &p_rHostDomain) const`. Replace the body's single `addLabel(p_pcHostname)` with a loop over dot-separated segments:

```
clear p_rHostDomain
if (!p_pcHostname || !*p_pcHostname) return false
for each segment of p_pcHostname split on '.':
    if segment empty -> return false          // leading/trailing/double dot
    if (!p_rHostDomain.addLabel(segment)) return false   // addLabel enforces <=63
return p_rHostDomain.addLabel(scpcLocal) && p_rHostDomain.addLabel(0)
```

Preceded by the provenance comment:

```cpp
// HARIXOS PATCH: split the hostname on '.' into separate labels.
// Upstream LEAmDNS writes it as ONE label, so dotted hostnames like
// esp8266.rimu.local never match client queries. Synced from
// ESP8266mDNS 1.2 (framework-arduinoespressif8266).
```

No other file in `lib/ESP8266mDNS/` may differ from upstream.

- [ ] **Step 4: Gates + flash**

Run full gates → SUCCESS + 190/190 (a successful build does **not** prove lib precedence — only Step 6 does). Then upload. The device still has `hostname=esp8266.rimu` from Step 1.

- [ ] **Step 5: GREEN — resolution**

```
Resolve-DnsName -Type A esp8266.rimu.local   → returns the device IP (e.g. 192.168.20.26)
settings show                                → Hostname: esp8266.rimu
info                                          → mDNS: esp8266.rimu.local
```

This single pass proves three things at once: Windows multicasts multi-label `.local`, the patched code is the compiled code (precedence), and the split labels match client queries. If it fails while the patch looks right, investigate LDF precedence first (which `ESP8266mDNS` got linked).

- [ ] **Step 6: GREEN — service chain + HTTP**

`serve <existing-file> 8266` (pick any file already on LittleFS; if Wi-Fi prompts appear, it is already connected — proceed). Then raw UDP PTR probe for `_http._tcp.local` (adapt the P5 probe pattern in `mdns_t3.py`: bind :5353, join 224.0.0.251 group, send query, filter QR-bit responses). Assert the response chain contains:

```
_services._dns-sd._udp.local PTR  _http._tcp.local
esp8266.rimu._http._tcp.local  SRV  esp8266.rimu.local:8266
esp8266.rimu.local A → device IP
```

Then `curl http://esp8266.rimu.local:8266/` → HTTP 200 with file content. Finally `stop` the server.

- [ ] **Step 7: Restore + single-label regression**

```
settings hostname test1                        → Settings saved. Hostname: test1
Resolve-DnsName -Type A test1.local            → device IP
```

- [ ] **Step 8: Full harness regression**

```powershell
python C:\Users\delam\AppData\Local\Temp\opencode\mdns_t2.py    # expect 33/33
python C:\Users\delam\AppData\Local\Temp\opencode\mdns_t3.py green  # expect 17/17
```

(LF-only rule applies inside the harnesses; they already follow it. If COM3 access is denied, close the serial monitor.)

- [ ] **Step 9: Docs**

`Documentation/Commands.md:109` — append to the settings line: `; dotted hostnames are supported and resolve under .local (e.g. esp8266.rimu)`.

- [ ] **Step 10: Commit**

```bash
git add lib/ESP8266mDNS Documentation/Commands.md
git commit -m "feat(mdns): dotted hostname labels via vendored LEAmDNS patch"
```
