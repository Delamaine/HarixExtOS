HarixOS Shell Commands
=======================

Core shell commands
- `help` — Show help and command list
- `info` — Show system, flash and memory information
- `reboot` — Restart the device
- `clear` — Clear serial console (emulator)
- `update check` — Check for system updates via GitHub
- `pull <url> <path>` — Download file from the internet (HTTP/HTTPS)
- `time` — Show time and NTP sync options
- `schedule` — Manage background tasks with 6-field cron

```
schedule list
schedule add <sec> <min> <hour> <dom> <month> <dow> <command>
schedule remove <id>
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
schedule add */5 * * * * heap
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


Filesystem (`fs`) commands
- `pwd` — Print current working directory
- `ls [path]` — List directory contents
- `cd <path>` — Change directory (supports `..` and absolute paths)
- `mkdir <path>` — Create directory recursively
- `touch <path>` — Create empty file
- `cat <file>` — Display file contents
- `write <file> <text...>` — Overwrite file with text
- `append <file> <text...>` — Append text to file
- `rm <path>` — Remove file or empty directory
- `cp <src> <dst>` — Copy files (not recursive)
- `mv <src> <dst>` — Move/rename files
- `format` — Format LittleFS filesystem (erases data)

Apps
- `notepad <path>` — Open interactive line editor
- `settings` — Show and edit persistent shell settings (e.g., `settings update on|off`)
- `calc <expr>` — Evaluate arithmetic expressions

HTTP file server
- `serve <file> [port]` — Start a simple HTTP server serving the provided file as the index. If not connected to Wi‑Fi the shell will prompt for SSID and password. Returns the device IP and port when started.
- `serve stop` — Stop the HTTP server.
- `serve status` — Show current server status and served file.

Notes: When serving a file (for example `/www/index.html`), linked resources referenced by paths in the HTML (e.g., `/script.js`, `/styles.css`) will also be served from the same directory. The server logs each request to Serial.

Networking
- `wifi scan` — Scan available networks
- `wifi connect '<ssid>' '<pass>'` — Connect to Wi‑Fi (use quotes for SSID/Pass)
- `wifi status` — Show Wi‑Fi status

Hardware
- `gpio read <pin>` — Read GPIO
- `gpio write <pin> <on|off|toggle|0|1>` — Set GPIO output
- `i2c scan` — Scan for I2C devices

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
