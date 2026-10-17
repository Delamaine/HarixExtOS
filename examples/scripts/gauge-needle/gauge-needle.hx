# gauge-needle.hx -- self-contained: paste into notepad, then run it once
# with "run /scripts/gauge-needle.hx". The init block attaches the needle
# servo and registers the schedule plus the scale knobs once per boot. Every
# 10 s the registered DHT22's cached temperature is mapped linearly onto the
# needle: $min_t degC sits at 0 deg, $max_t degC at 180 deg, clamped at both
# ends so an out-of-range reading parks the needle at the stop instead of
# hitting the servo wall. Retune the scale over <prefix>/shell/in with
# "vars set min_t=-10" / "vars set max_t=60"; a degenerate scale
# (max_t <= min_t) is repaired locally, your knobs are left alone. Reads the
# cached temperature via the "sensor <name> temperature" token.
# Wiring: DHT22 on D5 (GPIO14) with 10k pull-up to 3V3, servo signal on D2
# (GPIO4), servo V+ 5V, common GND.

init
  servo attach 4
  schedule add */10 * * * * * run /scripts/gauge-needle.hx
  vars setdefault min_t=0
  vars setdefault max_t=50
end

sensor publish dial
set t = sensor dial temperature
set hi = $max_t
if $hi <= $min_t
  set hi = $min_t + 1
end
set pos = ($t - $min_t) * 180 / ($hi - $min_t)
if $pos < 0
  set pos = 0
end
if $pos > 180
  set pos = 180
end
servo write 4 $pos
