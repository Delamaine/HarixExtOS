# scene-ack.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/scene-ack.hx". The init block registers both relays, the poll
# schedule and the defaults once per boot. Scene control over
# <prefix>/shell/in: "vars set scene=0" (off) / =1 (lamp) / =2 (lamp+strip).
# Whenever the scene knob changes, the script actuates the relays and
# acknowledges the new value on stat/harixos/scene via raw mqtt pub -- the
# pattern for any remote knob that needs confirmation. prev starts at -1 so
# the first observed scene always actuates; it is RAM-only. Unknown scene
# numbers actuate nothing but are still acked (the ack mirrors what was
# received). Wiring: lamp relay D1 (GPIO5), strip relay D6 (GPIO12).

init
  relay add 5 lamp
  relay add 12 strip
  schedule add */2 * * * * * run /scripts/scene-ack.hx
  vars setdefault scene=0
  vars setdefault prev=-1
end

if $scene != $prev
  set prev = $scene
  if $scene == 0
    relay set lamp off
    relay set strip off
  end
  if $scene == 1
    relay set lamp on
    relay set strip off
  end
  if $scene == 2
    relay set lamp on
    relay set strip on
  end
  mqtt pub stat/harixos/scene $scene
end
