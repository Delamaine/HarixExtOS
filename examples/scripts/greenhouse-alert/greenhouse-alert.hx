# greenhouse-alert.hx -- self-contained: paste into notepad, then run it once
# with "run /scripts/greenhouse-alert.hx". The init block registers the check
# schedule and the default limits once per boot; "vars setdefault" never
# overwrites limits you set remotely, and the vars store auto-loads at boot.
# Buzzer beeps when the DHT22 reading exceeds either limit. Reads the cached
# temperature/humidity via the "sensor <name> <quantity>" token.
# Wiring: DHT22 on D2 (GPIO4) with 10k pull-up to 3V3, buzzer + on D5 (GPIO14)
# through a 100R resistor, buzzer - to GND.

init
  schedule add 0 */5 * * * * run /scripts/greenhouse-alert.hx
  vars setdefault max_temp=32
  vars setdefault max_hum=70
end

sensor publish greenhouse
set t = sensor greenhouse temperature
set h = sensor greenhouse humidity
if $t > $max_temp
  beep 14 1200 400
end
if $h > $max_hum
  beep 14 1600 400
end
