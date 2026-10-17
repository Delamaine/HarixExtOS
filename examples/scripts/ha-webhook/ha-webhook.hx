# ha-webhook.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/ha-webhook.hx". The init block registers the reed edge rule
# and the poll schedule once per boot. Every 2 s the script compares the door
# pin against the RAM-only prev flag; on either edge it POSTs to a Home
# Assistant webhook notification -- {"state":"open"} or {"state":"closed"} --
# no MQTT broker required, no HA integration config beyond the automation.
# Edit the URL to your HA host and the webhook_id you picked. Post expands
# $vars leniently; the door class edge still reaches MQTT/HA discovery too
# if the broker is up. Wiring: reed D2 (GPIO4) to GND + external 10k pull-up
# to 3V3 ("onchange add" forces plain INPUT).

init
  onchange add 4 both door
  schedule add */2 * * * * * run /scripts/ha-webhook.hx
  vars setdefault prev=0
end

set open = readpin 4
if $open != $prev
  set prev = $open
  if $open == 1
    post http://homeassistant.local:8123/api/webhook/harixos_door {"state":"open"}
  else
    post http://homeassistant.local:8123/api/webhook/harixos_door {"state":"closed"}
  end
end
