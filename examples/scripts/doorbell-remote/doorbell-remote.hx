# doorbell-remote.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/doorbell-remote.hx". The init block registers the edge publication,
# the check schedule, and the prev default once per boot (idempotent -- safe to
# re-run). The edges themselves reach MQTT via onchange (<prefix>/gpio/4 + HA
# binary_sensor) -- no script needed there.
# Door state lives in the persistent vars store (prev), auto-loaded at boot.
# Wiring: reed D2 (GPIO4) to GND + external 10k pull-up to 3V3 ("onchange add" forces plain INPUT).

init
  onchange add 4 both door
  schedule add */2 * * * * * run /scripts/doorbell-remote.hx
  vars setdefault prev=0
end

set open = readpin 4
if $open != $prev
  if $open == 1
    beep 10 880 300
  end
  set prev = $open
  vars save
end
