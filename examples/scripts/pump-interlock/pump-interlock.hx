# pump-interlock.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/pump-interlock.hx". The init block registers the schedules and the
# default threshold once per boot; "vars setdefault" never overwrites a target you
# set remotely, and the vars store auto-loads at boot (no "vars load" needed).
# Pump relay on while the A0 reading exceeds the remotely-set threshold target:
#   vars set target=650  then  vars save   (both on <prefix>/shell/in)
# Wiring: capacitive soil sensor on A0 (dry = high reading), pump relay module on D1 (GPIO5).

init
  relay add 5 pump
  schedule add */30 * * * * * run /scripts/pump-interlock.hx
  schedule add 0 */5 * * * * * sensor publish moisture
  vars setdefault target=600
end

set raw = adc
if $raw > $target
  relay set pump on
else
  relay set pump off
end
