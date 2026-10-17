# drum-feeder.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/drum-feeder.hx". The init block attaches the drum servo and
# registers the two daily cron jobs (07:30 and 18:30) plus the portion
# defaults once per boot. One rotary-valve cycle feeds exactly ONE biscuit:
# the servo first swings the pocket under the hopper throat ($load, a biscuit
# settles in for $settle ms), then over the exit chute ($deliver, it falls
# for $pass ms). The pocket holds a single biscuit, so a cycle delivers one
# or none (jammed/empty) -- never two. After the last cycle the servo
# detaches (no holding-torque draw, safe under `powerprofile light`).
# $count cycles per run, clamped 1-12.
# Knobs expand $vars: "vars set count=3", then "vars save", over
# <prefix>/shell/in. Fit the horn so $load_angle is the pocket-under-throat
# position before first power-up, and give the pocket a lip so the biscuit
# survives the swing. Both cron jobs need a valid system time (NTP syncs
# automatically). Retune times via schedule list -> schedule remove <id> ->
# schedule add <new six fields> <command>.
# Wiring: servo signal D2 (GPIO4), servo V+ 5V, common GND. Hopper: a
# one-biscuit-wide throat above the drum, exit chute below the deliver side.

init
  servo attach 4
  schedule add 0 30 7 * * * run /scripts/drum-feeder.hx
  schedule add 0 30 18 * * * run /scripts/drum-feeder.hx
  vars setdefault count=2
  vars setdefault load_angle=0
  vars setdefault deliver_angle=180
  vars setdefault settle=700
  vars setdefault pass=700
end

set n = $count
if $n < 1
  set n = 1
end
if $n > 12
  set n = 12
end
while $n > 0
  servo write 4 $load_angle
  delay $settle
  servo write 4 $deliver_angle
  delay $pass
  set n = $n - 1
end
servo detach 4
