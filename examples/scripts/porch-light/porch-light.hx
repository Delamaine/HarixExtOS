# porch-light.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/porch-light.hx". The init block registers the relay, the motion
# edge rule, the 1 s check schedule and the defaults once per boot; the deadline
# is RAM-only and resets to 0 at boot, so the light always starts off.
# PIR motion keeps the light on for $secs after the last trigger (remote:
# vars set secs=180 then vars save). Deadline math uses the "time" value token
# (epoch seconds) -- no loop, the script exits every tick.
# Wiring: PIR OUT on D7 (GPIO13), relay module on D1 (GPIO5). The PIR output is
# push-pull 3.3V, so it is safe on a GPIO; "onchange add" forces plain INPUT.

init
  relay add 5 light
  onchange add 13 both motion
  schedule add * * * * * * run /scripts/porch-light.hx
  vars setdefault secs=120
  vars setdefault until=0
end

set now = time
if readpin 13 == 1
  set until = $now + $secs
  relay set light on
else
  if $now >= $until
    relay set light off
  end
end
