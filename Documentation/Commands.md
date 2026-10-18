HarixOS Shell Commands
=======================

Core shell commands
- `help [topic]` — Show help and command list (`help wifi|gpio|i2c|fs|serve|post|mqtt|onchange|relay|time|schedule|update|sensor|servo|motor|beep|run|calc|set|vars|settings|powerprofile|cpufreq`)
- `about` — Show version and feature list
- `info` — Show system (incl. supply voltage VCC in mV when the firmware is built with `ADC_MODE(ADC_VCC)`, otherwise `n/a`), WiFi, GPIO, filesystem, last reset and (if synced) current time
- `chip` — Show chip and flash details
- `heap` — Show free heap
- `uptime` — Show runtime
- `reboot` — Restart the device (`reset` is an alias that prints a rename notice)
- `adc` (or `a0`) — Read analog pin A0
- `clear` (or `cls`) — Clear serial console (emulator)
- `update check` — Check for system updates via GitHub
- `pull <url> <path>` — Download file from the internet (HTTP/HTTPS)
- `time` — Show time; `time sync`, `time list-tz` (alias `timezones`), `time set` manage NTP/timezone
- `powerprofile [full|balanced|powersave|light|minimal|off]` — Power profile (`powerprofile set <profile>|apply|status`). `light` = CPU sleeps between DTIM beacons; WiFi/broker stay up, but servos glitch while held. While the saved profile is `light`, a live serial console preempts it with `full` on any received byte (NodeMCU can't sense port-open; RX activity is the proxy) and the stored profile returns 60 s after the last input.
- `cpufreq [40|80]` — CPU frequency (`cpufreq set <freq>|status`). The
  ESP8266 SDK rejects 40 MHz at runtime, so 80 is the only value that applies.
- `schedule` — Manage background tasks with 6-field cron

```
schedule list
schedule add [id=<n>] <sec> <min> <hour> <dom> <month> <dow> <command>
schedule remove <id>
schedule run <id>
```

  The optional `id=<n>` prefix (n >= 1) picks a specific task id instead of
  the next free one — rejected if a different task already holds it — so a
  script can `schedule remove` the same task by a known id. Ids are saved to
  `/harixos/schedule.cfg` and reload unchanged across a reboot.

  Field order and ranges: `sec` 0-59, `min` 0-59, `hour` 0-23, `dom` 1-31,
  `month` 1-12 (numbers only, no `JAN`/`FEB`), `dow` 0-6 with **0 = Sunday**
  (no `7`). Seconds first is a HarixOS extension over standard 5-field cron.

  Each field accepts `*`, `n`, `a-b`, `*/n`, `a-b/n`, and comma lists such
  as `1,3,5`. `*/n` starts at the field's minimum (0 for sec/min/hour/dow,
  1 for dom/month), ranges never wrap (`5-1` is rejected), and `n <= 0` is
  rejected. There is no `@reboot` field; boot-time execution is the
  `/profile.hx` boot profile — a script the firmware runs once from
  `setup()` after all subsystems are up (absent file = skip). The six
  fields above are the only accepted cron form.

  **`dom` and `dow` are AND-ed when both are restricted — this differs from
  Linux cron, which ORs them.** `0 0 9 15 * 1` runs only when the 15th is
  also a Monday. A `*` in a field restricts nothing.

  Examples:

```
schedule add */5 * * * * * heap
schedule add 0 30 14 * * * settings save
schedule add 0 0 9 15 * 1 ping
schedule add id=7 0 0 8 * * * run /scripts/pump-interlock.hx
```

  The command is everything after the six fields and may contain spaces, so
  no quoting is needed. It runs through the same dispatcher as a `.hx`
  script; see `SCRIPT-REFERENCE.md` for the keywords available to it.

  Valid system time is required (`time`, `time sync`). Before NTP has
  synced the scheduler prints `no valid system time, cron not running`
  once and fires nothing.

  Tasks auto-save to `/harixos/schedule.cfg` on every add and remove and
  reload at boot — there is no `schedule save`. Each saved line carries
  `id=<n>`, so ids (including hand-picked ones) reproduce exactly after a
  reboot; a legacy line without the prefix loads with the next free id, and
  a duplicate `id=` in a hand-edited file is re-assigned with a boot warning.
  A malformed line is skipped with a warning rather than
  aborting the load. Re-adding the same cron+command is a no-op success
  (idempotent), so scripts can register their own schedules in `init`
  blocks — see `SCRIPT-REFERENCE.md`.


Filesystem commands (each works top-level and with an `fs ` prefix,
e.g. `fs ls`)
- `pwd` — Print current working directory
- `ls [-R] [path]` — List directory contents
- `cd <path>` — Change directory (supports `..` and absolute paths)
- `mkdir <path>` — Create directory recursively
- `touch <path>` — Create empty file
- `cat <file>` — Display file contents
- `write <file> <text...>` — Overwrite file with text
- `append <file> <text...>` — Append text to file
- `rm [-r] <path>` — Remove file or directory
- `cp <src> <dst>` — Copy files (not recursive)
- `mv <src> <dst>` — Move/rename files

There is no `format` command; LittleFS is formatted at provision time.

Apps and variables
- `notepad <path>` — Open interactive line editor
- `settings` — Show and edit persistent shell settings (`settings show|banner|timezone|tz|hostname|update|save|reload`); `settings show` also prints the OS version (`kOsVersion`, currently `0.9`) and the config path; dotted hostnames supported (e.g. `esp8266.rimu` → `esp8266.rimu.local`; resolves via mDNS on macOS/Linux — Windows natively resolves only two-label `.local` names)
- `calc <expr>` — Evaluate arithmetic expressions (max 64 tokens per expression)
- `set <name> = <expr>` — Store a script variable usable as `$name`
- `vars [list|set|setdefault|get|del|save|load|clear]` — Global variable store
  (persisted to `/vars.dat`; `vars set <name>=<value>`, `vars setdefault <name>=<value>`
  sets only if the name is undefined — for idempotent `init` blocks, `vars get <name>`,
  `vars del <name>`, `vars save`, `vars load`, `vars clear`). The store auto-loads at
  boot; `vars load` is only needed to re-read the file after external edits. Limits: max 16
  variables, names up to 16 characters, string values up to 31 characters.
  Also a script keyword, so it works in `.hx` files, `schedule add` and MQTT `shell/in`.
- `run <path>` / `run list` / `run install <name>` / `run uninstall <name>` —
  Run or manage `.hx` apps

HTTP file server
- `serve <file> [port]` — Start a simple HTTP server serving the provided file as the index. If not connected to Wi‑Fi the shell will prompt for SSID and password. Returns the device IP and port when started. While running, the server is also announced on the local network via mDNS as `_http._tcp` (reachable as `<hostname>.local`).
- `serve stop` — Stop the HTTP server.
- `serve status` — Show current server status and served file.

Notes: When serving a file (for example `/www/index.html`), linked resources referenced by paths in the HTML (e.g., `/script.js`, `/styles.css`) will also be served from the same directory. The server logs each request to Serial.

Networking
- `wifi status` — Show Wi‑Fi status
- `wifi scan` — Scan available networks
- `wifi connect '<ssid>' '<pass>'` — Connect to Wi‑Fi (use quotes for SSID/Pass)
- `wifi disconnect` — Disconnect and clear the current session
- `wifi ap '<ssid>' '<pass>'` — Start an access point
- `wifi mode off|sta|ap|staap` — Set Wi‑Fi mode
- `wifi ip` — Show IP configuration
- `wifi mac` — Show MAC addresses

IoT (MQTT / webhooks)
- `mqtt status` — Connection state, config (password masked) and counters
- `mqtt start` / `mqtt on` — Enable the MQTT service (persists to settings)
- `mqtt stop` / `mqtt off` — Disable the MQTT service
- `mqtt pub <topic> <payload>` — Publish a raw message (topic is used verbatim,
  no prefix is added)

  Broker, credentials, prefix and telemetry interval come from `mqtt_host`,
  `mqtt_port`, `mqtt_user`, `mqtt_pass`, `mqtt_prefix`, `mqtt_interval`,
  `mqtt_discover`, `mqtt_enabled` in `/harixos/settings.cfg` — there is no
  `settings mqtt_*` command; edit the file and run `settings reload`.
  Full topic map, HA discovery and examples: `Documentation/ha-mqtt.md`.

- `post <url> <body> [content-type]` — HTTP POST webhook
  (content-type defaults to `application/json`)

- `onchange add <pin> <rising|falling|both>` — Register a GPIO edge rule
   (pin accepts `4` or `D2` style; rules persist to `/onchange.rules`, max 8).
   A debounced edge publishes `<mqttPrefix>/gpio/<pin>` = `on`/`off` — see
   `ha-mqtt.md`. Re-adding the same pin+edge+class is a no-op success; a
   different edge or class on a registered pin reports a duplicate.
- `onchange remove <pin>` — Remove a rule
- `onchange list` — Show active rules (bare `onchange` lists too)

- `relay add <pin> <name> [on|off]` — Register a relay (initial level
  optional, defaults to off) and persist to `/relays.conf`. Re-adding the
  same pin+name is a no-op success and leaves the latched level untouched
  (idempotent, safe in `init` blocks).
- `relay set <name> on|off` — Latch a relay on or off (level holds until
  changed). Publishes `<mqttPrefix>/relay/<name>/state`.
- `relay toggle <name>` — Invert a relay's current level.
- `relay status <name>` — Show the current on/off level for a relay.
- `relay list` — Show registered relays and their GPIOs (bare `relay` lists too).

  Relays are latched switches: once set, they stay in that state across
  power loss until changed again. Names are 1-15 chars of `[A-Za-z0-9_-]`
  (they appear verbatim in MQTT topics). HA `switch` discovery is published
  automatically when MQTT is enabled — see `ha-mqtt.md`.

Hardware
- `gpio read <pin>` — Read GPIO
- `gpio write <pin> <on|off|toggle|0|1>` — Set GPIO output
- `gpio list|mode|pulse` — Pin listing, mode and pulse output
- `i2c scan` — Scan for I2C devices
- `i2c begin <sda> <scl>` — Initialize the I2C bus

Hardware drivers
- Servo control:

```
servo attach <pin>
servo detach <pin>
servo write <pin> <angle>
servo read <pin>
servo list
```

- OLED display (SSD1306 128x64, I2C):

```
oled init [addr]
oled clear
oled line <0-7> <text>
oled on
oled off
oled status
```

  - Text-only: 21 chars per line (ASCII 32..126, others render as `?`), no
    graphics. `init` defaults to address 0x3C (`addr` is a decimal 8..119);
    the shared I2C bus must exist first (`Wire.begin()` at setup — SDA
    GPIO4, SCL GPIO5 — or `i2c start <sda> <scl>`). Display state is RAM
    only; re-run `oled init` after a reboot (e.g. from a script `init`
    block). See `SCRIPT-REFERENCE.md`.

- Sensors (named registry: ultrasonic, DHT22, DS18B20 daisy-chain, BME280, AHT20, soil, ldr, BH1750):

```
sensor register ultrasonic <trigger> <echo> <name>
sensor register dht22 <pin> <name>
sensor register ds18b20 <pin> [index] <name>
sensor register bme280 <name>
sensor register aht20 <name>
sensor register soil A0 <name>
sensor register ldr A0 <name>
sensor register bh1750 <name>
sensor unregister <name>
sensor list
sensor read <name>
sensor publish [name]
```

  - Names are 1-15 chars `[A-Za-z0-9_-]`; pins are GPIO numbers validated via
    `GpioAPI::isAvailablePin`. soil/ldr are raw ADC readings on the single
    ESP8266 analog input and accept only the `A0` (or `17`) pin token, reported
    as a 0-100 percent; bh1750 is an I2C lux sensor (address 0x23) needing no
    pin, aht20 likewise (I2C 0x38, temperature + humidity). Registered sensors persist to `/sensors.conf`
    (bad lines are skipped with a warning at boot) and their readings publish
    retained to `<mqtt_prefix>/sensor/<name>/<quantity>` (`distance`,
    `temperature`, `humidity`, `pressure`, `moisture`, `illuminance`). See
    `Documentation/ha-mqtt.md`.
  - `sensor publish` reads every sensor now and publishes the cached readings
    — the cron-friendly cadence:
    `schedule add 0 */5 * * * * sensor publish`
  - Legacy pin-addressed aliases still work but are deprecated (ultrasonic
    only): `sensor init <trigger> <echo>` (registers as `hc<echo>`),
    `sensor ping [trigger] [echo]`, `sensor read [echo]`, `sensor list`.

- DC motor (L293D H-bridge):

```
motor init [m1|m2] [name_pin] [speed_pin]
motor forward [m1|m2]
motor reverse [m1|m2]
motor stop [m1|m2]
motor brake [m1|m2]
motor speed <0-100> [m1|m2]
motor list
```

- Buzzer beep:

```
beep <pin> [hz] [ms]
```

Blocks for `<ms>` (default 1000Hz, 200ms; max 10s). Rejects pins attached
to a servo or motor.

Examples
- Create and edit a file:

```
mkdir notes
cd notes
notepad todo.txt
```

- Quick filesystem check:

```
ls /
pwd
cat /harixos/settings.cfg
```

- Servo sweep script:

```
servo attach 4
servo write 4 0
delay 500
servo write 4 90
delay 500
servo write 4 180
delay 500
servo write 4 90
delay 500
servo read 4
servo detach 4
```

- Sensor reading (legacy alias form still works; prefer the registry):

```
sensor register ultrasonic 4 5 door
sensor read door
sensor publish door
sensor list
```

- Motor control script:

```
motor init m1
motor forward m1
motor speed 75 m1
delay 2000
motor stop m1
motor init m2
motor reverse m2
motor speed 50 m2
delay 2000
motor brake m2
motor list
```
