# doorbell-remote.hx -- short check run by the scheduler every 2 s; exits at once.
# Beeps the local buzzer on door-open transitions. The edges themselves reach MQTT via
# "onchange add 4 both door" (<prefix>/gpio/4 + HA binary_sensor) -- no script needed there.
# Door state lives in the persistent vars store (prev).
# Wiring: reed D2 (GPIO4) to GND + external 10k pull-up to 3V3 ("onchange add" forces plain INPUT).

vars load
set open = readpin 4
if $open != $prev
  if $open == 1
    beep 10 880 300
  end
  set prev = $open
  vars save
end
