HarixOS Shell Commands
=======================

Core shell commands
- `help [topic]` — Show help and command list (`help wifi|gpio|i2c|fs|serve|post|mqtt|onchange|time|schedule|update|sensor|servo|motor|run|calc|set|vars|settings|powerprofile|cpufreq`)
- `about` — Show version and feature list
- `info` — Show system, flash and memory information
- `chip` — Show chip and flash details
- `heap` — Show free heap
- `uptime` — Show runtime
- `reboot` — Restart the device (`reset` is an alias that prints a rename notice)
- `adc` (or `a0`) — Read analog pin A0
- `clear` (or `cls`) — Clear serial console (emulator)
- `update check` — Check for system updates via GitHub
- `pull <url> <path>` — Download file from the internet (HTTP/HTTPS)
- `time` — Show time; `time sync`, `time list-tz` (alias `timezones`), `time set` manage NTP/timezone
- `powerprofile [full|balanced|powersave|minimal|off]` — Power profile (`powerprofile set <profile>|apply|status`)
- `cpufreq [40|80]` — CPU frequency (`cpufreq set <freq>|status`). The
  ESP8266 SDK rejects 40 MHz at runtime, so 80 is the only value that applies.
- `schedule` — Manage background tasks with 6-field cron

```
schedule list
schedule add <sec> <min> <hour> <dom> <month> <dow> <command>
schedule remove <id>
schedule run <id>
```

  Field order and ranges: `sec` 0-59, `min` 0-59, `hour` 0-23, `dom` 1-31,
  `month` 1-12 (numbers only, no `JAN`/`FEB`), `dow` 0-6 with **0 = Sunday**
  (no `7`). Seconds first is a HarixOS extension over standard 5-field cron.

  Each field accepts `*`, `n`, `a-b`, `*/n`, `a-b/n`, and comma lists such
  as `1,3,5`. `*/n` starts at the field's minimum (0 for sec/min/hour/dow,
  1 for dom/month), ranges never wrap (`5-1` is rejected), and `n <= 0` is
  rejected. There is no `@reboot`; the six fields above are the only
  accepted form.

  **`dom` and `dow` are AND-ed when both are restricted — this differs from
  Linux cron, which ORs them.** `0 0 9 15 * 1` runs only when the 15th is
  also a Monday. A `*` in a field restricts nothing.

  Examples:

```
schedule add */5 * * * * * heap
schedule add 0 30 14 * * * settings save
schedule add 0 0 9 15 * 1 ping
```

  The command is everything after the six fields and may contain spaces, so
  no quoting is needed. It runs through the same dispatcher as a `.hx`
  script; see `SCRIPT-REFERENCE.md` for the keywords available to it.

  Valid system time is required (`time`, `time sync`). Before NTP has
  synced the scheduler prints `no valid system time, cron not running`
  once and fires nothing.

  Tasks auto-save to `/harixos/schedule.cfg` on every add and remove and
  reload at boot — there is no `schedule save`. Ids are reassigned `1..N`
  in file order when loading, so an unchanged file reproduces the same ids
  after a reboot. A malformed line is skipped with a warning rather than
  aborting the load.


Dynamic powerprofile switching (auto power save)
- The device auto-lowers power when a serial terminal is not actively
  connected. Detection is `Serial.available() > 0` (buffered input present),
  re-evaluated every `loop()`.
- A state change (connected ⇄ idle) resets a timer. The switch only fires
  once the state has been stable for **60 seconds**, so brief bursts of
  traffic never trigger it.
- **Idle ≥ 60 s** and the current profile is `balanced` or `full` → switch
  to `powersave` and print `Terminal idle for 1 minute. Switched to
  powersave.` Profiles already `powersave`, `minimal` or `off` are left
  untouched.
- **Reconnected ≥ 60 s** → restore the saved profile from settings and the
  saved CPU frequency, and print `Terminal active for 1 minute. Restored
  profile: <profile>.`
- After a switch the timer resets, so it won't repeat until the state
  changes again. This runs in `loop()` (`src/main.cpp`) and is independent
  of `powerprofile set`/`apply`, which drive the same `applyPowerProfile`
  code path.

  Note: `balanced` and `powersave` both program `MODEM_SLEEP_T` on the
  ESP8266, so the idle→powersave switch prints a message but does not change
  the actual WiFi sleep type — the real savings come from the terminal being
  idle, not from the profile switch itself.


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
- `settings` — Show and edit persistent shell settings (`settings show|banner|timezone|tz|update|save|reload`)
- `calc <expr>` — Evaluate arithmetic expressions
- `set <name> = <expr>` — Store a script variable usable as `$name`
- `vars [list|set|get|del|save|load|clear]` — Global variable store
  (persisted to `/vars.dat`; `vars set <name>=<value>`, `vars get <name>`,
  `vars del <name>`, `vars save`, `vars load`, `vars clear`). Also a script
  keyword, so it works in `.hx` files, `schedule add` and MQTT `shell/in`.
- `run <path>` / `run list` / `run install <name>` / `run uninstall <name>` —
  Run or manage `.hx` apps

HTTP file server
- `serve <file> [port]` — Start a simple HTTP server serving the provided file as the index. If not connected to Wi‑Fi the shell will prompt for SSID and password. Returns the device IP and port when started.
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
  Rules are armed but no callback runs on an edge yet — see `ha-mqtt.md`.
- `onchange remove <pin>` — Remove a rule
- `onchange list` — Show active rules (bare `onchange` lists too)

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

- Ultrasonic sensor:

```
sensor init <trigger> <echo>
sensor ping [trigger] [echo]
sensor read [echo]
sensor list
```

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

- Ultrasonic sensor reading:

```
sensor init 4 5
sensor ping
sensor read 5
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
