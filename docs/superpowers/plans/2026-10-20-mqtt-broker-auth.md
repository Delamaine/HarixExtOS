# MQTT Broker Authentication Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the approved broker-auth spec as paste-ready Mosquitto configs plus `Documentation/MQTT-Security.md`, with pointers from the existing MQTT docs — zero firmware changes.

**Architecture:** Docs-and-examples deliverable. The spec (`docs/superpowers/specs/2026-10-20-mqtt-broker-auth-design.md`) is the single source of truth for account names, topic literals and config blocks; tasks copy from it verbatim and cross-check by grep so the three shipped files cannot drift from each other.

**Tech Stack:** Markdown docs, mosquitto config/acl files, PlatformIO gates (unchanged firmware).

**Spec:** `docs/superpowers/specs/2026-10-20-mqtt-broker-auth-design.md`

## Global Constraints

- Account names exactly: `homeassistant`, `admin`, and per-device pattern username = that device's `mqtt_prefix` (e.g. `harixos/a59450`).
- ACL pattern lines exactly: `pattern readwrite %u/#`, `pattern write homeassistant/#`, `pattern write tele/#`.
- Static blocks exactly: `homeassistant` → `topic read #` + `topic write harixos/+/shell/in`; `admin` → same two lines.
- Broker config exactly: `listener 1883`, `allow_anonymous false`, `password_file /etc/mosquitto/passwd`, `acl_file /etc/mosquitto/acl` (plus the Windows path note and the localhost recovery listener from the spec).
- No firmware edits (`src/**` untouched); commit style: conventional, single-quoted message, commits only at the plan's commit steps.

## Review Focus

- **Config/docs drift** (device denied or over-granted because the acl file and the doc show different lines): each task's final step greps account names and the three `pattern` lines across all shipped files and fails on mismatch.
- **Device lockout after enabling `allow_anonymous false`** (forgotten password / bad file path strands the broker): Task 2 verification greps `MQTT-Security.md` for the `listener 1884 127.0.0.1` recovery block; Task 3 runs the live checklist only if `mosquitto_pub` exists, else records it as user-run.
- **HA buttons silently losing `shell/in` access** (single-device ACL copied from an old draft): Task 1 verification greps the acl file for `harixos/+/shell/in` and for a missing static `user homeassistant` device block.
- **Pattern/static-block misunderstanding** (someone later adds a `user` block for a device and patterns stop applying): Task 2 verification greps the doc for the "static `user` blocks override patterns" note.
- **Windows path omission** (user's broker runs on Windows and copies Linux paths blindly): Task 2 verification greps `MQTT-Security.md` for `Windows`.

---

### Task 1: Paste-ready broker config files

**Files:**
- Create: `examples/mosquitto/mosquitto.conf`
- Create: `examples/mosquitto/acl`

**Interfaces:**
- Consumes: spec § "Broker configuration" (config block + ACL block, verbatim).
- Produces: the literal `acl` content and `mosquitto.conf` content that Task 2's doc must quote identically (account names + `pattern` lines are the shared contract).

- [ ] **Step 1: Write `examples/mosquitto/mosquitto.conf`**

Copy the spec's broker configuration block verbatim, plus a one-line comment header naming the companion `acl` file and pointing at `Documentation/MQTT-Security.md`. Include the spec's Windows path note as a comment.

- [ ] **Step 2: Write `examples/mosquitto/acl`**

Copy the spec's updated ACL block verbatim (two static `user` blocks, three `pattern` lines).

- [ ] **Step 3: Verify against spec**

Run (PowerShell):
```powershell
Select-String -Path examples\mosquitto\acl -Pattern 'pattern readwrite %u/#','pattern write homeassistant/#','pattern write tele/#','topic write harixos/\+/shell/in'
(Get-Content examples\mosquitto\acl | Select-String '^user ' | Measure-Object).Count
Select-String -Path examples\mosquitto\mosquitto.conf -Pattern 'allow_anonymous false','listener 1883','password_file','acl_file'
```
Expected: 5 matches total (the 3 `pattern` lines + 2 `topic write harixos/+/shell/in` lines), exactly 2 `user` lines, all 4 conf lines present.

- [ ] **Step 4: Commit**

```bash
git add examples/mosquitto
git commit -m 'docs: add paste-ready mosquitto conf and acl examples'
```

### Task 2: `Documentation/MQTT-Security.md`

**Files:**
- Create: `Documentation/MQTT-Security.md`

**Interfaces:**
- Consumes: Task 1's file contents (must quote account names/pattern lines identically) and spec sections: Threat model, Account model, Broker configuration, Client setup steps, Recovery, Verification checklist.
- Produces: the doc referenced by Task 3's pointers (`Documentation/MQTT-Security.md`).

- [ ] **Step 1: Write the doc**

Prose restatement of the spec, in the repo's Documentation style (compare `Documentation/ha-mqtt.md` heading/tables tone): sections `Threat model` (in-scope/out-of-scope bullets verbatim in meaning), `Accounts` (spec table + the "adding the Nth device = one passwd line" procedure), `Broker configuration` (embed both Task 1 files' contents), `Client setup` (device / HA / you steps incl. `settings reload`), `Recovery` (localhost listener block), `Verification checklist` (all 7 spec steps), `TLS note` (one paragraph: upgrade path if broker leaves the LAN). Link the spec file at the bottom.

- [ ] **Step 2: Verify the doc**

Run (PowerShell):
```powershell
Select-String -Path Documentation\MQTT-Security.md -Pattern 'listener 1884 127.0.0.1','Windows','static `user` blocks override patterns','pattern readwrite %u/#','harixos/\+/shell/in','settings reload','mosquitto_passwd'
```
Expected: all 7 patterns match (recovery, Windows note, pattern-override note, device pattern, HA/admin write line, settings reload, passwd command).

- [ ] **Step 3: Cross-check doc against the shipped acl file**

Run (PowerShell):
```powershell
$acl = Get-Content examples\mosquitto\acl -Raw
$doc = Get-Content Documentation\MQTT-Security.md -Raw
@('pattern readwrite %u/#','pattern write homeassistant/#','topic write harixos/+/shell/in') | ForEach-Object { if ($acl -notmatch [regex]::Escape($_) -or $doc -notmatch [regex]::Escape($_)) { throw "MISMATCH: $_" } }
'OK'
```
Expected: `OK` (no mismatch thrown).

- [ ] **Step 4: Commit**

```bash
git add Documentation/MQTT-Security.md
git commit -m 'docs: add MQTT-Security.md for mosquitto auth and ACLs'
```

### Task 3: Pointers, gates, verification

**Files:**
- Modify: `Documentation/ha-mqtt.md` (new `## Security` section, after the settings table section — ~line 70)
- Modify: `Documentation/Commands.md` (mqtt block, line ~137, one-line pointer)
- Modify: `Documentation/README.md` (index list, line ~13, `- [MQTT-Security.md](MQTT-Security.md) — …` entry)

**Interfaces:**
- Consumes: `Documentation/MQTT-Security.md` from Task 2 (link target).
- Produces: none (terminal task).

- [ ] **Step 1: Add the three pointers**

`ha-mqtt.md`: `## Security` section, 2–3 sentences on why `shell/in` needs auth + link. `Commands.md`: extend the existing `ha-mqtt.md` pointer sentence with `MQTT-Security.md`. `README.md`: index bullet.

- [ ] **Step 2: Run repo gates**

Run: `python -m platformio test -e native` then `python -m platformio run -e nodemcuv2`
Expected: 238 tests succeeded; build SUCCESS (docs-only change — any failure is a regression).

- [ ] **Step 3: Broker verification (if mosquitto is installed)**

Run: `Get-Command mosquitto_pub -ErrorAction SilentlyContinue`
If present: run the doc's 7-step verification checklist against a local broker using Task 1 files (skip device/HA steps needing hardware — steps 1,2,4,6,7 only) and record results in the task notes.
If absent: note `Checklist left for user — mosquitto not installed on build host` and stop (the checklist ships in the doc either way).

- [ ] **Step 4: Commit**

```bash
git add Documentation/ha-mqtt.md Documentation/Commands.md Documentation/README.md
git commit -m 'docs: link MQTT-Security.md from mqtt docs and index'
```
