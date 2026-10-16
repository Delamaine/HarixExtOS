# door-chime.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/door-chime.hx". The init block registers the reed edge rule,
# the chime poll schedule and the defaults once per boot. Every 2 s the
# script compares the door pin against the RAM-only prev flag; on a
# closed->open edge it plays a $notes-note melody (880 Hz blips + a long
# 1175 Hz finish) if enabled. Silence it over <prefix>/shell/in with
# "vars set enabled=0" (re-enable with =1; RAM-only, chime back on after a
# reboot). Retune $notes (1-5) and $gap ms the same way; the script clamps
# notes so a typo cannot hang the scheduler. The edges also reach MQTT via
# onchange (<prefix>/gpio/4 + HA binary_sensor class door) -- no script
# needed there. Wiring: reed D2 (GPIO4) to GND + external 10k pull-up to
# 3V3 ("onchange add" forces plain INPUT), buzzer + on D5 (GPIO14) through
# a 100R resistor, buzzer - to GND.

init
  onchange add 4 both door
  schedule add */2 * * * * * run /scripts/door-chime.hx
  vars setdefault enabled=1
  vars setdefault prev=0
  vars setdefault notes=2
  vars setdefault gap=150
end

if $notes < 1
  set notes = 1
end
if $notes > 5
  set notes = 5
end
set open = readpin 4
if $open != $prev
  set prev = $open
  if $open == 1
    if $enabled == 1
      set n = $notes
      while $n > 0
        beep 14 880 200
        delay $gap
        set n = $n - 1
      end
      beep 14 1175 350
    end
  end
end
