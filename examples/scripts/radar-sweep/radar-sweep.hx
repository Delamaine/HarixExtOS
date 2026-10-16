# radar-sweep.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/radar-sweep.hx". The init block attaches the servo and
# registers the 2 s sweep schedule once per boot. Each tick advances the head
# to the next position (0 -> 90 -> 180 -> 0), lets it settle, then refreshes
# the cached ultrasonic distance -- so <prefix>/sensor/radar/distance traces
# the room as the head rotates. The step index is a RAM-only var (home = 0
# after reboot). The angle comes from "$step * 90" and reaches the servo via
# "servo write 4 $pos" -- command arguments expand $vars. A single-run while
# sweep is deliberately avoided: it would publish only the final distance.
# Wiring: HC-SR04 trig on D5 (GPIO14), echo on D6 (GPIO12), servo signal on
# D2 (GPIO4), servo V+ 5V, common GND.

init
  servo attach 4
  schedule add */2 * * * * * run /scripts/radar-sweep.hx
  vars setdefault step=0
end

set pos = $step * 90
servo write 4 $pos
set step = $step + 1
if $step > 2
  set step = 0
end
delay 350
sensor publish radar
