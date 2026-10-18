# HarixOS script examples

Self-contained automation examples for the ESP8266 build. Each folder pairs a
`.hx` script with a README covering wiring, the `init` block (its `relay add`,
`onchange add` and `schedule add` calls are idempotent and run once per boot)
and the MQTT knobs. Copy the script into `/scripts/<name>.hx` with `notepad`,
set the pins for your board, reboot — it registers itself.

Optional: write `run /scripts/<name>.hx` lines into a `/profile.hx` file on
the device and the firmware executes them once at every boot (after all
subsystems are up, before the first prompt) — boot registration with no
manual first run after flashing and no schedule tick. See the boot profile
bullet under `init` in [SCRIPT-REFERENCE.md](../../SCRIPT-REFERENCE.md).

## Alarms & notifications

- [daily-digest](daily-digest/README.md) — 08:00 health digest to console and ntfy; the `print` + lenient `$vars` example.
- [door-chime](door-chime/README.md) — Plays a short melody on each door open; mute knob over MQTT.
- [greenhouse-alert](greenhouse-alert/README.md) — DHT22 over-limit alarm with remote temperature/humidity thresholds.
- [leak-webhook](leak-webhook/README.md) — Soil moisture past a threshold fires an HTTP webhook with the live reading.
- [parking-assist](parking-assist/README.md) — HC-SR04 beeps: slow under `warn`, fast under `stop`; both retunable.
- [sos-alarm](sos-alarm/README.md) — Panic button sends an urgent ntfy push; cooldown stops bounces.
- [weather-station](weather-station/README.md) — BME280 + BH1750 telemetry every 15 min with a frost alarm.

## Climate & control loops

- [auto-vent](auto-vent/README.md) — Temperature hysteresis swings a servo vent open past `open_at`, closed below `close_at`.
- [desk-climate](desk-climate/README.md) — AHT20 comfort report on an SSD1306 OLED with MQTT/HA telemetry every 30 s.
- [fan-control](fan-control/README.md) — DS18B20-driven fan with hysteresis; both switch points retunable over MQTT.
- [pump-interlock](pump-interlock/README.md) — Pump follows a remotely sent `target` threshold against soil moisture.
- [rain-closure](rain-closure/README.md) — Rain closes the awning once per shower, then POSTs a webhook event.
- [sun-blind](sun-blind/README.md) — Servo blind tracks LDR illuminance with retunable thresholds.

## Servos & motion

- [curtain-motor](curtain-motor/README.md) — Motorized curtain: `vars set cmd=open/close`, limit switches stop the motor.
- [drum-feeder](drum-feeder/README.md) — Rotary-valve drum feeds exactly one large biscuit per cycle; portion = `$count` cycles.
- [gauge-needle](gauge-needle/README.md) — DHT22 temperature mapped onto a servo needle; a physical dial gauge with a retunable scale.
- [pan-tilt-remote](pan-tilt-remote/README.md) — Two servos as a remote mount: `vars set pan/tilt` over MQTT with per-axis acks.
- [pet-feeder](pet-feeder/README.md) — Servo hopper feeds at 07:30/18:30; portion and dwell are `$vars` knobs.
- [radar-sweep](radar-sweep/README.md) — Servo sweeps the head; cached ultrasonic distance feeds HA graphs.
- [servo-lock](servo-lock/README.md) — Deadbolt servo with a reed interlock: refuses to lock while the door is open, acks the bolt state.

## Timing & routines

- [porch-light](porch-light/README.md) — PIR light with a hold time in RAM; reboot always starts dark.
- [tea-timer](tea-timer/README.md) — Kettle relay timed by `delay $minutes`; the argument-expansion showcase.
- [weekday-alarm](weekday-alarm/README.md) — 07:00 Mon-Fri wake-up; hour/dow-restricted cron and script jobs.

## MQTT & Home Assistant

- [bridge-status](bridge-status/README.md) — Tasmota-style JSON telemetry every 5 min on a fixed topic like `tele/harixos/state`.
- [doorbell-remote](doorbell-remote/README.md) — Reed door sensor on D2 to MQTT/HA `door` entity plus local buzzer check.
- [ha-webhook](ha-webhook/README.md) — Door state as HTTP POST to a Home Assistant webhook; no broker needed.
- [lamp-remote](lamp-remote/README.md) — A0 light telemetry plus lamp relay with dark-as-fallback local check.
- [oled-status](oled-status/README.md) — Live SSD1306 status board: time, heap and uptime, refreshed every second.
- [pin-mirror](pin-mirror/README.md) — Input pin mirrored to a relay, edges published, remote override on `shell/in`.
- [remote-button](remote-button/README.md) — Button on one NodeMCU drives a `vars` knob on another over MQTT.
- [scene-ack](scene-ack/README.md) — Remote scene sets relays and confirms the applied value on `stat/harixos/scene`.
- [two-device](two-device/README.md) — Shell one-liners plus a broker-side MQTT forwarder driving a second device; README only.
