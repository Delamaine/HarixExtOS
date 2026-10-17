# oled-status

[Back to index](../README.md)

A live status board: the SSD1306 shows the boot title, epoch time, free heap
and uptime, refreshed every second from a cron schedule. The smallest useful
`oled` example — four `oled line` writes per tick, no sensors, no MQTT.

## Wiring (NodeMCU v2)

```
SSD1306 SDA ─────────────────────────── D2 (GPIO4)
SSD1306 SCL ─────────────────────────── D1 (GPIO5)
SSD1306 VCC ─────────────────────────── 3V3
SSD1306 GND ─────────────────────────── GND
```

## Setup

```sh
wifi connect 'SSID' 'pass'
notepad /scripts/oled-status.hx   # paste the contents of oled-status.hx, save
run /scripts/oled-status.hx       # run once: init brings up the display and registers the 1s refresh
```

The script is self-contained: its `init` block runs `oled init` and adds the
1-second schedule once per boot (re-adding the same schedule is a no-op).

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'oled clear'               # blank the screen
mosquitto_pub -t '<prefix>/shell/in' -m 'oled line 0 HELLO'        # write a line manually
mosquitto_pub -t '<prefix>/shell/in' -m 'oled off'                 # display sleep (state kept)
mosquitto_pub -t '<prefix>/shell/in' -m 'schedule list'            # find the refresh task id
mosquitto_pub -t '<prefix>/shell/in' -m 'schedule remove <id>'     # stop the refresh
```

## Notes

- `TIME` is raw epoch seconds — the `$time` value token is numeric only.
  Decode it in the shell with `time`.
- Each tick rewrites all four lines (~50 ms of I2C per second at 100 kHz).
  Cheaper than that is a firmware knob (dirty-line diffing), not worth it
  for a status board.
- Non-0x3C panel: run `oled init 61` once from the shell to probe 0x3D,
  then change the init line in the script.
- Text is ASCII 32..126, 21 chars per line; anything else renders as `?`.
- 8 lines are available (`oled line 0` .. `oled line 7`) — the script uses
  four; the rest are free for your own additions.
- Display state is RAM only: after a reboot the `init` block re-runs
  `oled init` and the refresh tick repaints everything.
