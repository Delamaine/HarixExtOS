# parking-assist.hx -- self-contained: paste into notepad, then run it once
# with "run /scripts/parking-assist.hx". The init block registers the check
# schedule and the default distances once per boot; "vars setdefault" never
# overwrites distances you set remotely, and the vars store auto-loads at boot.
# Buzzer beeps faster as the car closes in: slow under $warn cm, fast under
# $stop cm. Reads the registered HC-SR04's cached distance via the
# "sensor <name> distance" token.
# Wiring: HC-SR04 trig on D5 (GPIO14), echo on D6 (GPIO12), buzzer + on
# D1 (GPIO5) through a 100R resistor, buzzer - to GND.

init
  schedule add */2 * * * * * run /scripts/parking-assist.hx
  vars setdefault warn=80
  vars setdefault stop=30
end

sensor publish garage
set d = sensor garage distance
if $d < $stop
  beep 5 2000 250
else
  if $d < $warn
    beep 5 1000 120
  end
end
