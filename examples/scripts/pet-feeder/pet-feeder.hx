# pet-feeder.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/pet-feeder.hx". The init block attaches the hopper servo and
# registers two daily cron jobs (07:30 and 18:30) once per boot. Each run
# swings the hopper to $open_angle for $hold ms -- food drops while the flap
# travels -- then returns it to 0, lets it settle, and detaches so the horn
# is limp between feeds (no holding-torque draw, safe under
# `powerprofile light`). Knobs expand $vars: "vars set
# open_angle=120" / "vars set hold=2500", then "vars save", over
# <prefix>/shell/in. Both jobs need a valid system time; NTP syncs
# automatically once Wi-Fi is up. Retune the times via schedule list ->
# schedule remove <id> -> schedule add <new six fields> <command>.
# Wiring: servo signal D2 (GPIO4), servo V+ 5V, common GND.

init
  servo attach 4
  schedule add 0 30 7 * * * run /scripts/pet-feeder.hx
  schedule add 0 30 18 * * * run /scripts/pet-feeder.hx
  vars setdefault open_angle=90
  vars setdefault hold=1500
end

servo write 4 $open_angle
delay $hold
servo write 4 0
delay $hold
servo detach 4
