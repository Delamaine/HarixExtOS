# pin-mirror.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/pin-mirror.hx". The init block registers the edge publication, the
# 1 s mirror schedule, and the force default once per boot. Remote override on
# <prefix>/shell/in:
#   vars set force=1  (on)   vars set force=0  (off)   vars set force=2  (follow input again)
#   then vars save -- the vars store auto-loads at boot and setdefault keeps the saved value.
# Input edges are published by onchange (<prefix>/gpio/4).
# Wiring: slide switch D2 (GPIO4) to 3V3 + external 10k pull-down to GND (onchange forces
# plain INPUT); mirrored output on D1 (GPIO5), relay module or LED + 330R.

init
  relay add 5 mirror
  onchange add 4 both
  schedule add * * * * * * run /scripts/pin-mirror.hx
  vars setdefault force=2
end

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
