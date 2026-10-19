# MQTT broker authentication — design spec

Date: 2026-10-20
Status: approved design, awaiting spec review
Scope: Mosquitto (LAN-only) with two static accounts, pattern-generated
per-device accounts and per-client ACLs, protecting the remote shell and
telemetry topics across any number of ESP8266s.

## Grounding

- Device credentials already flow: `mqtt_user` / `mqtt_pass` in
  `/harixos/settings.cfg` are passed to `PubSubClient.connect()` at
  `src/kernel/iot/mqtt_service.cpp:161-169`; `mqtt status` masks the password
  (`mqtt_service.cpp:595-598`). No firmware change is required for the device
  to authenticate — only configuration (`notepad` the file, `settings reload`).
- Topic map read from code: `<prefix>/shell/in` (device subscribes,
  `mqtt_service.cpp:53`), `<prefix>/shell/out` (replies), `<prefix>/availability`
  (LWT + online), `<prefix>/mqtt_enabled`, `<prefix>/telemetry`,
  `<prefix>/relay/<name>/state`, `<prefix>/gpio/<pin>`,
  `<prefix>/sensor/<name>/<qty>`, HA discovery published under
  `homeassistant/{sensor,switch,binary_sensor}/…/config`
  (`mqtt_service.cpp:249,273,319,363,413`), plus the `tele/<topic>` pattern
  used by the `bridge-status` example.
- `mqtt_prefix` defaults to `harixos/<chipid>` (ha-mqtt.md:49). All example
  topics below use the literal `harixos/a59450`; substitute your own prefix
  from `settings show`.
- Credentials are set by editing `/harixos/settings.cfg` (`mqtt_user`,
  `mqtt_pass`) then `settings reload` — there is no `settings mqtt_*`
  subcommand (Commands.md:133-136).

## Threat model

**In scope (what this stops):**
- Any unauthenticated LAN client publishing to `<prefix>/shell/in` — that
  topic executes arbitrary shell commands (`reboot`, `wifi`, `write`, …), so
  today any broker client (or a broker with `allow_anonymous true`) owns the
  device.
- Reads of state/telemetry by unauthenticated clients.
- Accidental command injection from other MQTT integrations.

**Out of scope (explicit non-goals):**
- **Physical/USB access.** Anyone with the cable can flash or dump the ESP8266
  regardless of broker auth (no secure boot / flash encryption on ESP8266).
- **LAN MITM / network-level attackers.** Broker is LAN-only; TLS is the
  documented upgrade path if the broker is ever reachable off-LAN.
- **Per-command authorization.** The broker decides *who may publish*, not
  *which commands the shell obeys*. A compromised `admin` password is full
  shell access by design.
- Console (serial) authentication — tracked separately.

## Account model

Two static accounts plus one pattern-generated account class:

| Username | Purpose | Needs |
|---|---|---|
| `harixos/<chipid>` (pattern) | Each ESP8266 — username **is** the device's `mqtt_prefix` (e.g. `harixos/a59450`) | Everything under its own prefix; discovery; `tele/` |
| `homeassistant` | HA MQTT integration | Read everything; write `shell/in` only (HA buttons/switches) |
| `admin` | You (mosquitto_sub/pub, scripts) | Read everything; write `shell/in` only |

Multi-device is first-class: devices get no static ACL block, so mosquitto's
`pattern` lines apply (a `user` block, when present, overrides patterns).
Adding the Nth device = one `mosquitto_passwd` line + the two `mqtt_user`/
`mqtt_pass` keys on that device — **no ACL edits**.

`admin` deliberately cannot publish retained state topics — the shell inbox
is full control, state spoofing is not needed for legitimate use.

## Broker configuration

`examples/mosquitto/mosquitto.conf` (additions to a stock install):

```conf
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd
acl_file /etc/mosquitto/acl
```

(Windows installs: keep `passwd`/`acl` next to `mosquitto.conf` and use
Windows paths. HA add-on Mosquitto is out of scope per the chosen setup.)

Account creation:

```sh
mosquitto_passwd -c /etc/mosquitto/passwd homeassistant      # -c only for the FIRST user
mosquitto_passwd /etc/mosquitto/passwd admin
mosquitto_passwd /etc/mosquitto/passwd 'harixos/a59450'     # one line per device, as added
```

`examples/mosquitto/acl`:

```conf
user homeassistant
topic read #
topic write harixos/+/shell/in

user admin
topic read #
topic write harixos/+/shell/in

pattern readwrite %u/#
pattern write homeassistant/#
pattern write tele/#
```

Notes:
- Devices have no `user` block, so the `pattern` lines grant each one
  `readwrite <its-username>/#` — with username = `mqtt_prefix`, that is
  exactly its own topic tree (`shell/in` subscribe, `shell/out`,
  `availability`, `relay`, `gpio`, `sensor`, `mqtt_enabled`, `telemetry`).
- `+`/`#` wildcards are legal in ACL topics; `harixos/+/shell/in` covers every
  device's inbox for HA and admin.
- Static `user` blocks override patterns, so HA/admin cannot use the device
  pattern (they write only `shell/in`).
- `tele/#` exists only for the `bridge-status` example pattern (`mqtt pub
  tele/harixos/state …`). Omit the pattern line if you don't use it.
- Username containing `/` is valid MQTT (UTF-8) and accepted by
  `mosquitto_passwd` — quote it on the shell as shown.

## Client setup steps (documented in MQTT-Security.md)

1. **Device** — edit `/harixos/settings.cfg`: `mqtt_user=` = your
   `mqtt_prefix` (e.g. `harixos/a59450`), `mqtt_pass=<that device's
   password>`, `mqtt_host=<broker>`, then `settings reload` (and `mqtt start`
   if not enabled). `mqtt status` must show the user and a masked password.
   **Adding another ESP8266:** `mosquitto_passwd /etc/mosquitto/passwd
   'harixos/<chipid>'`, then the same two keys on that device — nothing on
   the broker changes beyond the password line.
2. **Home Assistant** — in the MQTT integration config, username
   `homeassistant` + its password. Discovery keeps working (device publishes
   `homeassistant/#`, HA subscribes).
3. **You** — `mosquitto_pub/sub -u admin -P …`.

## Recovery (lockout safety)

An unreadable `password_file` or forgotten password must not strand the
broker. Documented escape hatch — temporarily add:

```conf
listener 1884 127.0.0.1
allow_anonymous true
```

restart, fix credentials from localhost, remove the line. ACL mistakes deny
per-topic (broker still accepts connections), so recovery is always possible
from localhost.

## Verification checklist (spec acceptance = checklist written; run on broker)

1. `mosquitto_pub -t harixos/a59450/shell/in -m uptime` (no creds) →
   **Connection refused / not authorized**.
2. `mosquitto_pub -u admin -P wrong …` → authentication fails.
3. `mosquitto_pub -u admin -P … -t harixos/a59450/shell/in -m uptime` →
   device executes; reply visible on `shell/out`.
4. `mosquitto_pub -u admin -P … -t harixos/a59450/relay/fake/state -m on` →
   **not authorized** (admin has no state write).
5. Device `mqtt status` → connected as its prefix username (e.g.
   `harixos/a59450`); telemetry appears on `<prefix>/telemetry`; HA entities
   still discover.
6. Anonymous `mosquitto_sub -t '#`' (no creds) → **not authorized**.
7. Pattern isolation (multi-device proof): as a second device account
   (`-u 'harixos/other'`), publish to `harixos/other/shell/in` → **allowed**;
   publish to `harixos/a59450/shell/in` → **not authorized**.

## Deliverables (implementation scope)

- `Documentation/MQTT-Security.md` — threat model, accounts, config,
  client setup, recovery, verification (the content above, prose form).
- `examples/mosquitto/mosquitto.conf`, `examples/mosquitto/acl` — paste-ready
  files matching the docs byte-for-byte in account names/topics.
- Pointers: `Documentation/ha-mqtt.md` (Security section link),
  `Documentation/Commands.md` mqtt block (one-line pointer),
  `Documentation/README.md` index entry.
- **No firmware changes.** Device-side auth already exists.

## Acceptance criteria

- All three deliverable files exist; account names and topics agree across
  docs and example files.
- Non-goals (USB, TLS, per-command auth) stated explicitly.
- Native suite (238) and `nodemcuv2` build stay green; verification checklist
  runs clean against a real Mosquitto when available.

## Open questions

None — broker (Mosquitto, LAN-only) and account model (device + HA + admin)
chosen by the user during brainstorming.
