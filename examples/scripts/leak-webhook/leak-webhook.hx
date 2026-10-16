# leak-webhook.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/leak-webhook.hx". The init block registers the check schedule
# and the defaults once per boot; "vars setdefault" never overwrites a
# threshold you set remotely. When the soil sensor's cached moisture falls
# below $wet (wet floor / leak), the script posts a webhook once per wet
# episode: the RAM-only active flag edge-triggers the post and re-arms when
# the sensor dries (after a reboot a still-wet sensor posts once more).
# The webhook body embeds the live reading via $reading -- post expands
# $vars leniently (defined vars substitute, undefined $name stays literal,
# so JSON survives). Put your own ntfy/ntfy.sh (or any HTTP) topic URL in
# the post line.
# Wiring: capacitive soil sensor on A0 (dry = high reading).

init
  schedule add */30 * * * * * run /scripts/leak-webhook.hx
  vars setdefault wet=400
  vars setdefault active=0
end

sensor publish sump
set raw = sensor sump moisture
if $raw < $wet
  if $active == 0
    set active = 1
    set reading = $raw
    post https://ntfy.sh/harixos-leak {"event":"leak","raw":"$reading"}
  end
else
  set active = 0
end
