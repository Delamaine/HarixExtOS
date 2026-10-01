# Replace POSIX Timezone with IANA Region Names

## Objective
Replace the current POSIX TZ string-based timezone system with IANA region names
(e.g., `Pacific/Auckland`, `America/New_York`) so that DST is handled automatically
and users don't need to remember complex TZ syntax.

## Current State
- `AppSettings.timezone` stores a POSIX TZ string (e.g., `"UTC0"`, `"PKT-5"`, `"GMT0BST,M3.5.0/M10.5.0"`)
- Boot: `setenv("TZ", ...)` + `tzset()` applied at startup
- NTP: `configTime(0, 0, ...)` with offset=0, relies on TZ for local time conversion
- Commands: `settings timezone <tz>`, `time sync [timezone]`, `time list-tz`
- `localtime()`/`strftime()`/`localtime_r()` all depend on TZ env var
- Cron scheduler uses `localtime_r()` which is TZ-dependent

## Design

### 1. Mapping Table: IANA Region -> POSIX TZ + UTC Offset

Each entry maps an IANA region name to:
- A POSIX TZ string that `setenv()`/`tzset()` understands
- A UTC offset in minutes (for `configTime()` auto-offset)

```
struct TzEntry {
  const char* iana;       // e.g., "Pacific/Auckland"
  const char* posix;      // e.g., "+12|-12,M9.5.0/M4.1.0"
  int32_t offsetMinutes;  // e.g., 720 (for configTime offset hours = offsetMinutes/60)
};
```

The POSIX string encodes DST rules. The `offsetMinutes` is the **standard** (non-DST) offset.
`configTime(offsetMinutes/60, 0, ...)` gives NTP a good starting point.

### 2. Conversion Function

```
// Returns the POSIX TZ string for an IANA region name.
// If input is already a POSIX TZ string (contains '-' or ':' or 'M'), returns as-is.
const char* ianaToPosix(const char* ianaName);

// Returns the UTC offset in minutes for an IANA region name.
int32_t ianaOffsetMinutes(const char* ianaName);
```

Detection of POSIX vs IANA:
- If the string contains `M` (month rule), `:` (half-hour), or starts with `+-HH:` or `+-HHMM`,
  treat as POSIX TZ.
- Otherwise, treat as IANA region name.

### 3. Mapping Table Structure

Organized by continent/region for easy scanning:

```
// === Americas ===
{ "America/New_York",  "-5|-5,M3.2.0,M11.1.0",   -300 },
{ "America/Chicago",   "-6|-6,M3.2.0,M11.1.0",   -360 },
{ "America/Denver",    "-7|-7,M3.2.0,M11.1.0",   -420 },
{ "America/Los_Angeles","-8|-8,M3.2.0,M11.1.0",  -480 },
{ "America/Sao_Paulo", "-3",                      -180 },
{ "America/Anchorage", "-9|-9,M3.2.0,M11.1.0",   -540 },
{ "America/Halifax",   "-4|-4,M3.2.0,M11.1.0",   -240 },
{ "America/El_Salvador","-6",                     -360 },
{ "America/Caracas",   "-4:30",                   -270 },
{ "America/St_Johns",  "-3:30|-3:30,M3.2.0,M11.1.0", -210 },

// === Europe ===
{ "Europe/London",     "GMT0BST,M3.5.0/M10.5.0",    0 },
{ "Europe/Paris",      "CET-1CEST,M3.5.0/M10.5.0/3", 60 },
{ "Europe/Berlin",     "CET-1CEST,M3.5.0/M10.5.0/3", 60 },
{ "Europe/Athens",     "EET-2EEST,M3.5.0/3/M10.5.0/4", 120 },
{ "Europe/Moscow",     "MSK-4",                     180 },
{ "Europe/Dublin",     "GMT0IST,M3.5.0/M10.5.0",    0 },
{ "Europe/Lisbon",     "WET0WEST,M3.5.0/M10.5.0",  0 },
{ "Europe/Helsinki",   "EET-2EEST,M3.5.0/3/M10.5.0/4", 120 },

// === Africa ===
{ "Africa/Cairo",      "EET-2EEST,M4.5.5/24,M10.5.5/24", 120 },
{ "Africa/Johannesburg","SAST-2",                   120 },
{ "Africa/Lagos",      "WAT-1",                     60 },
{ "Africa/Nairobi",    "EAT-3",                     180 },

// === Asia ===
{ "Asia/Dubai",        "GST-4",                    240 },
{ "Asia/Kolkata",      "IST-5:30",                 330 },
{ "Asia/Dhaka",        "BST-6",                    360 },
{ "Asia/Bangkok",      "ICT-7",                    420 },
{ "Asia/Shanghai",     "CST-8",                    480 },
{ "Asia/Tokyo",        "JST-9",                    540 },
{ "Asia/Seoul",        "KST-9",                    540 },
{ "Asia/Singapore",    "SGT-8",                    480 },
{ "Asia/Karachi",      "PKT-5",                    300 },
{ "Asia/Tashkent",     "UZT-5",                    300 },
{ "Asia/Colombo",      "IST-5:30",                 330 },

// === Oceania ===
{ "Australia/Sydney",  "AEST-10AEDT,M10.1.0/M4.1.0/3", 600 },
{ "Australia/Melbourne","AEST-10AEDT,M10.1.0/M4.1.0/3", 600 },
{ "Australia/Adelaide", "ACST-9:30ACDT,M10.1.0/M4.1.0/3", 570 },
{ "Australia/Perth",   "AWST-8",                   480 },
{ "Pacific/Auckland",  "+12|-12,M9.5.0/M4.1.0/3",  720 },
{ "Pacific/Fiji",      "+12|-12,M1.1.0/M4.1.0/3",  720 },
{ "Pacific/Honolulu",  "HST-10",                  -600 },
{ "Pacific/Tongatapu", "+13",                      780 },
{ "Pacific/Guam",      "ChST-10",                  600 },
```

Total: ~40 entries covering all commonly used regions.

### 4. Storage

- `AppSettings.timezone` continues to be a `String` — stores the IANA name.
- Default value changes from `"UTC0"` to `"UTC"` (maps to `"+0"` POSIX).
- The settings file format stays the same: `timezone=...` line.

### 5. Boot Flow

```
1. loadSettings() -> shellSettings.timezone = "Pacific/Auckland" (or whatever was saved)
2. Convert: posixStr = ianaToPosix(shellSettings.timezone)
3. setenv("TZ", posixStr, 1); tzset();
4. offsetHours = ianaOffsetMinutes(shellSettings.timezone) / 60;
5. configTime(offsetHours, 0, "pool.ntp.org", "time.nist.gov");
```

### 6. Command Changes

#### `settings timezone <name>`
```
settings timezone Pacific/Auckland    # Set IANA region
settings timezone UTC                 # Set UTC
settings timezone America/New_York    # Set US Eastern
```
- If the argument contains `:` or `M` or starts with `+-`, treat as POSIX TZ (backward compat).
- Otherwise, treat as IANA region name.

#### `time sync [region]`
```
time sync                           # Sync NTP with current timezone (auto-offset from mapping)
time sync Pacific/Auckland          # Sync NTP with auto-offset for Auckland
time sync GMT0BST,M3.5.0/M10.5.0    # Sync with explicit POSIX TZ (backward compat)
```
- If region given: lookup offset from mapping, call `configTime(offset, 0, ...)`.
- If no region: keep current timezone, just sync NTP.

#### `time list-regions` (new, replaces `time list-tz`)
```
time list-regions
```
Output:
```
Common Timezone Regions:
  Americas:
    America/New_York    UTC-5    US Eastern
    America/Chicago     UTC-6    US Central
    America/Denver      UTC-7    US Mountain
    America/Los_Angeles UTC-8    US Pacific
    America/Sao_Paulo   UTC-3    Brazil
    America/St_Johns    UTC-3:30 Canada Newfoundland

  Europe:
    Europe/London       UTC+0    UK / Western European
    Europe/Paris        UTC+1    Central European
    Europe/Moscow       UTC+4    Russia Moscow
    Europe/Athens       UTC+2    Eastern European

  Africa:
    Africa/Johannesburg UTC+2    South Africa
    Africa/Lagos        UTC+1    West Africa
    Africa/Nairobi      UTC+3    East Africa

  Asia:
    Asia/Dubai          UTC+4    UAE
    Asia/Kolkata        UTC+5:30 India
    Asia/Bangkok        UTC+7    SE Asia
    Asia/Shanghai       UTC+8    China
    Asia/Tokyo          UTC+9    Japan
    Asia/Seoul          UTC+9    Korea

  Oceania:
    Australia/Sydney    UTC+10   Australian Eastern
    Australia/Adelaide  UTC+9:30 Australian Central
    Pacific/Auckland    UTC+12   New Zealand
    Pacific/Honolulu    UTC-10   Hawaii

  Other:
    UTC                 UTC+0    Coordinated Universal Time
```

#### `time` (updated output)
```
Current Time: 2026-09-30 17:30:45
Timezone: Pacific/Auckland (NZST, UTC+12)
```

### 7. Files to Modify

| File | Change |
|------|--------|
| `src/apps/settings/settings.h` | Change default `timezone` from `"UTC0"` to `"UTC"` |
| `src/apps/settings/settings.cpp` | No structural change (stores String) |
| `src/main.cpp` | Major: add tz_mapping table, update all TZ references |
| (new) `src/api/tz_mapping.h` | TzEntry struct, function declarations |
| (new) `src/api/tz_mapping.cpp` | Mapping table + conversion functions |

### 8. Backward Compatibility

- **Existing settings files**: If `timezone=` contains `:` or `M` or starts with `+-`, it's treated as POSIX TZ. Old values like `UTC0`, `PKT-5`, `GMT0BST,M3.5.0/M10.5.0` continue to work.
- **New settings**: Default is `"UTC"` (IANA name, also valid POSIX).
- **Migration**: No migration needed. Users can gradually switch to IANA names.
- **`time list-tz`**: Kept as alias to `time list-regions` for backward compat.

### 9. Implementation Steps

1. Create `src/api/tz_mapping.h` with `TzEntry` struct and function declarations.
2. Create `src/api/tz_mapping.cpp` with the full mapping table and conversion functions.
3. Update `settings.h` default timezone to `"UTC"`.
4. Update `main.cpp`:
   - Add `#include "api/tz_mapping.h"`
   - Update `handleSettings()` timezone action to use IANA names
   - Update `handleTime()` sync action to use auto-offset from mapping
   - Update `handleTime()` list-tz action to show IANA regions
   - Update boot sequence (`main()` / `setup()`) to convert IANA->POSIX
   - Update help text in `handleHelp()` for `settings timezone` and `time sync`
   - Update `time list-tz` help text
5. Update `handleTime()` output to show IANA name alongside formatted time.
6. Build, flash, and verify.

### 10. Edge Cases

| Case | Handling |
|------|----------|
| Unknown IANA name | Fall back to `"+0"` (UTC), print warning |
| Empty timezone | Default to `"UTC"` |
| POSIX TZ string passed | Detect and use as-is (no lookup) |
| Half-hour offsets | Stored as fractional hours in `offsetMinutes`, `configTime()` truncates to int (acceptable for NTP sync) |
| DST transitions | Handled by `tzset()` using the POSIX string's DST rules |

## Acceptance Criteria

- [ ] `settings timezone Pacific/Auckland` sets timezone and saves correctly
- [ ] `time sync` uses auto-offset from the mapping table
- [ ] `time list-regions` shows all common regions with UTC offsets
- [ ] Boot sequence converts IANA name to POSIX TZ and applies it
- [ ] `localtime()`/`strftime()` produce correct local time after NTP sync
- [ ] Cron scheduler matches on local time (via TZ)
- [ ] Old POSIX TZ strings still work (backward compat)
- [ ] Help text updated for `settings timezone` and `time sync`
- [ ] Build succeeds, firmware flashes and boots correctly
