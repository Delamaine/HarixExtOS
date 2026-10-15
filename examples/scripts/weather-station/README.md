# weather-station

An I2C weather corner: BME280 (temp/humidity/pressure) and BH1750 (illuminance)
publish retained telemetry every 15 minutes for Home Assistant discovery, and the
script beeps a frost alarm when the cached temperature drops below a remotely
retunable `frost` limit via the `sensor <name> temperature` value token.

## Wiring (NodeMCU v2)

```
3V3 ──┬──────────────── BME280 VCC / BH1750 VCC
GND ──┴──────────────── BME280 GND / BH1750 GND

BME280 SDA ──┬── D2 (GPIO4)  (shared I2C bus, Wire starts at boot)
BH1750 SDA ──┘
BME280 SCL ──┬── D1 (GPIO5)
BH1750 SCL ──┘
BH1750 ADDR ── GND (address 0x23)

D5 (GPIO14) ──[ 100R ]──[ buzzer + ]   [ buzzer − ] ── GND
```

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
sensor register bme280 weather
sensor register bh1750 light
notepad /scripts/weather-station.hx    # paste the contents of weather-station.hx, save
run /scripts/weather-station.hx        # run once: init registers the publish schedule and defaults the frost limit
```

The script is self-contained: its `init` block registers the publish schedule
and `vars setdefault` the frost limit once per boot. A limit you set remotely
survives reboots (the vars store auto-loads at boot) and is never overwritten
by setdefault. (`sensor register` still runs once in the shell — re-registering
reports a duplicate.)

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/sensor/weather/temperature` | °C telemetry (every 15 min) |
| `<prefix>/sensor/weather/humidity` | %RH telemetry (same publish) |
| `<prefix>/sensor/weather/pressure` | hPa telemetry (same publish) |
| `<prefix>/sensor/light/illuminance` | lux telemetry (every 15 min) |
| `<prefix>/availability` | `online` / `offline` |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set frost=4'   # alarm earlier
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'          # persist across reboots
```

Monitor:

```sh
mosquitto_sub -t '<prefix>/sensor/weather/#' -t '<prefix>/sensor/light/illuminance'
```

## Notes

- Home Assistant auto-discovers all four `sensor.weather_*` / `sensor.light_*`
  entities (device classes temperature/humidity/pressure/illuminance) from the
  retained publishes — the dashboard side needs no script work.
- The frost beep only sounds on a scheduled tick, so it can lag the threshold
  crossing by up to 15 minutes; tighten the cron if that matters.
- I2C wiring note: both sensors share the bus; if BME280 sits at 0x77 instead
  of 0x76 the kernel probes both addresses automatically.
