# HarixOS v1.0 .hx Script Quick Reference

## Keywords available to scripts and scheduled commands

`.hx` scripts and `schedule add` share one command dispatcher, so every
keyword below works identically in both. This is the set added for
scripting and scheduling (21 in total):

**System values:** `heap`, `uptime`, `chip`, `info`, `adc`, `calc`
**Filesystem:** `pwd`, `cd`, `ls`, `mkdir`, `touch`, `rm`, `cp`, `mv`,
`cat`, `write`, `append`
**Settings and time:** `settings`, `time`, `reboot`
**Variables:** `set`

Deliberately not available to either: `serve`, `i2c`, `notepad`,
`update`, `pull` (spec §8).

## New Commands

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

`print` echoes its argument verbatim and does **not** expand `$name` or value
tokens. Use `set` or `calc` when you need to see an expanded value.

An undefined `$name` is an **error**, never a silent `0`.

### Value tokens

| Token | Value |
|---|---|
| `heap` | free heap in bytes |
| `adc` | analog read of A0 |
| `readpin <n>` | `digitalRead(n)` |
| `uptime` | seconds since boot |
| `millis` | milliseconds since boot |
| `time` | epoch seconds |

```bash
set level = readpin 2
set stamp = time
```

`readpin` with a missing or non-numeric argument is an error.

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

## Control Flow (`if` / `else` / `end`)

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
- An unclosed block is reported as `[ERROR] <n> unclosed if block(s)`.

**Not supported:** `&&`, `||`, `elif`, and loops (`while`, `for`).

`if` / `else` / `end` are **script-only**. Typing `if ...` at the shell
reports `Unknown command: if`. The scheduler dispatches single commands and
does not echo the result it returns, so a scheduled `if` prints nothing
beyond the `[Scheduler] Executing` line — it never opens a block.

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

## WiFi Command (Networking)

```bash
wifi scan                  # Scan available networks
wifi connect 'SSID' 'pass' # Connect to WiFi (use quotes)
wifi disconnect            # Disconnect from WiFi
wifi status                # Show WiFi status
wifi ip                    # Show IP configuration
```

## Time & Schedule Commands (Automation)

### System Clock
```bash
time                       # Show current time
time sync <TZ_STRING>      # Sync via NTP (e.g., PKT-5)
time list-tz               # Show timezone examples
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
```

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
reload at boot; there is no `schedule save` command. Ids are reassigned
`1..N` in file order when loading, so an unchanged file reproduces the same
ids after a reboot. A malformed line in that file is skipped with a warning
and does not abort the load.

Before NTP has synced the scheduler prints
`no valid system time, cron not running` once per boot and fires nothing.


## Delay Command (Timing)

```bash
delay 1000                 # Wait 1000 milliseconds (1 second)
delay 500                  # Wait 500 milliseconds (0.5 seconds)
delay 100                  # Wait 100 milliseconds
```

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
