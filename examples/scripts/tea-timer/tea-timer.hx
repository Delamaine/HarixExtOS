# tea-timer.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/tea-timer.hx" (registers the kettle relay), then trigger a
# brew from <prefix>/shell/in with "run /scripts/tea-timer.hx" again. A run
# turns the relay on for $minutes (clamped to 1-16), then off with a beep.
# Knobs expand $vars: "vars set minutes=3" then "vars save". 16 minutes is
# the arithmetic ceiling here -- larger millisecond values format in
# scientific notation the delay parser cannot read. A brew blocks the
# scheduler for the full duration (delay is synchronous).
# Wiring: kettle relay on D1 (GPIO5), buzzer + on D5 (GPIO14) through 100R,
# buzzer - to GND.

init
  relay add 5 kettle
  vars setdefault minutes=3
end

if $minutes < 1
  set minutes = 1
end
if $minutes > 16
  set minutes = 16
end
set ms = $minutes * 60000
print kettle on for $minutes min
relay set kettle on
delay $ms
relay set kettle off
beep 14 880 300
