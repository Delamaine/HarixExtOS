# Dotted mDNS Hostname: multi-label names like `esp8266.rimu.local`

## Goal

Allow `settings hostname esp8266.rimu` so the device resolves as
`esp8266.rimu.local` on the LAN (mDNS stays `.local`; `.rimu` is a label,
not a new zone). Stock LEAmDNS cannot do this — it writes the hostname as
a single DNS label — so the library ships patched. Also lands the
deferred hostname-validation minor for both settings paths.

## Current State

- No validation anywhere: `resolveHostname` (settings.cpp:17) only checks
  `length() > 0` (empty → `harixos-<chipid>`); the CLI path
  (main.cpp:799-807) assigns `tokens[2]` raw. Any string reaches
  `MDNS.begin()` (main.cpp:725, 2619).
- LEAmDNS treats the hostname as one label, with no dot handling:
  - `_setHostname` (LEAmDNS_Helpers.cpp:357) checks only
    `strlen <= MDNS_DOMAIN_LABEL_MAXLENGTH` (63) — dots pass silently.
  - `_buildDomainForHost` (LEAmDNS_Transfer.cpp:982) does
    `addLabel(hostname) + addLabel("local")`; `addLabel`
    (LEAmDNS_Structs.cpp:681) writes raw bytes, never splits on `.`.
  - Result: `begin("esp8266.rimu")` advertises wire name
    `[11]"esp8266.rimu"[5]"local"` while clients query
    `[9]"esp8266"[4]"rimu"[5]"local"` — byte mismatch, never answered.
- `_buildDomainForHost` is the single choke point: A record, SRV target,
  probes, and reverse-domain all flow through it (LEAmDNS_Transfer.cpp
  1441/1558/1770, LEAmDNS_Control.cpp 8 call sites). Service *instance*
  names use `addLabel` directly and are untouched (dots are legal bytes
  inside one DNS-SD instance label).
- Library lives inside the framework package
  (`framework-arduinoespressif8266/libraries/ESP8266mDNS/src`, synced
  from ESP8266 Arduino core). Private `lib/` libraries outrank framework
  libraries in PlatformIO's LDF — treated as an assumption to be proven
  by the GREEN probe, not a fact.
- Windows/macOS resolve `*.local` queries via multicast regardless of
  label count — assumption to be proven by the RED probe.

## Decisions

| Topic | Decision |
|---|---|
| Scope | Multi-label hostname under `.local` only; no real `.rimu` unicast zone |
| Lib delivery | Vendor `ESP8266mDNS` → `lib/ESP8266mDNS/` (src + manifest, no examples); approach A, approved |
| Patch | One function: `_buildDomainForHost` splits hostname on `.` into per-label `addLabel` calls; `_setHostname`, `addLabel`, instance handling untouched |
| Provenance | Comment above the patch: what it changes, which core version it was synced from |
| Validation rules | Lowercase-normalize on save; labels `[a-z0-9-]`, no empty/leading/trailing dots, each label ≤63, total ≤63 (keeps `_setHostname` as-is) |
| Validator home | `api/hostname.{h,cpp}` — pure `const char*`, natively testable |
| CLI path | `handleSettings` hostname action (main.cpp:799): normalize + validate → else one error line, no change |
| Load/notepad path | `resolveHostname` (settings.cpp:17): invalid → default hostname (same shape as today's empty → default) |
| Service instance | Stays hostname-as-single-label; no code change |
| Failure mode | `MDNS.begin` false → existing `mDNS: start failed.` line; no new failure paths |
| Tests | Native validator tests + device RED/GREEN; existing mDNS harnesses as regression |

## Design

### 1. Validator (`api/hostname.{h,cpp}`)

- `namespace harixos::api::hostname`:
  - `bool valid(const char *name)` — rejects null/empty, uppercase
    (both call sites lowercase first via `String::toLowerCase()`), chars
    outside `[a-z0-9-]`, leading/trailing/double dots, label >63,
    total >63.
- No Arduino includes, no `normalize` helper — lowercasing is a
  one-call `String::toLowerCase()` at each of the two call sites.
  Added to `[env:native]` `build_src_filter` (`+<api/hostname.cpp>`).

### 2. Settings wiring

- CLI (main.cpp:804): `normalize` → `valid` → on failure print
  `Invalid hostname: use a-z, 0-9, '-' labels separated by '.' (max 63 chars)`,
  keep old value.
- Load/notepad (settings.cpp:17): `resolveHostname` returns the value
  only if `valid()`, else the `harixos-<chipid>` default — covers both
  `settings reload` and notepad edits with one guard.
- `settings show`, `info`'s `mDNS:` line, and both help lines print the
  raw value + `.local` — already correct, no edits.

### 3. Vendoring + patch

- Copy `framework-arduinoespressif8266/libraries/ESP8266mDNS/{src,
  library.properties}` → `lib/ESP8266mDNS/` (manifest: ESP8266mDNS 1.2,
  LaborEtArs; per-file MIT headers already present; skip
  examples/keywords/README).
- Patch `_buildDomainForHost` only:

  ```
  clear domain
  for each segment of hostname split on '.':
      if segment empty -> fail (return false)
      addLabel(segment)   // enforces <=63 per label
  addLabel("local"); addLabel(0)
  ```

- Everything that builds the host domain (answers, probes, SRV, PTR)
  picks up the split automatically; `m_pcHostname` itself stays raw and
  still feeds the auto service instance name unchanged.

### 4. Surfaces

- `Documentation/Commands.md:109`: settings list gains
  `e.g. esp8266.rimu`.
- Help strings unchanged (already `(<name>.local)`).

## Verification

- Gates: `python -m platformio run -e nodemcuv2` → SUCCESS;
  `python -m platformio test -e native` → 177 + new hostname tests, all
  green.
- Native tests (`test/test_hostname/`): valid single-label, valid
  dotted, uppercase → normalize-then-valid, `.rimu`, `a..b`, `a b`, `a-
  b` ok, label 64 chars, total 64 chars, empty, null-ish.
- Device RED (before patch): `settings hostname esp8266.rimu` is
  accepted but `Resolve-DnsName -Type A esp8266.rimu.local` fails →
  baseline documenting today's broken wire format.
- Device GREEN (after): same query resolves to the device IP — which is
  also the proof that Windows *does* multicast multi-label `.local`
  queries; `settings hostname esp8266.rimu` → `Settings saved. Hostname:
  esp8266.rimu`; `serve` → raw UDP PTR probe shows the `_http._tcp`
  chain with SRV target `esp8266.rimu.local`; browser/curl fetch via
  `http://esp8266.rimu.local/` succeeds. GREEN also proves the vendored
  library took precedence over the framework copy.
- Regressions: single-label `test1` through the full existing mDNS
  harness; invalid values rejected at both paths (`settings reload`
  after a notepad edit falls back to default); `settings show` /
  `info` / help lines unchanged.

## Non-Goals

- ESP32 `ESPmDNS` parity (ESP32 port not commissioned).
- A real `.rimu` unicast DNS zone (router-side feature).
- Upstream LEAmDNS PR (possible later; not a gate).
- Dot handling in service *instance* names as separate labels.
- AP-mode advertising, TXT records (unchanged from mDNS spec).
