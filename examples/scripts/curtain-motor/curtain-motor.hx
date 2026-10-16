# curtain-motor.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/curtain-motor.hx". The init block initializes motor m1, both
# limit-switch edge rules, the 1 s command schedule and the defaults once per
# boot. Remote drive over <prefix>/shell/in:
#   vars set cmd=open    vars set cmd=close
# Motor speed is $pct (default 75 %, tune with vars set pct=60 then vars save).
# The command is RAM-only (idle after reboot). Travel ends at the limit switch
# or after $travel seconds, whichever comes first -- a while loop with break.
# A travel run blocks shell/scheduler for up to $travel seconds.
# Wiring: driver direction D5 (GPIO14), driver speed/PWM D6 (GPIO12); open-limit
# switch D2 (GPIO4) and close-limit switch D7 (GPIO13), each to GND with an
# external 10k pull-up to 3V3 ("onchange add" forces plain INPUT).

init
  motor init m1
  onchange add 4 both window
  onchange add 13 both window
  schedule add * * * * * * run /scripts/curtain-motor.hx
  vars setdefault cmd=idle
  vars setdefault travel=9
  vars setdefault pct=75
end

if $cmd == open
  set cmd = idle
  if readpin 4 == 0
    motor stop m1
  else
    motor speed $pct m1
    motor forward m1
    set deadline = time + $travel
    while time < $deadline
      if readpin 4 == 0
        break
      end
      delay 100
    end
    motor brake m1
  end
end
if $cmd == close
  set cmd = idle
  if readpin 13 == 0
    motor stop m1
  else
    motor speed $pct m1
    motor reverse m1
    set deadline = time + $travel
    while time < $deadline
      if readpin 13 == 0
        break
      end
      delay 100
    end
    motor brake m1
  end
end
