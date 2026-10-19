# pet-height-gate.hx -- self-contained: paste into notepad, then run it once
# with "run /scripts/pet-height-gate.hx" (register the sensor first, see the
# README). The init block registers the 2 s check schedule and the band
# defaults once per boot, and always starts with the lid shut. An HC-SR04
# looks straight down at the bowl: distance in [lo, hi] = a cat-sized animal
# -> the servo pulls the lid open; closer than lo = something taller rearing
# up -> shuts at once; further than lo, or a dead echo, = empty -> closes
# after $gone seconds of quiet, so a lingering tail or a missed ping cannot
# flap the lid. The lid is spring-hinged shut and the servo only pulls it
# open, so power loss fails closed. Knobs: "vars set lo=/hi=/gone=
# /open_angle=" then vars save; calibrate with "sensor read gate" while the
# cat eats vs the bowl empty vs an intruder rearing up.
# Wiring: HC-SR04 trig on D5 (GPIO14), echo on D6 (GPIO12) through the
# 1k/2k divider, servo signal on D2 (GPIO4), servo V+ 5V, common GND.

init
  schedule add */2 * * * * * run /scripts/pet-height-gate.hx
  vars setdefault lo=10
  vars setdefault hi=30
  vars setdefault gone=6
  vars setdefault open_angle=90
  set open = 0
  set quiet = 0
end

sensor publish gate
set d = sensor gate distance
set now = time

# band: 0 = nobody or no echo, 1 = cat window, 2 = too tall
set band = 0
if $d > 0
  if $d > $hi
    set band = 0
  else
    if $d < $lo
      set band = 2
    else
      set band = 1
    end
  end
end

if $band == 1
  set quiet = $now + $gone
  if $open == 0
    servo attach 4
    servo write 4 $open_angle
    set open = 1
  end
end

if $band == 2
  set quiet = 0
end

if $band != 1
  if $open == 1
    if $now >= $quiet
      servo attach 4
      servo write 4 0
      delay 400
      servo detach 4
      set open = 0
    end
  end
end
