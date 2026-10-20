# remote-button.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/remote-button.hx" on the BUTTON device. The init block
# registers the edge rule, the poll schedule and the target default once per
# boot. Every 2 s the script compares the button pin against the RAM-only
# prev flag; on a closed->open edge it publishes "vars set cmd=open" to the
# other device's shell/in over MQTT -- pair it with curtain-motor, lamp-remote
# or any example driven by a vars knob. A failed publish rolls prev back
# inside try/catch ("released"), so the next 2 s poll re-publishes a still-
# held button instead of swallowing the press. The target topic is a knob:
# "vars set target=other-prefix/shell/in" + "vars save". Payloads may contain
# spaces -- mqtt pub takes everything after the topic. Button edges also
# reach MQTT via onchange (<prefix>/gpio/4 + HA binary_sensor class door).
# Wiring: momentary button D2 (GPIO4) to GND + external 10k pull-up to 3V3
# ("onchange add" forces plain INPUT).

init
  onchange add 4 both door
  schedule add */2 * * * * * run /scripts/remote-button.hx
  vars setdefault target=hx-lamp/shell/in
  vars setdefault prev=0
end

set pressed = readpin 4
if $pressed != $prev
  set prev = $pressed
  if $pressed == 0
    try
      mqtt pub $target vars set cmd=open
    catch
      set prev = 1
    end
  end
end
