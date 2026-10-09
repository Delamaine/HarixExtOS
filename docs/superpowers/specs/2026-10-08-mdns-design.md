# mDNS: Local Hostname + HTTP Service Announcement

## Goal

Reach the device by name on the LAN (`harixos-1a2b3c.local`) from any
browser/OS, and advertise its HTTP server as `_http._tcp` so mDNS
consumers (browser share dialogs, Home Assistant, network scanners) can
find it. Measured cost on nodemcuv2: **+21,324 B flash, +200 B static
RAM, ~2-4 KB heap while connected; no new dependencies.**

## Current State

- The ESP8266 Arduino core ships `ESP8266mDNS` (LEAmDNS) with a global
  `MDNSResponder MDNS` instance (`ESP8266mDNS.h:54`). Nothing in this
  repo uses it yet (grep: 0 hits).
- Three blocking connect-success sites: `tryAutoWifi` (main.cpp:1632),
  the interactive join inside `serve` (main.cpp:1043),
  `WiFiAPI::connect` (wifi_api.cpp:66). All three miss silent
  auto-reconnects (`WIFI_AUTO_RECONNECT`) after a router reboot, so a
  per-site call would leave mDNS stale exactly when it is most needed.
- `serve` lifecycle: server created at main.cpp:1078-1081 (port in
  `httpServePort`), stopped at `handleHttpStop` (main.cpp:1086) and
  torn down/recreated on restart at main.cpp:1067-1072.
- Settings are single `key=value` lines parsed by first-occurrence
  `indexOf`; `resolveMqttPrefix` (settings.cpp:10-15) is the precedent
  for "empty → derived-at-load" defaults.
- Baseline build (nodemcuv2): RAM 53,832 / 81,920; flash 592,747 /
  1,044,464. A/B probe (include + `begin` + `addService` + `update`,
  since reverted): flash 614,071 (+21,324), RAM 54,032 (+200).

## Decisions

| Topic | Decision |
|---|---|
| Scope | Advertise only (hostname + `_http._tcp`); no mDNS query/client role |
| Approach | Direct `MDNS.*` calls in `main.cpp`; no wrapper module (approach A) |
| Library | Framework-bundled ESP8266mDNS; no `lib_deps` change |
| Hostname | `settings hostname <name>`; default `harixos-<chipid>` resolved at load (mqttPrefix precedent) |
| Lifecycle | `loop()` detects Wi-Fi down→up with a valid IP: first up → `begin`, later ups → `notifyAPChange()` + `announce()` |
| Service | `addService` when the HTTP server starts, `removeService` before any re-add/stop; STA only |
| Failure mode | Non-fatal: one Serial error line, boot/`serve` continue |
| TXT records | None (YAGNI v1) |
| Tests | No native tests (pure wiring); device verification is the test |
| AP mode | Out of scope — `startAP` does not advertise |

## Design

### 1. Settings and hostname

- `AppSettings` gains `String hostname = "";` with comment
  `// empty -> "harixos-<chipid>" at load`.
- `loadSettings` parses a `hostname=` line; after parsing, an empty
  value resolves to `String("harixos-") + String(ESP.getChipId(), HEX)`
  via a small `resolveHostname` helper mirroring `resolveMqttPrefix`.
  The resolved value is in-memory only until some later `saveSettings`
  (same behavior as `mqttPrefix`).
- `saveSettings` appends `hostname=<value>`; `printSettings` prints
  `Hostname: <value>`.
- `handleSettings` gains a `hostname` action:
  `settings hostname <name>` — reject an empty value with a usage line,
  otherwise assign, save, and if the responder is already running
  restart it (`MDNS.end()` then `MDNS.begin(new)`) so the change takes
  effect immediately. `end()` invalidates the service handle, so the
  restart path nulls `mdnsHttpService` and re-adds `_http._tcp` if
  `httpServerRunning`. Report `MDNS.begin` failure as a Serial error.
- Help line at main.cpp:716 gains `settings hostname <name>`.

### 2. mDNS lifecycle (main.cpp)

- `#include <ESP8266mDNS.h>` with the other includes.
- File-scope statics in `main.cpp` (serve functions and `settings
  hostname` need them too, so they cannot live inside `loop()`):
  `mdnsStarted`, `mdnsWasUp`, and `mdnsHttpService` (the
  `hMDNSService` handle returned by `addService`, or `nullptr` —
  `removeService` takes a handle, LEAmDNS.h:227, not a name).
- One transition detector in `loop()`:

  ```
  up = WiFi.status() == WL_CONNECTED && WiFi.localIP() != 0.0.0.0
  if (up && !mdnsWasUp):
      if (!mdnsStarted): mdnsStarted = MDNS.begin(hostname);  // error log on false
      else:              MDNS.notifyAPChange(); MDNS.announce();
  mdnsWasUp = up
  MDNS.update()   // only when mdnsStarted
  ```

  The valid-IP check avoids racing DHCP (`WL_CONNECTED` can precede
  `gotIP`). This single seam covers first boot, `wifi join`, the
  `serve` interactive join, `WiFiAPI::connect`, and silent
  auto-reconnects. `settings hostname` restart covers renames without
  waiting for a reconnect.
- No disconnect hook: on down the responder simply goes quiet; the next
  up triggers `notifyAPChange()` + `announce()`.

### 3. Serve wiring

- `startHttpServer`: after `httpServer->begin()`, if `mdnsStarted`:
  remove any previous handle first (`if (mdnsHttpService)` →
  `removeService` + null it), then
  `mdnsHttpService = MDNS.addService("http", "tcp", httpServePort)`.
- `handleHttpStop` and the restart path at main.cpp:1067: if
  `mdnsHttpService` → `MDNS.removeService(mdnsHttpService)` + null it.
- Instance name = hostname (library default). No TXT records.

### 4. Surfaces

- `info` gains `mDNS: <hostname>.local` (from settings; no liveness
  probe).
- Docs: `Documentation/Commands.md` — `settings hostname <name>` in the
  settings command list, plus one line under `serve`: announced via
  mDNS as `_http._tcp` while running.

## Verification

- Gates: `python -m platformio run -e nodemcuv2` → SUCCESS;
  `python -m platformio test -e native` → 177/177 (unchanged).
- Device (nodemcuv2, COM3):
  1. Boot on saved Wi-Fi → PC `ping harixos-<chipid>.local` resolves.
  2. `serve <file>` → PC `curl http://harixos-<chipid>.local:<port>/...`
     succeeds; `stop` → service removed (re-`serve` still works).
  3. `settings hostname test1` → saved, restarts responder, PC resolves
     `test1.local`.
  4. Wi-Fi down/up (power-cycle AP or `wifi disconnect` + reconnect) →
     name resolves again without reboot.
  5. `settings show` displays the hostname; `info` shows the mDNS line.

## Non-Goals

- AP-mode advertising, mDNS service querying (client role), TXT
  records, an on/off toggle (always on when connected), native unit
  tests for one-line defaults, DNS-SD browser-browse verification
  beyond ping/curl.
