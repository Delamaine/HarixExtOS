# IoT Integration: MQTT + Home Assistant, Webhook `post`, and Pin Events

## Goal

Make HarixOS a LAN-trusted Home Assistant node: telemetry and discovery out,
raw shell in over MQTT, an HTTP `post` command for webhooks, and persistent
pin-change triggers — all sharing one inbound command queue and one
output-capture path.

## Current State

- `loop()` runs serial → web server → scheduler → power-profile autoswitch;
  there is no other periodic seam.
- Two dispatchers exist: the shell's (`executeCommand` in `main.cpp`) prints
  straight to `Serial` and returns nothing; `ScriptEngine::executeCommand`
  returns an `ApiResult {isError, message}` and already routes output through
  a `Stream&`. Cron was deliberately wired to the latter ("widen
  `ScriptEngine` keyword list, not a bridge to the shell" — spec 4.3).
- Settings persist as single-valued `key=value` lines parsed by
  first-occurrence `indexOf` (`settings.cpp`); multi-valued data uses
  dedicated files instead (`/harixos/schedule.cfg` precedent).
- HTTP helpers exist (`ESP8266HTTPClient` in `main.cpp` and
  `src/utils/http/http_downloader.cpp`), plaintext and TLS variants.
- `platformio.ini` has no `lib_deps` today; everything is framework or
  vendored.
- Measured budget: 33,040 B heap free with WiFi connected, static RAM 55%,
  app slot 52.3%. TLS is out of reach (mbedTLS handshake ≈ 40 KB).

## Decisions

| Topic | Decision |
|---|---|
| Use case | HA node + unfiltered remote shell; LAN-trust model |
| Broker | Local Mosquitto, plain TCP :1883, user/pass in settings, connect at boot |
| Remote exec | `ScriptEngine::executeCommand` into a capture stream; widen keyword list |
| `onchange` storage | Persistent across reboot, active without scripts |
| Architecture | Approach 1: `src/kernel/iot/` service + one shared bounded queue drained in `loop()` |
| Control entities | None in v1 — discovery publishes sensors only; HA controls via automations on `shell/in` |

## Design

### 1. Components

| Component | File | Responsibility |
|---|---|---|
| MQTT service | `src/kernel/iot/mqtt_service.{h,cpp}` (new) | connect/backoff, `client.loop()`, publish, LWT, HA discovery, telemetry tick |
| Inbound queue | `src/kernel/iot/inbound_queue.{h,cpp}` (new) | bounded FIFO of command lines; clock-free, host-testable |
| Pin events | `src/kernel/iot/onchange.{h,cpp}` (new) | rules file, `attachInterrupt` flagging, debounce → queue |
| Pin-event logic | `src/kernel/iot/onchange_logic.{h,cpp}` (new) | pure edge/debounce state machine (no Arduino), host-testable |
| Webhook | `http_post()` in `src/utils/http/http_downloader.{h,cpp}` | one-shot POST, plaintext first |
| Library | `platformio.ini` | first `lib_deps`: `knolleary/PubSubClient@^2.8`, pinned |

`loop()` gains one line: `harixos::iot::update()` after the scheduler call.

### 2. Settings and rule storage

New `AppSettings` fields (settings.cfg, existing parser): `mqtt_enabled`
(on/off, default off), `mqtt_host` (""), `mqtt_port` (1883), `mqtt_user`,
`mqtt_pass`, `mqtt_prefix` (default `harixos/<chipid>`, resolved at load),
`mqtt_interval` (seconds, default 60, 0 = off), `mqtt_discover` (on/off,
default on). `mqtt status` masks the password.

Pin rules are multi-valued, which the first-occurrence settings parser
cannot represent, so they live in `/harixos/onchange.cfg` — one rule per
line, `<pin> <rising|falling|both> <shell line…>`, auto-saved on change and
reloaded at boot, mirroring `schedule.cfg`.

### 3. Topic scheme and payloads

```
<prefix>/shell/in        commands in (never publish retained to this topic)
<prefix>/shell/out       one response per command (chunked, see §5)
<prefix>/telemetry       {"heap":33040,"uptime":915,"rssi":-71,"adc":342}
<prefix>/availability    "online"/"offline", retained, set as LWT
<prefix>/event/<pin>     "1"/"0" on pin trigger, retained
homeassistant/sensor/<id>/config   discovery, retained, when mqtt_discover=on
```

Discovery entities (all with `uniq_id` = `harixos_<chipid>_<key>`,
`stat_t` = `<prefix>/telemetry` with `val_tpl` extracting the key):
`heap` (unit B), `uptime` (unit s, device_class duration), `rssi` (unit dBm),
`adc` (raw), plus one `binary_sensor` per configured `onchange` pin whose
state topic is `<prefix>/event/<pin>`. Published once after each successful
connect. Telemetry also publishes **once after each connect** regardless of
`mqtt_interval`, so discovery entities are never dead when the interval is
0.

Transport properties: QoS 0 everywhere (PubSubClient limit), clean session,
keepalive 60 s, `setBufferSize(1024)`, socket timeout 3 s.

### 4. Inbound queue

- Capacity **4 lines × 80 chars**, FIFO, drop-oldest with a counter on
  overflow; `reset()` for tests.
- Producers: MQTT `shell/in` callback (copy payload + NUL) and `onchange`
  debounce dispatch. Single consumer: `update()` drains fully each pass.
- Empty payloads and lines over 80 chars are rejected at the producer edge;
  an over-long `shell/in` line gets an immediate `ERROR: line too long`
  response rather than a silent drop.
- Clock-free and Arduino-free so `test_inbound_queue` runs on the native
  env (`build_src_filter` adds the two pure iot files).

### 5. Command execution contract

1. Drain pops one line → `ScriptEngine::executeCommand(line, captureStream)`
   with a memory `Stream`.
2. Exactly one publish to `<prefix>/shell/out`, in queue order, never
   interleaved: captured output; `ERROR: <message>` on `ApiResult` failure;
   `ok` when silent.
3. Output over 900 bytes is chunked into sequential messages; total output
   capped at 4000 bytes with a trailing `…[truncated]`.
4. Retained-message caveat: PubSubClient does not expose the retain flag on
   receive, so "never publish retained to `shell/in`" is an operational rule
   documented for users, not enforced in code.
5. Sequential semantics: MQTT 3.1.1 / PubSubClient has no correlation IDs;
   one command fully completes (and its reply publishes) before the next
   pops.

Keyword widening in `ScriptEngine::executeCommand` (+ `printHelp`): `mqtt`,
`post`, `onchange`. The shell gets the same three commands as native
handlers in `main.cpp`, so both dispatchers expose an identical surface:

```
mqtt status | mqtt start | mqtt stop | mqtt pub <topic> <payload>
post <url> <body> [content-type]        # default application/json
onchange <pin> <rising|falling|both> <line…> | onchange off <pin> | onchange list
```

`mqtt pub` targets the topic verbatim (no prefix added) so users can reach
any broker topic.

### 6. `onchange` behavior

- Registration validates the pin is already in an input mode via the GPIO
  API and errors clearly otherwise; it attaches `attachInterrupt` with the
  chosen mode. The ISR sets one `volatile` bit in a mask — no `String`, no
  heap, no GPIO calls in ISR context.
- `update()` processes raised bits: 50 ms software debounce, then pushes the
  rule's line into the inbound queue. Debounce and edge filtering
  (rising/falling/both vs previous level) live in the pure
  `onchange_logic.{h,cpp}` state machine driven by `nowMs`; `onchange.cpp`
  owns only the Arduino-side wiring (ISR, `attachInterrupt`, rules file).
  Triggers that fire while the system
  was busy (e.g., during a long script) coalesce to a single execution —
  the bitmask holds one bit per pin by design.
- Rules re-arm at boot after GPIO init; `onchange off <pin>` removes and
  rewrites the rules file; the rule's line executes like any other inbound
  command (fire-and-forget; its output follows the normal path only if it
  originated from MQTT — a pin-triggered line just runs).

### 7. `post` command

`http_post(url, body, contentType, timeoutMs=5000)` beside the existing GET
helpers; prints/publishes `HTTP <code>` on completion, connection failure is
an `ApiResult`/shell error. Plaintext HTTP only (consistent with broker
decision).

### 8. Service lifecycle, errors, budget

- `setup()`: if `mqtt_enabled=on`, `begin()` (no blocking connect).
  `update()`: WiFi check → backoff connect (5 s doubling to 60 s cap,
  ≤ 3 s block per attempt) → on success publish availability + discovery →
  `client.loop()` → telemetry tick (skips missed intervals after blocks; no
  catch-up burst) → queue drain → onchange scan.
- Refuse to connect below **10 KB free heap** with a printed reason;
  counters exposed in `mqtt status`: connect failures, publish failures,
  dropped queue lines, dropped telemetry.
- WiFi loss: detect, count telemetry as dropped, restart backoff. No
  offline buffering.
- Blocking scripts pause MQTT exactly as they pause the shell/scheduler
  today — documented behavior, not remedied.
- PubSubClient callback runs synchronously inside `client.loop()` within
  `update()` — same thread as the consumer, no locking.

### 9. Files to modify

New: `src/kernel/iot/mqtt_service.{h,cpp}`, `src/kernel/iot/inbound_queue.{h,cpp}`,
`src/kernel/iot/onchange.{h,cpp}`, `src/kernel/iot/onchange_logic.{h,cpp}`,
`test/test_inbound_queue/`, `test/test_onchange/`, `Documentation/ha-mqtt.md`.

Modified: `platformio.ini` (lib_deps, build_src_filter), `src/main.cpp`
(loop hook, boot init, shell handlers, help), `src/api/script_engine.cpp`
(keywords + printHelp), `src/apps/settings/settings.{h,cpp}` (fields),
`src/utils/http/http_downloader.{h,cpp}` (`http_post`), `SCRIPT-REFERENCE.md`,
`Documentation/Commands.md`, `README.md`.

### 10. Edge cases

- `mqtt_enabled=on` but empty `mqtt_host` → refuse with reason, counter.
- Queue full → drop oldest, count; never blocks the MQTT callback.
- `shell/in` line > 80 chars → immediate `ERROR: line too long` reply.
- Discovery/telemetry `String` build fails low-heap → skip tick, count.
- `post` with WiFi down → immediate error, no retry.
- Reboot with rules present but pin hardware absent → GPIO registration
  fails per-rule with a warning; other rules still arm.
- Power profile powersave: keepalive 60 s tolerates WiFi sleep; verified in
  the device plan.

## Acceptance Criteria

1. Native suites green including `test_inbound_queue` and `test_onchange`.
2. Device: boot connects (or cleanly refuses), `availability=online`
   retained, `mqtt status` shows config + counters + heap.
3. Every `shell/in` command yields exactly one `shell/out` with the §5
   contract, in order, including `run`, `schedule add`, `ls`.
4. Telemetry JSON arrives on interval; discovery configs visible on
   `homeassistant/#`; HA shows the sensors.
5. `onchange` rule fires on pin edge, debounces, publishes `event/<pin>`,
   survives reboot; ISR-safe by construction (bitmask only).
6. `post` reaches a local HTTP listener with correct body/content-type.
7. Bad credentials → backoff with visible errors; recovery after WiFi
   restore; heap guard triggers below 10 KB (code path reviewed, surfaced
   in status).
8. Regressions: existing native suites, the while-loop device test, and
   powersave-mode MQTT receive all pass.

## Testing

- **Host:** queue FIFO/overflow/wrap/reset; debounce + edge filtering as a
  pure `nowMs` state machine (the `BlockStack` pattern).
- **Device:** scripted serial plan covering criteria 2–7 plus regressions
  (criterion 8), using a local Mosquitto and a Python HTTP listener.

## Out of Scope (v1)

TLS, cloud brokers, offline buffering, HA control entities (switches, HA
services), MQTT v5 (correlation, QoS 1/2), multiple brokers, capturing
output of pin-triggered lines, `onchange` while a script owns pins,
connection auth other than user/password.

## Risks

- **Heap contention:** `serve` + `post` + MQTT concurrently is the worst
  case; the 10 KB guard and transient-string policy mitigate but a soak
  test is warranted.
- **First external dependency:** PubSubClient is the de-facto standard but
  pins the project's first `lib_deps` entry — version pinned, license
  (MIT) noted in docs.
- **QoS 0 loss:** commands/telemetry can drop silently on a flaky AP;
  acceptable for LAN v1, documented.
- **Raw shell over MQTT:** anyone with broker credentials owns the device;
  mitigated by LAN trust + credentials in settings.cfg, stated plainly in
  `Documentation/ha-mqtt.md`.
