# rain-closure.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/rain-closure.hx". The init block initializes motor m1,
# registers the rain probe, both limit-switch edge rules, the 5 s check
# schedule and the defaults once per boot. While the probe reads below $wet
# (rain; same dry=high convention as leak-webhook), the script posts a
# webhook and runs one close travel (motor reverse until the close-limit
# switch or $travel seconds) -- once per wet episode, re-armed by the
# RAM-only closed flag when the probe dries. Opening is deliberate:
# "vars set cmd=open" over <prefix>/shell/in, never automatic; if rain
# returns while open, the next poll closes again. Knobs: $wet, $travel,
# $pct all expand as $vars. Run "vars save" only while dry (closed=0):
# persisting the latch would stop the auto-close after a power cut.
# Wiring: driver direction D5 (GPIO14), speed/PWM D6 (GPIO12); moisture
# probe AOUT on A0; open-limit switch D2 (GPIO4), close-limit switch D7
# (GPIO13), each to GND with an external 10k pull-up to 3V3 ("onchange add"
# forces plain INPUT).

init
  motor init m1
  sensor register soil A0 rain
  onchange add 4 both window
  onchange add 13 both window
  schedule add */5 * * * * * run /scripts/rain-closure.hx
  vars setdefault wet=400
  vars setdefault travel=8
  vars setdefault pct=75
  vars setdefault closed=0
  vars setdefault cmd=idle
end

if $cmd == open
  set cmd = idle
  set closed = 0
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

sensor publish rain
set raw = sensor rain moisture
if $raw < $wet
  if $closed == 0
    set closed = 1
    post https://ntfy.sh/harixos-rain {"event":"rain","raw":"$raw"}
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
else
  set closed = 0
end
