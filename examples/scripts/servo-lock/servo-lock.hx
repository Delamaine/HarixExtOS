# servo-lock.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/servo-lock.hx". The init block attaches the bolt servo,
# registers the door edge rule, the check schedule and the defaults once per
# boot. Lock control over <prefix>/shell/in: "vars set lock=1" (throw the
# bolt) / =0 (withdraw). The interlock: locking is refused while the reed
# switch shows the door open -- one beep (edge-gated by warned, so no
# buzzing) -- and the request completes automatically the moment the door
# closes. Every applied state is acked on stat/harixos/lock. applied is
# RAM-only (-1 at boot), so the first tick re-asserts $lock; fit the servo
# horn so $locked_angle is the thrown position before first power-up.
# Wiring: servo signal D2 (GPIO4), reed D1 (GPIO5) to GND + external 10k
# pull-up to 3V3 ("onchange add" forces plain INPUT), buzzer + on D5 (GPIO14)
# through a 100R resistor, buzzer - to GND. Servo V+ 5V, common GND.

init
  servo attach 4
  onchange add 5 both door
  schedule add */2 * * * * * run /scripts/servo-lock.hx
  vars setdefault lock=1
  vars setdefault applied=-1
  vars setdefault warned=0
  vars setdefault locked_angle=0
  vars setdefault unlocked_angle=180
end

# Out-of-range lock values are repaired toward 1 (locked): a typo should
# never fail open.
if $lock < 0
  set lock = 1
end
if $lock > 1
  set lock = 1
end

set open = readpin 5
if $lock == $applied
  set warned = 0
else
  if $lock == 0
    servo write 4 $unlocked_angle
    set applied = 0
    mqtt pub stat/harixos/lock $applied
  else
    if $open == 0
      servo write 4 $locked_angle
      set applied = 1
      mqtt pub stat/harixos/lock $applied
    else
      if $warned == 0
        beep 14 300 250
        set warned = 1
      end
    end
  end
end
