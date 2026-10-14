# pump-interlock.hx -- short check run by the scheduler every 30 s; exits at once.
# Pump relay on while the A0 reading exceeds the remotely-set threshold target:
#   vars set target=650  then  vars save   (both on <prefix>/shell/in)
# Wiring: capacitive soil sensor on A0 (dry = high reading), pump relay module on D1 (GPIO5).

vars load
set raw = adc
if $raw > $target
  relay set pump on
else
  relay set pump off
end
