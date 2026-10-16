# sun-blind.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/sun-blind.hx". The init block registers the schedule and the
# default thresholds once per boot; "vars setdefault" never overwrites targets
# you set remotely, and the vars store auto-loads at boot.
# Servo blind opens in bright light, closes in darkness (hysteresis between
# $dark and $bright, remotely retunable). Reads the registered ldr sensor's
# cached illuminance percentage via the "sensor <name> illuminance" token.
# Wiring: LDR + 10k voltage divider on A0, servo signal on D2 (GPIO4).

init
  servo attach 4
  schedule add 0 */10 * * * * run /scripts/sun-blind.hx
  vars setdefault dark=20
  vars setdefault bright=60
end

sensor publish sun
set lux = sensor sun illuminance
if $lux > $bright
  servo write 4 170
else
  if $lux < $dark
    servo write 4 10
  end
end
