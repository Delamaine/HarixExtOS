# pan-tilt-remote.hx -- self-contained: paste into notepad, then run it once
# with "run /scripts/pan-tilt-remote.hx". The init block attaches both
# servos and registers the 1 s poll schedule plus the pose defaults once per
# boot. Drive the mount over <prefix>/shell/in: "vars set pan=45" /
# "vars set tilt=120" (0-180 each). A changed knob is actuated and acked on
# stat/harixos/pan / stat/harixos/tilt; values outside 0-180 are ignored
# (no ack, axis holds). Both knobs default to -1 = untouched: a fresh boot
# parks the mount where it is and moves only when commanded, while
# "vars set pan=90; vars save" makes a pose survive reboots -- applied_* is
# RAM-only, so the first tick re-asserts the saved knobs.
# Wiring: pan servo signal D2 (GPIO4), tilt servo signal D7 (GPIO13),
# servo V+ 5V, common GND.

init
  servo attach 4
  servo attach 13
  schedule add * * * * * * run /scripts/pan-tilt-remote.hx
  vars setdefault pan=-1
  vars setdefault tilt=-1
  vars setdefault applied_pan=-1
  vars setdefault applied_tilt=-1
end

if $pan != $applied_pan
  if $pan >= 0
    if $pan <= 180
      servo write 4 $pan
      set applied_pan = $pan
      mqtt pub stat/harixos/pan $applied_pan
    end
  end
end
if $tilt != $applied_tilt
  if $tilt >= 0
    if $tilt <= 180
      servo write 13 $tilt
      set applied_tilt = $tilt
      mqtt pub stat/harixos/tilt $applied_tilt
    end
  end
end
