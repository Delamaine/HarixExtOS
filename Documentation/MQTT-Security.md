# MQTT Broker Security (Mosquitto)

## Overview

This document locks down a LAN-only Mosquitto broker so only authenticated
clients can read HarixOS state or publish to the remote shell topic
`<prefix>/shell/in`. It covers accounts, broker configuration, client setup,
lockout recovery and a verification checklist.

**No firmware change is required.** The device already authenticates with the
`mqtt_user` / `mqtt_pass` keys in `/harixos/settings.cfg`
(`src/kernel/iot/mqtt_service.cpp:161-169`). Paste-ready copies of both
configuration files live in [`examples/mosquitto/`](../examples/mosquitto/).

## Threat model

**In scope (what this stops):**

- Any unauthenticated LAN client publishing to `<prefix>/shell/in` — that
  topic executes arbitrary shell commands (`reboot`, `wifi`, `write`, …), so
  a broker with `allow_anonymous true` gives every LAN client full control of
  the device.
- Reads of state/telemetry by unauthenticated clients.
- Accidental command injection from other MQTT integrations.

**Out of scope (explicit non-goals):**

- **Physical/USB access.** Anyone with the cable can flash or dump the
  ESP8266 regardless of broker auth (no secure boot / flash encryption on
  ESP8266).
- **LAN MITM / network-level attackers.** The broker is LAN-only; TLS is the
  documented upgrade path if the broker is ever reachable off-LAN.
- **Per-command authorization.** The broker decides *who may publish*, not
  *which commands the shell obeys*. A compromised `admin` password is full
  shell access by design.
- Console (serial) authentication — tracked separately.

## Accounts

Two static accounts plus one pattern-generated account class:

| Username | Purpose | Needs |
|---|---|---|
| `harixos/<chipid>` (pattern) | Each ESP8266 — username **is** the device's `mqtt_prefix` (e.g. `harixos/a59450`) | Everything under its own prefix; discovery; `tele/` |
| `homeassistant` | HA MQTT integration | Read everything; write `shell/in` only (HA buttons/switches) |
| `admin` | You (mosquitto_sub/pub, scripts) | Read everything; write `shell/in` only |

Multi-device is first-class: devices get no static ACL block, so Mosquitto's
`pattern` lines apply. Adding the Nth device is one `mosquitto_passwd` line
plus the two `mqtt_user`/`mqtt_pass` keys on that device — **no ACL edits**:

```sh
mosquitto_passwd /etc/mosquitto/passwd 'harixos/<chipid>'   # one line per device
# then on the device:
notepad /harixos/settings.cfg    # set mqtt_user=harixos/<chipid> and mqtt_pass=<password>
settings reload
```

`admin` deliberately cannot publish retained state topics — the shell inbox
is full control, state spoofing is not needed for legitimate use.

## Broker configuration

Additions to a stock install — this is `examples/mosquitto/mosquitto.conf`
shipped in this repo:

```conf
# HarixOS broker additions — pair with the sibling `acl` file.
# Full setup, accounts and recovery: Documentation/MQTT-Security.md
# Windows installs: keep passwd/acl next to this file and use Windows paths.

listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd
acl_file /etc/mosquitto/acl
```

Windows installs: keep `passwd`/`acl` next to `mosquitto.conf` and use
Windows paths. The HA add-on Mosquitto is out of scope for this setup.

Account creation:

```sh
mosquitto_passwd -c /etc/mosquitto/passwd homeassistant      # -c only for the FIRST user
mosquitto_passwd /etc/mosquitto/passwd admin
mosquitto_passwd /etc/mosquitto/passwd 'harixos/a59450'     # one line per device, as added
```

ACL file — this is `examples/mosquitto/acl` shipped in this repo:

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
- static `user` blocks override patterns, so HA/admin cannot use the device
  pattern (they write only `shell/in`).
- `tele/#` exists only for the `bridge-status` example pattern
  (`mqtt pub tele/harixos/state …`). Omit the pattern line if you don't use it.
- A username containing `/` is valid MQTT (UTF-8) and accepted by
  `mosquitto_passwd` — quote it on the shell as shown.

## Client setup

1. **Device** — edit `/harixos/settings.cfg`:
   `mqtt_user=` = your `mqtt_prefix` (e.g. `harixos/a59450`),
   `mqtt_pass=<that device's password>`, `mqtt_host=<broker>`, then
   `settings reload` (and `mqtt start` if not enabled). `mqtt status` must
   show the user and a masked password. **Adding another ESP8266:** run
   `mosquitto_passwd /etc/mosquitto/passwd 'harixos/<chipid>'`, then the
   same two keys on that device — nothing on the broker changes beyond the
   password line.
2. **Home Assistant** — in the MQTT integration config, username
   `homeassistant` + its password. Discovery keeps working (device publishes
   `homeassistant/#`, HA subscribes).
3. **You** — `mosquitto_pub/sub -u admin -P …`.

## Recovery (lockout safety)

An unreadable `password_file` or forgotten password must not strand the
broker. Temporarily add this escape hatch, restart, fix credentials from
localhost, then remove the lines again:

```conf
listener 1884 127.0.0.1
allow_anonymous true
```

ACL mistakes deny per-topic (the broker still accepts connections), so
recovery is always possible from localhost.

## Verification checklist

Run against the broker once configured:

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
6. Anonymous `mosquitto_sub -t '#'` (no creds) → **not authorized**.
7. Pattern isolation (multi-device proof): as a second device account
   (`-u 'harixos/other'`), publish to `harixos/other/shell/in` → **allowed**;
   publish to `harixos/a59450/shell/in` → **not authorized**.

## TLS note

This setup assumes a trusted LAN. If the broker is ever reachable off-LAN
(port-forwarded, VPN'd broadly, or on a hostile network), add TLS: generate a
certificate (e.g. Let's Encrypt or a self-signed CA), switch the listener to
`listener 8883` + `cafile`/`certfile`/`keyfile` in `mosquitto.conf`, and set
the devices and clients to TLS (PubSubClient supports this via the
underlying WiFiClientSecure — a firmware change, unlike everything else on
this page). Until then, keep the broker bound to the LAN.

---

Design record: [`docs/superpowers/specs/2026-10-20-mqtt-broker-auth-design.md`](../docs/superpowers/specs/2026-10-20-mqtt-broker-auth-design.md)
