# servo-lock

[Back to index](../README.md)

A deadbolt servo with a reed-switch interlock: you set `lock` over MQTT, the
bolt follows — **unless the door is open**, in which case one short beep
refuses the request and the lock completes automatically the moment the door
closes. Every applied state is acked on `stat/harixos/lock`, so the
dashboard knows where the bolt actually is, not just what was asked for.

## Wiring (NodeMCU v2)

```
servo signal ──────────────────────────────── D2 (GPIO4)   bolt position
reed ──────────────────────────────────────── D1 (GPIO5) ── GND
                     │ (reed open = HIGH; "onchange add" forces plain INPUT)
                     └────[ 10k pull-up ]──── 3V3
buzzer + ──[ 100R resistor ]───────────────── D5 (GPIO14)
servo V+ ── 5V, servo GND ── GND (common)
```

Fit the servo horn **before first power-up** so `$locked_angle` (default 0)
is the thrown position — the first tick re-asserts `$lock` (default 1 =
locked) and the bolt goes straight there.

## Setup

```
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
notepad /scripts/servo-lock.hx    # paste the contents of servo-lock.hx, save
run /scripts/servo-lock.hx        # run once: init attaches the servo, registers the door edge, the schedule and the defaults
```

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set lock=1'   # throw the bolt
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set lock=0'   # withdraw
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'         # persist the knob across reboots
```

Monitor the ack:

```sh
mosquitto_sub -t 'stat/harixos/lock'
```

## Locking rules

| State | `lock=1` request |
|---|---|
| door closed | bolt throws, ack `1` |
| door open | beep once, **no motion**; ack `1` fires when the door later closes |
| `lock=0` | always unlocks (no interlock on withdrawal) |

Invalid knob values (`<0`, `>1`) are repaired to 1 — a typo never fails
open. The refusal beep is edge-gated by the RAM-only `warned` flag: one
beep per refused request, not one per tick.

## What the device publishes

| Topic | Content |
|---|---|
| `stat/harixos/lock` | applied bolt state (0/1) per change (raw pub, fixed topic) |
| `<prefix>/gpio/5` | door edge via `onchange` + HA binary_sensor class `door` |
| `<prefix>/availability` | `online` / `offline` |

## Notes

- `applied` is RAM-only: after every reboot the first tick re-asserts
  `$lock`, and the reed tells it whether that is allowed yet — this is how
  the bolt finds out where it physically is.
- Auto-complete is the point of the interlock: leave with the door open,
  `vars set lock=1`, and the door shutting later is what throws the bolt.
- The reed reads **closed = 0** (pull-up, reed shorts to GND when the magnet
  is present) — the same convention as doorbell-remote and door-chime.
- Pair with **remote-button** by pointing its payload at `<prefix>/shell/in`
  with `vars set lock=1`.
- A held servo needs continuous pulses. Under `powerprofile light` the CPU
  pauses between DTIM beacons and pulses glitch (droop/jitter), so keep the
  profile at `full`/`balanced` while the bolt must stay put.
