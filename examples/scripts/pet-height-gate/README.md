# pet-height-gate

[Back to index](../README.md)

Height-gated food bowl: an HC-SR04 looks down at the bowl and a servo lid
opens only for a cat-sized visitor. Distance inside the `lo`–`hi` window means
a cat is at the bowl → lid opens and stays open while the cat eats; anything
**closer than `lo`** (a raccoon or dog rearing up on its hind legs) shuts it at
once; an empty bowl (or a dead echo) closes it after `$gone` seconds of quiet.
There is no identity here — the gate filters by **size class**, so a
neighbour's same-sized cat gets fed too; for real ID you'd want RFID.

## Closing mechanism (simplest & cheapest)

An **SG90 micro servo ($1–2) pulling a spring-hinged lid** — the cheapest
reliable close:

- Lid rests **shut** on a spring (or its own weight); the servo only pulls it
  aside. Power loss, reboot or limp horn (after `servo detach`) → lid stays or
  returns **shut = fail closed**.
- Three wires to the NodeMCU, no driver board, works straight from the
  existing `servo` command (same linkage as [pet-feeder](../pet-feeder/README.md)).
- Direct-drive (horn glued to the lid like pet-feeder's flap) also works, but
  then a power cut leaves the lid wherever it was — the spring hinge is the
  upgrade that costs nothing but a hinge and a rubber band.

A relay + 12 V linear actuator or a solenoid latch does the same job with
more wires, more money and usually *fails open* — not worth it for a bowl.

## Wiring (NodeMCU v2)

```
HC-SR04 VCC ── 5V (USB)     HC-SR04 GND ── GND
HC-SR04 trig ───────────────────────────── D5 (GPIO14)
HC-SR04 echo ──[ 1k ]──┬──[ 2k ]── GND   (3.3V divider)
                       └───────────────── D6 (GPIO12)

servo signal ───────────────────────────── D2 (GPIO4)
servo V+ (5V from USB)   servo GND ─────── GND
```

Mount the sensor **facing down**, its face about 45 cm above the bowl floor,
in a small hood. Aim so the beam cone (~15°) sees only the bowl, not the
room — a passer-by outside the cone can't open it.

## Setup

```sh
wifi connect 'SSID' 'pass'
mqtt settings <broker> 1883
mqtt start
sensor register ultrasonic 14 12 gate
notepad /scripts/pet-height-gate.hx   # paste the contents, save
run /scripts/pet-height-gate.hx       # run once: init registers the schedule and defaults
```

The script is self-contained: its `init` block registers the 2 s check
schedule and `vars setdefault` the knobs once per boot, and forces the lid
state to shut on every boot (a stray saved `open` can never leave the logic
thinking the lid is already up).

## Calibrating the bands

Measure three readings with `sensor read gate` (publishes cache on each tick,
so watch live values):

1. Bowl **empty** → expect ≈ the mount height (e.g. `45`)
2. **Cat eating** → top of head/ears (e.g. `22`)
3. Something **rearing up** (e.g. `8`)

Then set the window between (2) and (3), and the empty cut below (1):

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set lo=10'   # > cat reading, < rearing reading
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set hi=30'   # < empty reading
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'
```

## What the device publishes

| Topic | Content |
|---|---|
| `<prefix>/sensor/gate/distance` | cm telemetry (every 2 s via `sensor publish`) |
| `<prefix>/availability` | `online` / `offline` |

## Driving it remotely

```sh
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set gone=10'      # linger longer before closing
mosquitto_pub -t '<prefix>/shell/in' -m 'vars set open_angle=120'  # swing the lid wider
mosquitto_pub -t '<prefix>/shell/in' -m 'vars save'
mosquitto_pub -t '<prefix>/shell/in' -m 'run /scripts/pet-height-gate.hx'  # run one check now
```

## Notes

- While the lid is open the servo holds position (~100–150 mA); on close it
  writes 0, settles 400 ms, then detaches — limp, no buzzing between visits
  (same trick as pet-feeder).
- `$gone` is the anti-flap grace: a missed ping or a tail drifting out of the
  beam does not shut the lid, but a sensor that dies closes it within
  `$gone` seconds (no echo reads as empty).
- Reboot always starts shut: the open flag is RAM-only and `init` overwrites
  it, so the bowl is safe after every restart.
- The 2-second cadence is the ultrasonic refresh rate — the gate tracks the
  cat in near-real time; `delay 400` on close runs inside one tick.
- Keep `lo` above your cat's tallest reading: ears in the window are fine,
  but set it too low and the cat itself reads as "too tall".
- Like all size-based gates, expect no protection against a same-size
  intruder (or against a cat that learns to wait for the neighbour's cat).
