# pin-mirror.hx -- short check run by the scheduler every 1 s; exits at once.
# Mirrors the D2 input onto the relay. Remote override on <prefix>/shell/in:
#   vars set force=1  (on)   vars set force=0  (off)   vars set force=2  (follow input again)
#   then vars save -- the vars load at the top restores state after reboot.
# Input edges are published by "onchange add 4 both" (<prefix>/gpio/4).
# Wiring: slide switch D2 (GPIO4) to 3V3 + external 10k pull-down to GND (onchange forces
# plain INPUT); mirrored output on D1 (GPIO5), relay module or LED + 330R.

vars load
set s = readpin 4
if $force < 2
  if $force == 1
    relay set mirror on
  else
    relay set mirror off
  end
else
  if $s == 1
    relay set mirror on
  else
    relay set mirror off
  end
end
