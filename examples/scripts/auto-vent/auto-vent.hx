# auto-vent.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/auto-vent.hx". The init block attaches the vent servo and
# registers the schedule plus the thresholds once per boot. Hysteresis like
# fan-control: the vent opens at $open_at degC and stays open until the
# reading falls below $close_at (keep close_at < open_at), so it never
# chatters around one setpoint. The applied state is a RAM var acked on
# stat/harixos/vent; "vars set vent=1" / =0 nudges it manually (any other
# value is ignored), and "vars save" persists the last position so the first
# tick after a reboot re-asserts it. A fresh boot with nothing saved leaves
# the servo uncommanded until a threshold is crossed.
# Reads the registered DHT22's cached temperature via the "sensor <name>
# temperature" token.
# Wiring: DHT22 on D5 (GPIO14) with 10k pull-up to 3V3, vent servo signal on
# D2 (GPIO4), servo V+ 5V, common GND.

init
  servo attach 4
  schedule add 0 */5 * * * * run /scripts/auto-vent.hx
  vars setdefault open_at=30
  vars setdefault close_at=28
  vars setdefault open_angle=160
  vars setdefault closed_angle=20
  vars setdefault vent=-1
  vars setdefault applied=-1
end

sensor publish ventbox
set t = sensor ventbox temperature
if $t >= $open_at
  set vent = 1
end
if $t <= $close_at
  set vent = 0
end
if $vent != $applied
  if $vent == 1
    servo write 4 $open_angle
    set applied = 1
    mqtt pub stat/harixos/vent $applied
  end
  if $vent == 0
    servo write 4 $closed_angle
    set applied = 0
    mqtt pub stat/harixos/vent $applied
  end
end
