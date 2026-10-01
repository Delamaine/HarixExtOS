# IoT Integration (MQTT/HA + post + onchange) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make HarixOS a LAN-trusted Home Assistant node — MQTT telemetry/discovery out, raw shell in over one bounded command queue, an HTTP `post` webhook command, and persistent pin-change triggers.

**Architecture:** Approach 1 from the spec: an `src/kernel/iot/` service with `begin()`/`update()` hooked into `setup()`/`loop()`, one clock-free inbound queue shared by MQTT `shell/in` and `onchange`, drained through `ScriptEngine::executeCommand` with a capture stream so every command gets exactly one published reply. Pure logic (queue, debounce) is host-testable on the native env; everything else is device-verified over serial + a local Mosquitto.

**Tech Stack:** ESP8266 Arduino core (PIO `nodemcuv2`), PubSubClient 2.8 (first `lib_deps` entry), Unity tests on `platformio test -e native`, LittleFS, Python serial scripts for device verification.

**Spec:** `docs/superpowers/specs/2026-10-01-iot-mqtt-integration-design.md` — the plan argues from the spec; read both.

## Global Constraints

- Plain TCP only (no TLS anywhere): broker `:1883`, `post` plaintext HTTP.
- QoS 0 everywhere, clean session, keepalive 60 s, socket timeout 3 s, `setBufferSize(1024)` (spec §3).
- Reconnect backoff: 5 s doubling, cap 60 s; refuse connect below 10 KB free heap (spec §8).
- Inbound queue: 4 lines × 80 chars, drop-oldest + counter (spec §4).
- Reply contract: exactly one `shell/out` per `shell/in` line — captured output, `ERROR: <msg>`, or `ok`; chunk at 900 B, cap 4000 B + `…[truncated]` (spec §5).
- Debounce 50 ms; ISR touches only a `volatile uint32_t` bitmask (spec §6).
- Telemetry default interval 60 s (0 = off) but always publishes once after connect (spec §3).
- Settings keys exactly: `mqtt_enabled`, `mqtt_host`, `mqtt_port`, `mqtt_user`, `mqtt_pass`, `mqtt_prefix`, `mqtt_interval`, `mqtt_discover`; prefix default `harixos/<chipid>` (spec §2).
- Rules file `/harixos/onchange.cfg`, one rule per line `<pin> <rising|falling|both> <line…>` (spec §2).
- Both dispatchers expose identical surfaces: shell handlers in `main.cpp`, script keywords in `ScriptEngine` — script handlers delegate to shared functions, never duplicate bodies (script_engine.cpp:240-242 comment).
- No JSON library — payloads built by manual `String` concatenation (spec Risks).
- Pinned dependency: `knolleary/PubSubClient@^2.8`.
- Build/flash: `python -m platformio run -e nodemcuv2 --target upload` (COM3; close serial monitor first). Host tests: `python -m platformio test -e native`.

## Review Focus

Inputs/conditions the spec implies that no single happy-path test covers. Each line names the test that pins it, in the task that owns the code.

1. **Queue overflow burst** (more than 4 lines arrive while one executes) — expected: oldest dropped, counter increments, nothing crashes → Task 1 host test `test_overflow_drops_oldest_and_counts`.
2. **`shell/in` line ≥ 80 chars or empty** — expected: immediate `ERROR: line too long` reply / silent ignore, never enqueued → Task 7 device step with a crafted 100-char payload.
3. **Retained message on `shell/in`** — expected: PubSubClient can't see the retain flag, so a retained command re-executes on subscribe; the operational rule "never publish retained to shell/in" must be documented and the HA example YAML must not set retain → Task 9 docs check (grep `retain` in `Documentation/ha-mqtt.md` example).
4. **Heap guard deny path** — expected: below 10 KB the service refuses with a printed reason and a status counter, no connect attempt → Task 5 device step with `kMinHeap` temporarily overridden high, then reverted.
5. **Broker/WiFi loss and recovery** — expected: backoff messages, dropped-telemetry counter, automatic recovery on restore, no crash → Task 5 device step (stop Mosquitto, restart it).
6. **Blocking script vs MQTT** — expected: MQTT pauses during a 30 s script and emits exactly one telemetry tick afterward (no catch-up burst) → Task 6 device step using the while-loop cap script.
7. **Non-2xx and refused `post`** — expected: `ERROR: HTTP <code>` / connection error, never a false success → Task 4 device step (listener returns 500, then closed port).
8. **Pin storm during a blocking script** — expected: bitmask coalesces to a single rule execution → Task 8 device step (toggle pin rapidly while a `delay 3000` script runs).

---

### Task 1: InboundQueue (pure, host-tested)

**Files:**
- Create: `src/kernel/iot/inbound_queue.h`, `src/kernel/iot/inbound_queue.cpp`
- Test: `test/test_inbound_queue/test_inbound_queue.cpp`
- Modify: `platformio.ini:60` (native `build_src_filter`)

**Interfaces:**
- Consumes: nothing.
- Produces (Task 7 drains it, Task 8's onchange pushes into it):
  ```cpp
  namespace harixos { namespace iot {
  class InboundQueue {
   public:
    static constexpr size_t kLineMax = 80;   // incl. NUL -> 79 usable chars
    static constexpr size_t kCapacity = 4;
    bool push(const char *line, size_t len); // false if len==0 || len>=kLineMax
    bool pop(char *out);                     // out[kLineMax], false when empty
    size_t size() const;
    uint32_t dropped() const;                // overflow count since reset()
    void reset();
  };
  InboundQueue &inbound();                   // process-wide singleton accessor
  }}
  ```
  No Arduino includes, no `String` (native env). Push on a full queue drops the oldest line and increments `dropped()`.

- [ ] **Step 1: Write the failing tests**

```cpp
#include <unity.h>

#include <cstdio>
#include <cstring>

#include "kernel/iot/inbound_queue.h"

using harixos::iot::InboundQueue;

void setUp(void) {}
void tearDown(void) {}

static char out[InboundQueue::kLineMax];

static void test_push_pop_fifo(void) {
  InboundQueue q;
  TEST_ASSERT_TRUE(q.push("first", 5));
  TEST_ASSERT_TRUE(q.push("second", 6));
  TEST_ASSERT_EQUAL_UINT(2, q.size());
  TEST_ASSERT_TRUE(q.pop(out)); TEST_ASSERT_EQUAL_STRING("first", out);
  TEST_ASSERT_TRUE(q.pop(out)); TEST_ASSERT_EQUAL_STRING("second", out);
  TEST_ASSERT_FALSE(q.pop(out));
}

static void test_rejects_empty_and_overlong(void) {
  InboundQueue q;
  char line[100]; memset(line, 'x', sizeof(line)); line[99] = '\0';
  TEST_ASSERT_FALSE(q.push("", 0));
  TEST_ASSERT_FALSE(q.push(line, 99));          // >= kLineMax
  TEST_ASSERT_EQUAL_UINT(0, q.size());
  char maxok[InboundQueue::kLineMax];
  memset(maxok, 'y', sizeof(maxok) - 1); maxok[InboundQueue::kLineMax - 1] = '\0';
  TEST_ASSERT_TRUE(q.push(maxok, InboundQueue::kLineMax - 1));  // 79 chars fits
}

static void test_overflow_drops_oldest_and_counts(void) {
  InboundQueue q;
  char l[8];
  for (int i = 0; i < 6; i++) {
    snprintf(l, sizeof(l), "%d", i);
    TEST_ASSERT_TRUE(q.push(l, strlen(l)));
  }
  TEST_ASSERT_EQUAL_UINT(InboundQueue::kCapacity, q.size());
  TEST_ASSERT_EQUAL_UINT(2, q.dropped());
  TEST_ASSERT_TRUE(q.pop(out)); TEST_ASSERT_EQUAL_STRING("2", out);  // 0,1 dropped
}

static void test_wrap_around(void) {
  InboundQueue q;
  char a[80];
  for (int cycle = 0; cycle < 3; cycle++) {
    for (unsigned i = 0; i < InboundQueue::kCapacity; i++) {
      snprintf(a, sizeof(a), "c%d-i%u", cycle, i);
      TEST_ASSERT_TRUE(q.push(a, strlen(a)));
    }
    for (unsigned i = 0; i < InboundQueue::kCapacity; i++) {
      snprintf(a, sizeof(a), "c%d-i%u", cycle, i);
      TEST_ASSERT_TRUE(q.pop(out));
      TEST_ASSERT_EQUAL_STRING(a, out);
    }
  }
  TEST_ASSERT_EQUAL_UINT(0, q.dropped());
}

static void test_reset(void) {
  InboundQueue q;
  q.push("x", 1); q.push("y", 1); q.pop(out);
  q.reset();
  TEST_ASSERT_EQUAL_UINT(0, q.size());
  TEST_ASSERT_EQUAL_UINT(0, q.dropped());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_push_pop_fifo);
  RUN_TEST(test_rejects_empty_and_overlong);
  RUN_TEST(test_overflow_drops_oldest_and_counts);
  RUN_TEST(test_wrap_around);
  RUN_TEST(test_reset);
  return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m platformio test -e native --filter test_inbound_queue`
Expected: FAIL — `kernel/iot/inbound_queue.h: No such file or directory`.

- [ ] **Step 3: Implement `InboundQueue` in `src/kernel/iot/inbound_queue.cpp`**

Ring buffer of `char lines[kCapacity][kLineMax]` with head/tail/count and a `uint32_t dropped_`; `inbound()` returns a function-local static instance. `platformio.ini` native env: append `+<kernel/iot/inbound_queue.cpp>` to `build_src_filter`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m platformio test -e native --filter test_inbound_queue`
Expected: PASS (5/5).

- [ ] **Step 5: Full native suite + commit**

Run: `python -m platformio test -e native` → all suites PASS.
```bash
git add src/kernel/iot/ test/test_inbound_queue/ platformio.ini
git commit -m "feat(iot): bounded inbound command queue with drop-oldest accounting"
```

---

### Task 2: Pin debounce/edge logic (pure, host-tested)

**Files:**
- Create: `src/kernel/iot/onchange_logic.h`, `src/kernel/iot/onchange_logic.cpp`
- Test: `test/test_onchange/test_onchange.cpp`
- Modify: `platformio.ini:60` (add `+<kernel/iot/onchange_logic.cpp>`)

**Interfaces:**
- Consumes: nothing.
- Produces (Task 8's `onchange.cpp` drives it):
  ```cpp
  namespace harixos { namespace iot {
  enum class EdgeMode : uint8_t { Rising, Falling, Both };
  struct DebounceState { bool initialized; bool previousRaw; bool stableLevel; uint32_t lastTransitionMs; };
  // Feeds one raw sample. Returns true exactly when the *stable* level
  // changes (raw held differs from stable for kDebounceMs=50), then
  // *newLevel holds the new stable level.
  bool feedEdge(DebounceState &s, bool rawLevel, uint32_t nowMs, bool *newLevel);
  // True when a stable-level change from oldLevel to newLevel satisfies mode.
  bool edgeMatches(EdgeMode mode, bool oldLevel, bool newLevel);
  ```
  `lastTransitionMs` tracks the last time raw *changed* (not time spent differing), so a chattering input keeps restarting the window — the classic debounce rule. Task 8 parses the mode string itself (needs `String`, stays out of this header).

- [ ] **Step 1: Write the failing tests** (`test/test_onchange/test_onchange.cpp`)

Five tests, exact assertions (all times in ms, Algorithm B below):
- `test_first_sample_initializes_without_firing`: fresh state, feed `false` at t=1000000 → returns false.
- `test_change_after_window`: init `false` at t=0; feed `true` at t=10 → false; feed `true` at t=60 → **true**, `*newLevel==true`; feed `true` at t=70 → false.
- `test_transition_restarts_window`: init `false` t=0; `true` t=10 → false; `true` t=30 → false (30−10=20 < 50); `true` t=80 → **true** (80−10=70 ≥ 50); `true` t=90 → false.
- `test_raw_blip_below_window_is_ignored`: init `false` t=0; `true` t=10 → false; `false` t=40 → false (back to stable, no fire); `true` t=100 → false (window restarted); `true` t=160 → **true** (160−100=60 ≥ 50).
- `test_edge_matches_modes`: `edgeMatches(Rising, false, true)` true; `(Rising, true, false)` false; `(Falling, true, false)` true; `(Falling, false, true)` false; `(Both, …)` true for either direction.

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m platformio test -e native --filter test_onchange`
Expected: FAIL — header not found.

- [ ] **Step 3: Implement `feedEdge` / `edgeMatches` in `src/kernel/iot/onchange_logic.cpp`**

Algorithm B (state: `initialized`, `previousRaw`, `stableLevel`, `lastTransitionMs`):
1. Not initialized → `previousRaw = stableLevel = raw`, `lastTransitionMs = now`, return false.
2. If `raw != previousRaw` → `previousRaw = raw`, `lastTransitionMs = now`.
3. If `raw != stableLevel && now - lastTransitionMs >= 50` → `stableLevel = raw`, set `*newLevel`, return true.
4. Else return false.
All time math uses unsigned subtraction (wrap-safe, same idiom as `block_stack.cpp:101`). `edgeMatches`: Rising → `!old && new`; Falling → `old && !new`; Both → `old != new`. Add the file to `build_src_filter`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m platformio test -e native --filter test_onchange` → PASS.

- [ ] **Step 5: Full native suite + commit**

```bash
git add src/kernel/iot/onchange_logic.* test/test_onchange/ platformio.ini
git commit -m "feat(iot): pure edge/debounce state machine for pin triggers"
```

---

### Task 3: MQTT settings fields

**Files:**
- Modify: `src/apps/settings/settings.h:7-16` (AppSettings), `src/apps/settings/settings.cpp` (load after `cpufreq` parse at :76-88, save at :96-102, print at :111-116)

**Interfaces:**
- Consumes: existing `indexOf` first-occurrence parse pattern (settings.cpp:22-66).
- Produces (Task 5 reads these):
  ```cpp
  // settings.h, AppSettings members:
  bool mqttEnabled = false;
  String mqttHost = "";
  uint16_t mqttPort = 1883;
  String mqttUser = "";
  String mqttPass = "";
  String mqttPrefix = "";      // empty -> resolved to "harixos/<chipid>" at load
  uint32_t mqttInterval = 60;  // seconds; 0 = off
  bool mqttDiscover = true;
  ```
  Load: parse `mqtt_enabled=on|off`, `mqtt_host`, `mqtt_port` (toInt, only if >0), `mqtt_user`, `mqtt_pass`, `mqtt_prefix`, `mqtt_interval` (toInt ≥0), `mqtt_discover=on|off`. If `mqttPrefix` empty after parse → `"harixos/" + String(ESP.getChipId(), HEX)`. Save appends all eight lines after `cpufreq=`. Print adds: `MQTT: on/off  <host>:<port>  prefix=<prefix>  interval=<n>s  discover=on/off` — password never printed (follow `wifiSSID` line style).

- [ ] **Step 1: Implement fields + load/save/print** in the three locations above.

- [ ] **Step 2: Build both environments**

Run: `python -m platformio run -e nodemcuv2` and `python -m platformio test -e native`
Expected: both exit 0 (native unaffected but guards the shared header).

- [ ] **Step 3: Device check**

Flash, run `settings show` → new `MQTT:` line appears with defaults (`off`, empty host, prefix resolved). Run `settings save`, `cat /harixos/settings.cfg` → eight `mqtt_*` lines present.
```bash
git add src/apps/settings/
git commit -m "feat(settings): persist mqtt_* connection fields"
```

---

### Task 4: `post` webhook command

**Files:**
- Modify: `src/utils/http/http_downloader.h` (add `post`), `src/utils/http/http_downloader.cpp` (implement), `src/main.cpp` (shell dispatch `else if (command == F("post"))` in the tokenized chain near :1967 + handler beside `handleNotepad` :607), `src/api/script_engine.cpp` (one new dispatch elif + `handleIotCommand`), `src/api/script_engine.h` (declare `ApiResult handleIotCommand(const String &name, const String &args, Stream &output);`), `src/api/script_engine.cpp:printHelp` (~:659)

**Interfaces:**
- Consumes: `HttpDownloader::parseUrl`, `isWiFiConnected()` (http_downloader.h:38,21); `ApiResult` (api_types.h:27).
- Produces (Task 7/8 remote callers use it; docs Task 9):
  ```cpp
  // http_downloader.h
  static harixos::api::ApiResult post(const String &url, const String &body,
                                      const String &contentType = "application/json",
                                      uint32_t timeoutMs = 5000);
  // 2xx -> API_OK, message "HTTP <code>"; non-2xx -> API_ERROR, "HTTP <code>";
  // WiFi down / connect fail / malformed URL -> API_ERROR with reason.
  ```
  Command surface (both dispatchers): `post <url> <body> [content-type]`.
  Script dispatch: `else if (cmd.name == "post") { return handleIotCommand(cmd.name, cmd.args, output); }` — the single IoT handler group (mirrors `handleSystemValueCommand`), declared in `script_engine.h`, dispatching on `name` to `post` (and later `mqtt`/`onchange`). Shell handler `handlePost(const TokenizedLine &cmd)` prints the same `result.message`. Both delegate to `HttpDownloader::post` — no duplicated HTTP logic.

- [ ] **Step 1: Implement `HttpDownloader::post`** — plaintext `WiFiClient` + `HTTPClient` following the existing GET structure in `http_downloader.cpp:84-107` (plain variant); manual request: `POST path HTTP/1.1`, `Host`, `Content-Type`, `Content-Length`, body. Non-2xx is NOT an exception — return `HTTP <code>`.

- [ ] **Step 2: Wire shell + script dispatch + printHelp line** (`post <url> <body> [ctype]  HTTP POST webhook`).

- [ ] **Step 3: Build** `python -m platformio run -e nodemcuv2` → exit 0.

- [ ] **Step 4: Device test — success, 500, refused**

Start a local listener (`python -m http.server 8000` returns 501 for POST — acceptable as non-2xx), then also run a 15-line inline listener that echoes `HTTP/1.1 200 OK` / `HTTP/1.1 500 Boom` on demand. Expected:
- `post http://<pc-ip>:8000 {"a":1}` → `HTTP 501` (or `HTTP 200` from custom listener)
- listener returning 500 → `ERROR: HTTP 500`
- `post http://<pc-ip>:9999 x` (closed port) → `ERROR: …connection…`
- from a script: same output shape.

- [ ] **Step 5: Commit**
```bash
git add src/utils/http/ src/main.cpp src/api/script_engine.*
git commit -m "feat(iot): post command - HTTP webhook via shared HttpDownloader::post"
```

---

### Task 5: MQTT service core (connect, status, pub)

**Files:**
- Create: `src/kernel/iot/mqtt_service.h`, `src/kernel/iot/mqtt_service.cpp`
- Modify: `platformio.ini` (`lib_deps = knolleary/PubSubClient@^2.8`), `src/main.cpp` (shell handlers `mqtt` beside `handlePost`; `setup()` tail calls `harixos::iot::begin()`; `loop()` after `systemScheduler.update()` at :2643 calls `harixos::iot::update()`), `src/api/script_engine.cpp/.h` (`mqtt` elif → existing `handleIotCommand` from Task 4), help line.

**Interfaces:**
- Consumes: `AppSettings mqtt*` (Task 3), `InboundQueue &inbound()` (Task 1; drained starting Task 7 — leave the drain call out until then), `ScriptEngine::executeCommand` precedent at scheduler.cpp:94.
- Produces (Tasks 6-8 extend/drive):
  ```cpp
  namespace harixos { namespace iot {
  void begin();    // load settings, WiFiClient+PubSubClient setup:
                   // setKeepAlive(60), setSocketTimeout(3), setBufferSize(1024),
                   // LWT topic <prefix>/availability payload "offline" retained; no connect.
  void update();   // if enabled: WiFi check -> backoff connect (5s..60s) ->
                   // publish availability "online" retained -> client.loop().
  bool isConnected();
  harixos::api::ApiResult publishRaw(const String &topic, const String &payload,
                                     bool retained = false);  // topic verbatim
  String statusText();  // see format below
  // kMinHeap = 10240; counters: connectFailures, publishFailures, droppedTelemetry
  }}
  ```
  `statusText()` format (one line each): `connected: yes|no`, `heap: <free> (guard: allow|deny, min 10240)`, `config: <host>:<port> prefix=<p> interval=<n>s discover=<on|off> enabled=<on|off>` (user shown, **pass masked `***`**), `counters: connect=<n> publish=<n> telemetry-drop=<n> queue-drop=<n>`.
  Shell/script surface: `mqtt status`, `mqtt start` (sets `mqttEnabled=true` in memory + saves? — **no**: `start`/`stop` toggle runtime state AND persist `mqtt_enabled` via `saveSettings`), `mqtt stop`, `mqtt pub <topic> <payload>` (verbatim topic, `publishRaw`; not connected → error `mqtt: not connected`).

- [ ] **Step 1: Add PubSubClient to `lib_deps` + implement `mqtt_service.{h,cpp}` core** (begin/update/status/publishRaw per Interfaces; backoff state = static `nextAttemptMs`, doubling 5→60 s; `WiFi.status() != WL_CONNECTED` resets attempt timer).

- [ ] **Step 2: Wire shell + script `mqtt` handlers + `setup()`/`loop()` hooks + printHelp line.**

- [ ] **Step 3: Build** → exit 0 (also proves lib_deps resolves).

- [ ] **Step 4: Device — connect + status + pub**

With local Mosquitto up (credentials in `settings.cfg`: edit via `notepad /harixos/settings.cfg`, then `settings reload`): expected on boot — `mqtt status` shows `connected: yes`, counters 0; `mosquitto_sub -v -t 'harixos/<id>/availability'` → `offline` (LWT, seen because sub started before connect) then `online` retained; `mqtt pub test/topic hello` → subscriber sees `hello`; `mqtt stop` → disconnect + `mqtt_enabled=off` persisted; `mqtt start` → reconnect.
Also (spec §10): set `mqtt_enabled=on` with empty `mqtt_host` → boot/status prints `mqtt: no host configured` refusal, no connect attempts, `connect` counter unchanged.

- [ ] **Step 5: Device — heap guard deny path (Review Focus 4)**

Temporarily set `kMinHeap = 4000000` locally, rebuild+flash → boot prints refusal (`mqtt: heap guard: 33040 < 4000000, not connecting`), `mqtt status` shows `guard: deny`. Revert to `10240`, rebuild+flash, confirm `guard: allow` and connected. (The revert is part of this step; the final tree must contain `10240`.)

- [ ] **Step 6: Device — loss & recovery (Review Focus 5)**

Stop Mosquitto → backoff messages every 5→10→20 s, `connect` counter climbs, no crash. Restart Mosquitto → reconnects, `online` re-published. Toggle router/WiFi off/on (or `wifi disconnect` + `wifi connect`) → same cycle.
```bash
git add src/kernel/iot/mqtt_service.* platformio.ini src/main.cpp src/api/script_engine.*
git commit -m "feat(iot): MQTT service core - backoff connect, LWT, status, pub"
```

---

### Task 6: Telemetry + HA discovery

**Files:**
- Modify: `src/kernel/iot/mqtt_service.{h,cpp}` (telemetry tick + discovery builder)

**Interfaces:**
- Consumes: `publishRaw` (Task 5), `SystemAPI::getUptime()` (ms), `ESP.getFreeHeap()`, `WiFi.RSSI()`, `analogRead(A0)`.
- Produces (Task 9 documents them):
  - `<prefix>/telemetry` payload: `{"heap":<u>,"uptime":<u>,"rssi":<i>,"adc":<u>}` — `uptime` in **seconds**.
  - Tick rule: after a successful connect publish telemetry immediately, then every `mqttInterval` s (skip if 0 after the connect-time publish); after any blocking gap emit at most one tick (compare against `lastTickMs`, reset forward by one interval — never loop-catch-up).
  - Discovery (only when `mqttDiscover`): retained configs at `homeassistant/sensor/harixos_<chipid>_<key>/config` for `heap` (unit `B`), `uptime` (unit `s`, `dev_cla: duration`), `rssi` (unit `dBm`), `adc` (no unit), each with `stat_t` = `<prefix>/telemetry`, `val_tpl` extracting the JSON key, `uniq_id` = `harixos_<chipid>_<key>`, `name` = `<chipid> <key>`, and a minimal `dev` block (`ids: ["harixos_<chipid>"]`). Manual String concatenation.

- [ ] **Step 1: Implement tick + discovery in `update()`** (discovery publishes once after each successful connect, before telemetry). Guard per spec §10: if `ESP.getFreeHeap() < 8192` when a tick or discovery build starts, skip it and increment `droppedTelemetry` — never build `String` payloads on a starved heap.

- [ ] **Step 2: Build + flash.**

- [ ] **Step 3: Device — subscriber sees payloads**

`mosquitto_sub -v -t 'homeassistant/#' -t 'harixos/<id>/#'` → four retained discovery configs + telemetry JSON on connect + one per interval (set `mqtt_interval=5` for the test).
Verify `val_tpl` extracts `heap` from the JSON (subscribe shows HA-style parsing only in HA; locally assert template correctness by eye against payload keys).

- [ ] **Step 4: Device — no catch-up burst (Review Focus 6)**

Set `mqtt_interval=5`, run `run /harixos/wt_cap.hx`-style 30 s script (or `delay 30000` via script) → during the block zero ticks; after completion exactly one tick, not a burst.

- [ ] **Step 5: Device — low-heap skip path (spec §10)**

Temporarily raise the 8192 guard to `4000000` locally, rebuild+flash → ticks stop, `mqtt status` `telemetry-drop` counter climbs, no crash. Revert to `8192`, rebuild+flash, confirm ticks resume and the counter stops climbing. (Revert is part of this step; final tree must contain `8192`.)
```bash
git add src/kernel/iot/mqtt_service.cpp
git commit -m "feat(iot): telemetry interval + HA discovery configs"
```

---

### Task 7: Drain loop, StringStream, reply contract

**Files:**
- Create: `src/utils/string_stream.h` (header-only `StringStream : public Stream`)
- Modify: `src/kernel/iot/mqtt_service.cpp` (MQTT callback + drain in `update()`), `src/kernel/iot/mqtt_service.h` (`kReplyChunk=900`, `kReplyCap=4000` constants)

**Interfaces:**
- Consumes: `InboundQueue &inbound()` (Task 1), `ScriptEngine::executeCommand(const String&, Stream&)` (script_engine.cpp:472), `publishRaw` (Task 5), `mqtt`/`post` keywords already wired (Tasks 4-5).
- Produces: the reply contract used by docs/tests: callback on `<prefix>/shell/in` → reject empty / `len >= 80` (publish `ERROR: line too long` immediately) else `inbound().push(payload, len)`; drain per `update()`: pop → `StringStream capture; ApiResult r = ScriptEngine::executeCommand(line, capture);` reply = (`r.isError() ? "ERROR: " + r.message : (capture text empty ? "ok" : capture text))` → publish in chunks of ≤900 B to `<prefix>/shell/out`; total >4000 B → truncate + `…[truncated]`.
  `StringStream` (header-only): `String buf; int write(uint8_t) override;` + trivial `available/read/peek/flush` no-ops.

- [ ] **Step 1: Implement `StringStream` + callback + drain.**

- [ ] **Step 2: Build + flash.**

- [ ] **Step 3: Device — contract matrix**

Publish to `shell/in` and watch `shell/out`:
- `heap` → numeric output (not `ok`)
- `nosuchcmd` → `ERROR: …Unknown…`
- `#comment`-equivalent silent command (e.g. `set x = 1`) → `ok`
- two commands in quick succession → both replies, in order, not interleaved
- 100-char line → immediate `ERROR: line too long` (Review Focus 2), empty payload → nothing
- chunking: `write /harixos/big.txt <1500 chars>` then `cat /harixos/big.txt` → 2 messages ≤900 B, then verify cap path with a >4000 B file (`…[truncated]`).
- **ordering**: `ls /harixos` followed immediately by `heap` → reply 1 is the listing, reply 2 the heap value.

- [ ] **Step 4: Commit**
```bash
git add src/utils/string_stream.h src/kernel/iot/
git commit -m "feat(iot): drain shell/in through script dispatcher with single reply contract"
```

---

### Task 8: `onchange` rules — GPIO mode tracking, ISR, persistence

**Files:**
- Modify: `src/api/gpio_api.h/.cpp` (mode tracking), `src/kernel/iot/onchange.h/.cpp` (create: rules file, ISR, wiring), `src/main.cpp` (shell `onchange` handler + `iot::onchangeBegin()`/`update()` hooks next to mqtt hooks), `src/api/script_engine.cpp/.h` (`onchange` keyword), `platformio.ini` (nothing — `onchange.cpp` is Arduino-tainted, NOT added to native filter; only `onchange_logic.cpp` is, from Task 2)

**Interfaces:**
- Consumes: `feedEdge`/`edgeMatches`/`DebounceState`/`EdgeMode` (Task 2), `InboundQueue &inbound()` (Task 1), `GpioAPI` (gpio_api.h:10), mqtt drain already live (Task 7).
- Produces (docs Task 9):
  ```cpp
  // gpio_api.h — mode tracking (ESP8266 has no pinMode readback):
  static harixos::api::ApiResult getMode(uint8_t pin, uint8_t &mode); // API_ERROR "GPIO<n> mode unknown" if never set
  // setMode/write/pulse record into a static uint8_t table indexed by pin (0..16, 255=unknown)

  // onchange.h
  namespace harixos { namespace iot {
  void begin();   // load /harixos/onchange.cfg, attachInterruptArg per valid rule
  void update(uint32_t nowMs);  // scan ISR bitmask -> feedEdge -> edgeMatches -> inbound().push(rule.line)
  harixos::api::ApiResult addRule(uint8_t pin, EdgeMode mode, const String &line);
  harixos::api::ApiResult removeRule(uint8_t pin);
  void listRules(Stream &out);
  }}
  ```
  Command surface (both dispatchers): `onchange <pin> <rising|falling|both> <line…>`, `onchange off <pin>`, `onchange list`.
  `addRule`: validate pin available + `GpioAPI::getMode` is `INPUT` or `INPUT_PULLUP` (else `API_ERROR` `onchange: GPIO<n> is not an input (set: gpio <n> mode input)`), reject duplicate pin, `attachInterruptArg(pin, isr, (void*)(uintptr_t)pin, mode)` where ISR (`IRAM_ATTR`) sets bit in `volatile uint32_t s_pending`, save file. Rules file line: `<pin> <mode> <line>` (rest of line verbatim); unparsable lines skipped with a boot warning. Limit: 8 rules (static table).

- [ ] **Step 1: GPIO mode tracking** — table + `getMode`; `setMode`/`write`/`pulse` record (implicit-output paths at gpio_api.cpp:46,56,70,85 too). Build.

- [ ] **Step 2: `onchange` module + commands (shell + script keyword + printHelp).**

- [ ] **Step 3: Build + flash.**

- [ ] **Step 4: Device — validation**

- `onchange 4 rising print hi` with GPIO4 never configured → clear error, no rule saved.
- `gpio 4 mode input` then same command → `Rule saved`; `onchange list` shows it; file exists.
- duplicate pin → error.

- [ ] **Step 5: Device — trigger, debounce, persistence**

- Jumper GPIO4 to GND briefly (or `gpio 4 mode output`… **no** — rule requires input; use a second free pin toggled by wire, or press/reset on a wired button; fallback: `digitalWrite` trick via `pulse` is disallowed — use a physical short with a jumper) → rule line executes, `harixos/<id>/event/4` shows `0`, reverse → `1`.
- Rapid connect/disconnect ≥10× within 1 s → at most a couple of executions (50 ms debounce), not 10.
- Reboot → `onchange list` still shows the rule, still fires (rules re-arm after GPIO init).
- Hand-edit `/harixos/onchange.cfg` to add a line for a non-existent pin (`99 both print ghost`) plus keep the valid rule → reboot prints one warning for pin 99, the valid rule still works (spec §10).
- Review Focus 8: start `delay 3000` script, toggle pin 5× during it → after script ends, rule executed **once** (bitmask coalescing).
- Physical note: triggering edges needs a jumper wire (GPIO4 ↔ GND or a button); there is no software-only path because rules require input mode.
```bash
git add src/api/gpio_api.* src/kernel/iot/onchange.* src/main.cpp src/api/script_engine.*
git commit -m "feat(iot): onchange pin rules - validated, debounced, persistent"
```

---

### Task 9: Documentation + help

**Files:**
- Create: `Documentation/ha-mqtt.md`
- Modify: `SCRIPT-REFERENCE.md` (new `mqtt` / `post` / `onchange` section beside the while section), `Documentation/Commands.md` (command list entries), `README.md` (feature bullet), `src/api/script_engine.cpp` `printHelp`, `src/main.cpp` `handleHelp` topic list (add `mqtt` topic + main-list lines near :1789-1806)

**Interfaces:**
- Consumes: every surface fixed by Tasks 4-8.
- Produces: user-facing truth; **no code behavior changes.**

- [ ] **Step 1: Write `Documentation/ha-mqtt.md`** — Mosquitto setup, settings.cfg keys, topic table, HA MQTT YAML example (sensors from discovery; an automation publishing to `shell/in` **without `retain`** — Review Focus 3), raw-shell trust warning (LAN-only, broker creds = device ownership), `onchange` examples, `post` webhook example.

- [ ] **Step 2: Update SCRIPT-REFERENCE/Commands/README/help** — exact command syntax from Tasks 4/5/8; explicitly document: sequential replies, no retained publishing to `shell/in`, QoS 0 loss tolerance, queue overflow drop-oldest, script-blocking behavior (same caveat as while loops).

- [ ] **Step 3: Verify docs against code**

Run: `Select-String -Path Documentation\ha-mqtt.md -Pattern 'retain'` → example must contain `retain: false` or no retain key (never `retain: true` on `shell/in`).
Run: `python -m platformio run -e nodemcuv2` → exit 0 (help text compiles).
Cross-check every documented flag against the Interfaces blocks above.
```bash
git add Documentation/ SCRIPT-REFERENCE.md README.md src/api/script_engine.cpp src/main.cpp
git commit -m "docs(iot): HA/MQTT setup guide, command reference, help text"
```

---

### Task 10: Acceptance + regression suite

**Files:**
- Create: `test/scripts/iot_device_test.py` (COM3 driver, notepad-based multi-line writes like `whiletest.py`)

**Interfaces:**
- Consumes: all Tasks 1-8 surfaces; Mosquitto reachable at the settings host; a local HTTP listener.
- Produces: the spec's Acceptance Criteria 1-8 evidence.

- [ ] **Step 1: Run host regression**

Run: `python -m platformio test -e native` → ALL suites pass (incl. Tasks 1-2 additions).

- [ ] **Step 2: Write and run `iot_device_test.py`** covering spec acceptance 2-7:
boot connect + `availability=online` retained; `mqtt status` counters/heap; matrix from Task 7 (order, `ERROR:`, `ok`, chunking, over-long, empty); telemetry on interval + discovery on `homeassistant/#`; `onchange` fire + debounce + reboot persistence; `post` 200/500/refused; bad-credential backoff (point `mqtt_user` at a wrong password, observe failures, restore); recovery. Print `ALL n CHECKS PASSED` like `whiletest.py`.

- [ ] **Step 3: Device regressions**

Copy the while-loop device test into the repo (it currently lives only in the temp dir): save it as `test/scripts/whiletest.py`, run it from there → ALL 20 CHECKS PASSED. Manual: `schedule add` fires while MQTT connected; powersave after 1 min idle still receives MQTT (post a command after idle → executes). Then a 30-minute soak: `mqtt_interval=5` running while issuing a shell command every minute — heap in `mqtt status` must not trend down more than ~2 KB from baseline at the end.

- [ ] **Step 4: Final tree state + commit**

Confirm `kMinHeap` is `10240` (Task 5 revert), `git status` shows only intended files, no credentials anywhere (`Select-String -Path src\**\*.h -Pattern 'mqttPass = "'` must show only the empty default).
```bash
git add test/scripts/iot_device_test.py test/scripts/whiletest.py
git commit -m "test(iot): device acceptance suite for MQTT/post/onchange"
```
