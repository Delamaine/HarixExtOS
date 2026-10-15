# fan-control.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/fan-control.hx". The init block registers the fan relay, the
# check schedule and the default thresholds once per boot; "vars setdefault"
# never overwrites targets you set remotely, and the vars store auto-loads at boot.
# Fan relay on above $on_at degC, off below $off_at degC (hysteresis). Reads the
# registered DS18B20's cached temperature via the "sensor <name> temperature" token.
# Wiring: DS18B20 on D2 (GPIO4) with 4.7k pull-up to 3V3, fan relay module on D1 (GPIO5).

init
  relay add 5 fan
  schedule add */2 * * * * * run /scripts/fan-control.hx
  vars setdefault on_at=30
  vars setdefault off_at=27
end

sensor publish room
set t = sensor room temperature
if $t >= $on_at
  relay set fan on
else
  if $t <= $off_at
    relay set fan off
  end
end
