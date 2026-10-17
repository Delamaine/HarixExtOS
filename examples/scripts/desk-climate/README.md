# desk-climate

[Back to index](../README.md)

An AHT20 temperature/humidity monitor with a live OLED report: every 30
seconds the script reads the sensor, publishes it to MQTT (Home Assistant
picks both quantities up via discovery), and paints four OLED lines —
temperature, humidity, and a comfort word (`WARM` / `COMFORTABLE` / `COOL`)
between the `$t_max` / `$t_min` thresholds. The companion to **oled-status**:
sensor tokens driving a display instead of raw system numbers.

## Wiring (NodeMCU v2)

```
AHT20 SDA ───────────────────────────── D2 (GPIO4)
AHT20 SCL ───────────────────────────── D1 (GPIO5)
AHT20 VCC ───────────────────────────── 3V3
AHT20 GND ───────────────────────────── GND

SSD1306 SDA/SCL share the same two pins (address 0x3C, AHT20 is 0x38)
```

## Setup

```sh
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
sensor register aht20 desk             # one-time: registers on the shared I2C bus
notepad /scripts/desk-climate.hx       # paste the contents of desk-climate.hx, save
run /scripts/desk-climate.hx           # run once: init brings up the display and sets threshold defaults
```

(`sensor register` still runs once in the shell — re-registering every boot
would just print `[ERROR] sensor already registered` and continue.)

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/sensor/desk/temperature` | retained `23.4` (°C, `0.1` resolution) |
| `<prefix>/sensor/desk/humidity` | retained `44.4` (% RH) |
| `<prefix>/availability` | `online` / `offline` |

Plus the HA discovery configs for both quantities at register time.

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set t_max=28'   # widen the WARM band
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'           # persist the knob
mosquitto_pub -t '<prefix>/shell/in' -m 'run /scripts/desk-climate.hx'   # refresh now
mosquitto_pub -t '<prefix>/shell/in' -m 'sensor read desk'    # read without the display
```

## Notes

- Thresholds are `$vars` knobs with `setdefault` in init: the script never
  overwrites a limit you set remotely; retune either over MQTT, then
  `vars save`.
- Display lines are ASCII, 21 chars max; `TEMP 23.4 C` and `HUM 44.4 %`
  fit with room to spare. The comfort word is written last, so a threshold
  change takes effect on the next 30 s tick.
- Each tick costs one AHT20 measurement (80-180 ms of I2C) plus four OLED
  writes (~50 ms) — well under a second of bus time per run.
- `sensor publish` failure (sensor unplugged) prints `[ERROR] ...` and the
  script continues; the OLED keeps showing the last-good cached readings
  from the `sensor desk ...` tokens.
- Keep `powerprofile full` or `balanced` while the display must repaint on
  schedule — under `light`, CPU pauses between wakes stretch the 30 s tick
  (the schedule still fires; the wake-up latency just grows).
