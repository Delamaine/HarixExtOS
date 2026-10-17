# sos-alarm.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/sos-alarm.hx". The init block registers the button edge rule,
# the poll schedule and the defaults once per boot. On each button press the
# script POSTs a rich ntfy JSON notification -- title, priority and tags in
# one body -- to the topic in $topic. A $cooldown second gap (epoch-based)
# stops a bouncing or held button from spamming the topic. JSON bodies must
# stay space-free (post splits arguments on spaces). Button edges also reach
# MQTT via onchange (<prefix>/gpio/4 + HA binary_sensor class door).
# Wiring: momentary button D2 (GPIO4) to GND + external 10k pull-up to 3V3
# ("onchange add" forces plain INPUT), pressed = LOW.

init
  onchange add 4 both door
  schedule add */2 * * * * * run /scripts/sos-alarm.hx
  vars setdefault topic=harixos-sos
  vars setdefault cooldown=60
  vars setdefault last=0
  vars setdefault prev=1
end

set pressed = readpin 4
if $pressed != $prev
  set prev = $pressed
  if $pressed == 0
    set now = time
    if $now - $last >= $cooldown
      set last = $now
      post https://ntfy.sh {"topic":"$topic","title":"SOS","message":"help","priority":"urgent","tags":["warning","sos"]}
    end
  end
end
