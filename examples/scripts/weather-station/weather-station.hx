# weather-station.hx -- self-contained: paste into notepad, then run it once
# with "run /scripts/weather-station.hx". The init block registers the publish
# schedule and the default frost limit once per boot; "vars setdefault" never
# overwrites a limit you set remotely, and the vars store auto-loads at boot.
# Every 15 min: read BME280 + BH1750, publish retained telemetry (Home
# Assistant picks both up via discovery), then beep a frost alarm when the
# cached temperature drops below $frost via the "sensor <name> temperature" token.
# Wiring: BME280 and BH1750 on I2C: SDA on D2 (GPIO4), SCL on D1 (GPIO5),
# buzzer + on D5 (GPIO14) through a 100R resistor, buzzer - to GND.

init
  schedule add 0 */15 * * * * * run /scripts/weather-station.hx
  vars setdefault frost=2
end

sensor publish weather
sensor publish light
set t = sensor weather temperature
if $t < $frost
  beep 14 800 500
end
