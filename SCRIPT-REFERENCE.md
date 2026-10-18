# HarixOS v1.0 .hx Script Quick Reference

## Keywords available to scripts and scheduled commands

`.hx` scripts and `schedule add` share one command dispatcher, so every
keyword below works identically in both:

**Output and control:** `print`, `if`/`else`/`end`, `while`/`end`, `break`,
`init`/`end`, `#`
**System values:** `heap`, `uptime`, `chip`, `info`, `adc`, `calc`
**Filesystem:** `pwd`, `cd`, `ls`, `mkdir`, `touch`, `rm`, `cp`, `mv`,
`cat`, `write`, `append`
**Settings and time:** `settings`, `time`, `reboot`
**WiFi and GPIO:** `wifi`, `gpio`, `beep`, `delay`, `system`, `run <path>`, `help`
**Servo:** `servo attach`, `servo detach`, `servo write`, `servo read`, `servo list`
**Display:** `oled init`, `oled clear`, `oled line`, `oled on`, `oled off`, `oled status`
**Sensor:** `sensor` register/unregister/list/read/publish (ultrasonic, DHT22, DS18B20, BME280, soil, LDR, BH1750, AHT20) plus legacy `init|ping|read|list`
**Motor:** `motor init`, `motor forward`, `motor reverse`, `motor stop`, `motor brake`, `motor speed`, `motor list`
**IoT:** `post`, `mqtt`, `relay` set|toggle|status|list, `onchange`, `sensor` publish
**Automation:** `schedule add|remove|run|list`
**Variables:** `set`, `vars list|set|setdefault|get|del|save|load|clear`
**Power:** `powerprofile <full|balanced|powersave|light|minimal|off> | set <profile> | apply | status`

Shell-only (typing these inside a script or `schedule add` reports
`Unknown command`): `about`, `pull`, `update`, `serve`, `i2c`, `notepad`,
`cpufreq`, `fs`, `cls`, `reset`, and
the `run list|install|uninstall` subcommands (spec §8 for the deferred
group).

## New Commands (shell only — not script keywords)

The `about`, `pull` and `update` commands below are interactive-shell
commands; they are not available inside `.hx` scripts or `schedule add`.

### About Command

Shows HarixOS version and information:

```bash
about
```

Output includes:
- Version number
- Description
- Features list
- Build date and time

### Pull Command (Download Files)

Download files from the internet:

```bash
pull http://example.com/file.bin /data/file.bin
pull https://example.com/data.txt /scripts/data.txt
```

**Requirements:**
- Board must be connected to WiFi
- Supports HTTP and HTTPS
- Saves to LittleFS path

**Example:**
```bash
# First connect to WiFi
wifi connect 'MyNetwork' 'MyPassword'

# Then download a file
pull http://example.com/myapp.hx /apps/myapp.hx

# Run the downloaded app
run myapp
```

**WiFi Automation:**
- Once connected, WiFi stays connected until `wifi disconnect`
- Credentials are saved; HarixOS auto-connects to saved networks at boot
- Automatically prompted if WiFi is disconnected

### Update Command (System Updates)

Check for new HarixOS versions from GitHub:

```bash
update check
```

**Features:**
- Shows new version number
- Displays changelog/What's New
- Provides direct download link

**Automatic Boot Check:**
HarixOS can automatically check for updates on boot if WiFi is connected. This is disabled by default.
- **Enable:** `settings update on`
- **Disable:** `settings update off` (default)
- **Check Manually:** `update check`

## Print Command (Output)

```bash
print Hello, World!        # Print text
print                      # Print blank line
print GPIO value: 1
print WiFi: Connected
```

## Variables and Expressions

### `set` — store a value

```bash
set x = 5              # x = 5
set x = $x * 2 + 1     # x = 11
set count = 0          # define it before reading it back
set count = $count + 1 # read-modify-write
```

The whole expression is evaluated **before** the value is stored, so
`$count` on the right still reads the previous value. A malformed form
(missing `=`, invalid name, bad expression) reports an error and leaves
the variable untouched.

The same `set` works at the shell and inside a `.hx` script. In a script
it prints nothing; at the shell it prints `<name> = <value>`.

Names start with a letter or `_`, contain letters, digits and `_`, and are
at most 16 characters. Up to 16 variables can be stored.

### `$name` versus bare value tokens

**A bare word is always a value token; a variable is always referenced as
`$name`.** The two never collide, even when they share a name:

```bash
set heap = 5       # a variable literally named "heap"
set h = $heap      # h = 5        — $heap reads the variable
set b = heap       # b = <free heap> — a bare heap reads the real value
```

`print` expands `$name` in its argument — handy to dump a var. Bare value
tokens (`heap`, `readpin`, …) expand only inside `set` / `calc` / `if` /
`while` expressions; use `set` or `calc` when you need to see those.

An undefined `$name` is an **error** in ordinary commands, never a silent
`0` — except in `print` / `post` / `write` / `append`, where it stays
literal (see below).

### `$name` in command arguments

Command arguments expand `$name` before the command runs, so knobs can be
tuned without editing scripts:

```bash
vars set speed=75
motor speed $speed m1
delay $wait                # after set wait=1200
```

- String variables (`vars set name=text`) substitute their text; numbers
  substitute their formatted value.
- `print`, `post`, `write` and `append` use **lenient expansion**: defined
  vars substitute, but an undefined `$name` stays literal instead of
  erroring — so JSON bodies and shell text containing `$` survive.
- Bare value tokens (`heap`, `adc`, `readpin`, `sensor …`) expand only inside
  `set` / `calc` / `if` / `while` expressions; in arguments a bare word is
  always literal (a relay named `heap` stays `heap`).
- A `$` not followed by a name passes through literally (URLs, JSON bodies).

### Value tokens

| Token | Value |
|---|---|
| `heap` | free heap in bytes |
| `adc` | analog read of A0 |
| `readpin <n>` | `digitalRead(n)` |
| `sensor <name> <quantity>` | last-good cached reading of a registered sensor |
| `uptime` | seconds since boot |
| `millis` | milliseconds since boot |
| `time` | epoch seconds |

```bash
set level = readpin 2
set stamp = time
set tempC = sensor hall temperature
```

`readpin` with a missing or non-numeric argument is an error. `sensor` errors
when the name is unregistered, the quantity does not match the sensor type, or
the value was never read — run `sensor publish <name>` first (in the same
script or via a schedule) to refresh the cache. Quantities: `distance`,
`temperature`, `humidity`, `pressure`, `moisture`, `illuminance`.

### Expressions

Standard arithmetic with parentheses, `+ - * /`, unary minus, and
comparisons `< > <= >= == !=` (which bind loosest of all):

```bash
calc 1 + 2 * 3       # 7
calc (1 + 2) * 3     # 9
calc 2 * -3          # -6
calc $x <= 10        # 1 or 0
calc heap            # expands the value token first
```

`calc` and `set` both expand `$name` and value tokens before evaluating.
Division by zero reports `Invalid expression.`.

## Control Flow (`if` / `else` / `while` / `break` / `end`)

```bash
set level = readpin 2
if $level == 1
  print LED is on
else
  print LED is off
end
```

- A condition is true when it evaluates to a non-zero number.
- `else` is optional.
- Nesting is capped at **8 levels**; a ninth `if` reports
  `Block nesting limit exceeded`.
- Everything between `if` and its matching `else`/`end` is skipped when the
  condition is false — including nested `if` blocks.
- Comments must be their own line: a whole-line comment is dropped before
  keywords are matched, so `# end` does not close a block. A trailing `#`
  after an `if` or `set` expression is *not* a comment and fails as
  `Invalid expression.`.
- An unclosed block is reported as `[ERROR] <n> unclosed block(s)`.

**Not supported:** `&&`, `||`, `elif`, and `for`.

`if` / `else` / `end` are **script-only**. Typing `if ...` at the shell
reports `Unknown command: if`. The scheduler dispatches single commands and
does not echo the result it returns, so a scheduled `if` prints nothing
beyond the `[Scheduler] Executing` line — it never opens a block.

### Loops (`while` / `end`, `break`)

```bash
set i = 3
while $i > 0
  calc $i
  set i = $i - 1
end
```

- `while <cond>` starts a loop; its body runs until the condition goes
  false at a matching `end`. The condition is re-evaluated at each `end`.
- `break` leaves the **innermost** enclosing `while` immediately: the rest
  of the body (including any `if`/`else` still open) is skipped to that
  `while`'s `end`. A `break` outside any loop reports
  `break outside while loop`.
- Safety caps: **100,000 iterations** (`while loop iteration limit
  exceeded`) and **5 minutes** wall time (`while loop time limit
  exceeded`). The engine feeds the watchdog on each loop pass, so a tight
  loop ends via these caps rather than a board reset.
- The script still owns the CPU while looping: the shell, web UI, and
  scheduler are blocked until the script finishes. Keep bodies short (add
  `delay` when you don't need the CPU) and prefer scheduled single
  commands for periodic work.
- `while`/`break` are **script-only**, like `if`. A single scheduled
  `while` never opens a block.

## Init Block (`init` / `end`)

```bash
init
  schedule add */30 * * * * * run /scripts/myapp.hx
  onchange add 4 both door
  vars setdefault target=600
end

# the rest of the script runs on every execution
```

- The `init ... end` block runs **once per boot**. Later runs of the same
  script skip it; a reboot is the only reset. After reboot the persisted
  schedule fires again and re-runs the script, so init re-registers itself.
- Boot profile: if `/profile.hx` exists, the firmware runs it from
  `setup()` once per boot, after all subsystems are up and before the first
  prompt (absent file = silent skip). It is an ordinary script, so
  `run /scripts/myapp.hx` inside it executes `myapp`'s `init` block at every
  boot — the boot trigger for examples that should not depend on a schedule
  tick or a manual first run. `run /profile.hx` executes it manually.
- The once-per-boot gate is shared: `run`, the scheduler and `shell/in`
  all consult the same RAM table keyed by script path (max 8 scripts).
- Use only idempotent commands in `init`: `schedule add` (same cron+command
  re-added is a no-op), `onchange add` (same rule re-added is a no-op),
  `relay add` (same pin+name re-added is a no-op and the latched level is
  left untouched), `vars setdefault` (sets only if the name is undefined).
  `sensor register` reports a duplicate on re-registration and belongs in
  shell setup, not in `init`.
- If any command in the block errors, init is **not** marked done and runs
  again on the next execution.
- `init` is top-level only (not nested in `if`/`while`); an unclosed block
  is reported at end of file.

## GPIO Command (Hardware Control)

### Set Pin State
```bash
gpio 2 on                  # Set GPIO2 HIGH
gpio 2 off                 # Set GPIO2 LOW
gpio 2 toggle              # Toggle GPIO2
```

### Pin Configuration
```bash
gpio 2 mode output         # Set GPIO2 as output
gpio 2 mode input          # Set GPIO2 as input
gpio 2 mode input_pullup   # Set GPIO2 as input with pullup
```

### Read and Test
```bash
gpio 2 read                # Read GPIO2 value (HIGH/LOW)
gpio 2 pulse 5             # Pulse GPIO2 5 times (500ms default)
gpio 2 pulse 10 250        # Pulse GPIO2 10 times, 250ms between
gpio list                  # Show available GPIO pins on this board
```

### ESP-01 Available Pins
```bash
gpio 0                     # Available (boot pin - HIGH required)
gpio 2                     # Available (boot pin - HIGH required)
# All other pins not exposed on ESP-01
```

### ESP8266 (4MB+) Available Pins
```bash
gpio 0                     # Available (HIGH required at boot)
gpio 2                     # Available (HIGH required at boot)
gpio 4                     # Available
gpio 5                     # Available
gpio 12                    # Available
gpio 13                    # Available
gpio 14                    # Available
gpio 15                    # Available (LOW required at boot)
gpio 16                    # Available
```

## Beep Command (Buzzer Tone)

```bash
beep 14              # 1000 Hz for 200 ms (defaults)
beep 14 880 300      # 880 Hz for 300 ms
```

- `beep <pin> [hz] [ms]` works in the shell, inside `.hx` scripts, and in
  `schedule add` tasks. hz is 37-5000, ms is 10-10000.
- The pin must be an available GPIO (GPIO6-11 are flash pins and rejected).
  `beep` is also refused when the pin is attached to a servo or used by a
  motor, because `tone()` shares the PWM hardware with them.

## Servo Command (Servo Motors)

```bash
servo attach 4             # Attach servo to GPIO4
servo detach 4             # Detach servo from GPIO4
servo write 4 90           # Write angle (0-180 degrees)
servo read 4               # Read current angle
servo list                 # List all servos
```

### Example
```bash
print Attaching servo...
servo attach 4
servo write 4 0
delay 500
servo write 4 180
delay 500
servo read 4
servo detach 4
```

## OLED Command (SSD1306 128x64)

Text-only driver over the shared I2C bus (`i2c scan` should show `0x3C`).
21 characters per line, ASCII 32..126, anything else renders as `?`:

```bash
oled init [addr]        # bring up the display (default 0x3C, addr as decimal 8..119)
oled clear              # blank all 8 pages
oled line <0-7> <text>  # write one 21-char line
oled on                 # display on (or wake from sleep)
oled off                # display sleep, RAM state kept
oled status             # prints init state, address, line 0..7
```

- Nothing renders before `oled init` (per-run guard: `[ERROR] oled not
  initialized`). Display state is RAM only — re-run `oled init` after a
  reboot, e.g. from the script's `init` block.
- The bus must exist first: default `Wire.begin()` at setup (SDA GPIO4,
  SCL GPIO5), or `i2c start <sda> <scl>`.

## Sensor Command (Named Registry)

The `sensor` keyword takes one of the register/unregister/list/read/publish
actions, or a legacy alias:

```bash
sensor register ultrasonic <trigger> <echo> <name>   # HC-SR04
sensor register dht22 <pin> <name>                   # DHT22
sensor register ds18b20 <pin> [index] <name>         # DS18B20 (daisy-chain index)
sensor register bme280 <name>                        # BME280/BMP280 (I2C)
sensor register aht20 <name>                         # AHT10/AHT15/AHT20 (I2C 0x38)
sensor register soil A0 <name>                       # capacitive soil (A0 only)
sensor register ldr A0 <name>                        # LDR voltage divider (A0 only)
sensor register bh1750 <name>                        # BH1750 lux meter (I2C 0x23)
sensor unregister <name>
sensor list
sensor read <name>            # read now, print all quantities
sensor publish [name]         # read then publish cached readings (retained)
```

- Registered sensors persist to `/sensors.conf`, publish retained state to
  `<mqtt_prefix>/sensor/<name>/<quantity>`, and appear in Home Assistant via
  discovery (see `Documentation/ha-mqtt.md`).
- The cron-friendly cadence is `schedule add 0 */5 * * * * sensor publish`.
- Legacy aliases (deprecated, ultrasonic only): `sensor init <t> <e>`,
  `sensor ping [t] [e]`, `sensor read [echo]`, `sensor list`.

```bash
sensor register dht22 4 hall
sensor register ultrasonic 5 6 door
sensor publish
sensor read hall
sensor list
sensor unregister hall
```

## Motor Command (DC Motors - L293D)

```bash
motor init m1              # Initialize M1 (GPIO14 name, GPIO12 speed)
motor init m2              # Initialize M2 (GPIO13 name, GPIO5 speed)
motor forward m1           # Move M1 forward
motor reverse m2           # Move M2 reverse
motor stop m1              # Stop M1 (coast)
motor brake m2             # Brake M2 (stop quickly)
motor speed 75 m1          # Set M1 speed to 75%
motor list                 # List all motors
```

### Example
```bash
print Initializing motors...
motor init m1
motor init m2
motor forward m1
motor speed 75 m1
delay 2000
motor stop m1
motor reverse m2
motor speed 50 m2
delay 2000
motor brake m2
motor list
```

## IoT Commands (`post` / `mqtt` / `onchange`)

These work in the shell, in `.hx` scripts, in `schedule add` tasks, and
over MQTT `<prefix>/shell/in`.

### `post` — HTTP webhook

```bash
post <url> <body> [content-type]   # content-type defaults to application/json
post https://api.example.com/event {"type":"gpio","pin":2}
post http://localhost:8080/log hello text/plain
```

$name expands in the URL and body with **lenient** semantics: defined vars
substitute, an undefined `$name` stays literal, so a body can mix both —
`post https://api.example.com/e {"v":"$temp","note":"$schema"}` posts the
`temp` value and the literal `$schema` without either breaking the request.

### `mqtt` — MQTT service

```bash
mqtt status                        # connection, config, counters
mqtt start                         # enable service (persists; `mqtt on` is an alias)
mqtt stop                          # disable service (`mqtt off` is an alias)
mqtt pub <topic> <payload>         # publish verbatim — no prefix is added
```

Broker, prefix and interval are configured with `mqtt_*` keys in
`/harixos/settings.cfg`; topic map and Home Assistant setup are in
`Documentation/ha-mqtt.md`.

### `onchange` — GPIO edge rules

```bash
onchange add <pin> <rising|falling|both> [class]   # pin accepts GPIO numbers or D2 style
onchange remove <pin>
onchange list
```

The optional third argument sets the Home Assistant binary_sensor device
class: `motion`, `occupancy`, `door`, `window` or `moisture` (default
`motion`). Example: `onchange add 4 both door`.

Rules persist to `/onchange.rules` (max 8 rules) and reload at boot.
Pin mode is configured to input automatically when a rule is registered.
Re-adding the same pin+edge+class is a no-op success (safe inside `init`
blocks); a different edge or class on a registered pin reports a duplicate.

### `relay` — latched switch outputs

```bash
relay add <pin> <name> [on|off]  # register a relay (initial level optional)
relay set <name> on|off          # latch on/off; level holds until changed
relay toggle <name>              # invert the current level
relay status <name>              # show current level
relay list                       # show registered relays (bare `relay` lists)
```

Relays latch in software: once set, a level holds until it is changed
again, surviving reboots. Registration and level persist to `/relays.conf`
(max 4 relays) and reload at boot. Re-adding the same pin+name is a no-op
success and leaves the latched level untouched (safe inside `init` blocks);
a different name on a registered pin (or a different pin for a registered
name) reports a duplicate. Relay names are 1-15 chars of
`[A-Za-z0-9_-]` and map one GPIO to one HA `switch` when MQTT is enabled.

## WiFi Command (Networking)

```bash
wifi scan                  # Scan available networks
wifi connect 'SSID' 'pass' # Connect to WiFi (use quotes)
wifi disconnect            # Disconnect from WiFi
wifi status                # Show WiFi status
wifi ip                    # Show IP configuration
```

## Time & Schedule Commands (Automation)

`time`, `settings`, `reboot` and `schedule add|remove|run|list` are script
keywords; the command a task runs uses the script dispatcher listed at the
top of this document.

### System Clock
```bash
time                       # Show current time (script: show time)
time sync                  # Sync via NTP (script keyword)
time sync <TZ_STRING>      # Shell: sync with explicit POSIX/IANA tz
time list-tz               # Shell: show timezone examples
```

### Task Scheduler

6-field cron — `sec min hour dom month dow`. Seconds are the first field,
which standard 5-field cron does not have. Valid system time is required.

```bash
schedule list                          # List active tasks
schedule remove <id>                   # Delete task
schedule add */5 * * * * * heap        # Every 5 seconds
schedule add 0 30 14 * * * settings save   # Daily at 14:30:00
schedule add 0 0 9 15 * 1 ping         # 09:00:00 on the 15th AND a Monday
schedule add id=7 0 */5 * * * heap     # Fixed ID 7 (for schedule remove 7)
```

An optional `id=<n>` prefix (n >= 1) assigns a specific task id instead of
the next free one; `schedule add id=<n>` rejects the request when a
*different* task already holds that id, while re-adding an identical
cron+command still succeeds as a no-op regardless of the prefix. Use a fixed
id when a script needs `schedule remove <id>` to target the same task
forever.

| Field | Range |
|---|---|
| sec | 0–59 |
| min | 0–59 |
| hour | 0–23 |
| dom | 1–31 |
| month | 1–12 (numbers only, no `JAN`/`FEB`) |
| dow | 0–6, **0 = Sunday** (no `7`) |

Per-field syntax: `*`, `n`, `a-b`, `*/n`, `a-b/n`, and comma lists such as
`1,3,5`. `*/n` starts at the field's **minimum** (0 for sec/min/hour/dow,
1 for dom/month), ranges never wrap (`5-1` is rejected), and `n <= 0` is
rejected. There is no `@reboot`.

**`dom` and `dow` are AND-ed when both are restricted — this differs from
Linux cron, which ORs them.** `0 0 9 15 * 1` fires only when the 15th is
also a Monday, not on either condition alone. A `*` in a field restricts
nothing.

The command is everything after the six fields, so it may contain spaces
and needs no quoting. It dispatches through the script command set listed
at the top of this document.

Tasks auto-save to `/harixos/schedule.cfg` on every `add` and `remove` and
reload at boot; there is no `schedule save` command. Each line persists as
`id=<n> <expression> <command>`, so ids — including hand-picked ones —
reproduce exactly after a reboot (a legacy line without the prefix loads as
its position in the file; a duplicate `id=` in a hand-edited file is
re-assigned the next free id with a boot warning). A malformed line in that
file is skipped with a warning and does not abort the load. Re-adding the
same cron+command is a no-op success (idempotent), which is what makes
`schedule add` safe inside script `init` blocks.

Before NTP has synced the scheduler prints
`no valid system time, cron not running` once per boot and fires nothing.


## Delay Command (Timing)

```bash
delay 1000                 # Wait 1000 milliseconds (1 second)
delay 500                  # Wait 0.5 seconds (500ms)
delay 100                  # Wait 100 milliseconds
```

The value is an integer millisecond count. `$name` expands first
(`delay $wait`), but arithmetic is not evaluated — `delay 100*2` is invalid.

## System Command (Device Info)

```bash
system info                # Show chip ID, core version, SDK, etc.
system heap                # Show free heap memory
system reboot              # Reboot the device
```

## Comments and Help

```bash
# This is a comment
# Comments start with # and are ignored
help                       # Show available commands
```

## Common Patterns

### Blink LED (GPIO2)
```bash
print Setting up GPIO2...
gpio 2 mode output
print GPIO2 ready

print Blinking 5 times...
gpio 2 pulse 5 500

print Blink complete!
```

### Check System
```bash
print === System Check ===
system info
print
print Free memory:
system heap
print === Done ===
```

### WiFi Check
```bash
print Checking WiFi...
wifi status
print
print IP Configuration:
wifi ip
print
print Scanning networks...
wifi scan
```

### LED Sequence
```bash
# Turn LED ON
gpio 2 on
delay 1000

# Turn LED OFF
gpio 2 off
delay 1000

# Turn LED ON again
gpio 2 on
delay 1000

# Done
print Sequence complete
```

### Safe GPIO Test (No Boot Pins)
```bash
# Test GPIO2 (safe on ESP-01)
print Testing GPIO2...
gpio 2 mode output
gpio 2 on
delay 500
gpio 2 off
delay 500

# Test GPIO4 (if available)
print Testing GPIO4...
gpio 4 mode output
gpio 4 on
delay 500
gpio 4 off
print Tests done!
```

## Error Examples (and how to avoid them)

### ❌ Invalid GPIO Pin
```bash
gpio 99 on     # ERROR: GPIO99 doesn't exist
```
**Fix:** Use valid pin (GPIO0, GPIO2, GPIO4, GPIO5, GPIO12, GPIO13, GPIO14, GPIO15, GPIO16)

### ❌ Boot Pin Issue
```bash
gpio 0 off     # ERROR: GPIO0 must stay HIGH
gpio 2 off     # ERROR: GPIO2 must stay HIGH
```
**Fix:** These pins are required for boot. Don't set them LOW.

### ❌ Empty SSID
```bash
wifi connect "" password   # ERROR: SSID cannot be empty
```
**Fix:** Provide valid SSID

### ❌ Invalid Delay
```bash
delay 0        # ERROR: Delay must be > 0
delay -100     # ERROR: Delay must be > 0
```
**Fix:** Use positive milliseconds (delay 100)

## Installing and Running Scripts

### Step 1: Install
```bash
run install myapp
```

### Step 2: Enter Script Content
```
print Hello from my app!
gpio 2 mode output
gpio 2 on
delay 1000
gpio 2 off
print Done!
END
```
(Type `END` on new line to finish)

### Step 3: Run
```bash
run myapp
```

### Step 4: List Apps
```bash
run list
```

### Step 5: Uninstall
```bash
run uninstall myapp
```

## Output Format

**Successful Command:**
```
[OK] Command description
```

**Failed Command:**
```
[ERROR] command line: Error message
```

**Script Summary:**
```
--- Script Execution Start ---
[OK] 
[ERROR] gpio 99 on: Invalid pin
[OK] 
--- Script Complete: 3 lines, 1 errors ---
```

## Best Practices

✅ **DO:**
- Test commands individually first
- Use print statements for status
- Check GPIO availability (run `gpio list`)
- Add delays between GPIO operations
- Comment your scripts with #

❌ **DON'T:**
- Force GPIO0/GPIO2 LOW (breaks boot)
- Use unavailable pins
- Forget END marker when installing
- Use delay 0 or negative delays
- Assume operations are instant

## Memory and Performance

- **Heap:** ~47KB available on ESP-01
- **Script max lines:** ~1000 (depends on heap)
- **GPIO operation time:** <1ms
- **WiFi scan:** 5-30 seconds
- **Reboot time:** ~2 seconds

## Debug Tips

1. **Check available pins:**
   ```bash
   gpio list
   ```

2. **Check system status:**
   ```bash
   system info
   system heap
   ```

3. **Check WiFi:**
   ```bash
   wifi status
   wifi ip
   ```

4. **Add print statements** to see script progress:
   ```bash
   print Starting operation...
   gpio 2 on
   print GPIO2 is HIGH
   ```

5. **Read error messages** - they tell you what's wrong!

## Example: Complete App

```bash
print ===========================
print My HarixOS App v1.0
print ===========================
print

print 1. Checking system...
system info
delay 500

print
print 2. Testing GPIO2...
gpio 2 mode output
gpio 2 on
print GPIO2 is HIGH
delay 1000
gpio 2 off
print GPIO2 is LOW

print
print 3. WiFi Status...
wifi status

print
print ===========================
print App complete!
print ===========================
```

Save this as `run install myapp` and paste the script!
