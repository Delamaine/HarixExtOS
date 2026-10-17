# bridge-status.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/bridge-status.hx". The init block registers a cron job every
# 5 minutes once per boot. Each run publishes a single JSON telemetry line to
# a fixed Tasmota-style topic via raw mqtt pub -- the topic is NOT prefixed
# with <prefix>, which is the point: dashboards and bridges can subscribe to
# one known topic. The payload embeds free heap, uptime and epoch through
# $vars (mqtt pub is strict-mode: undefined $name errors, so the three sets
# must stay above the pub). Knob: "vars set topic=tele/yourname/state" +
# "vars save". Needs a valid system time for the epoch field; NTP syncs
# automatically once Wi-Fi is up. Note the firmware already publishes
# <prefix>/telemetry on its own schedule -- this example is the custom
# topic/payload recipe.
# Wiring: none.

init
  schedule add 0 */5 * * * * run /scripts/bridge-status.hx
  vars setdefault topic=tele/harixos/state
end

set h = heap
set up = uptime
set now = time
mqtt pub $topic {"heap":"$h","uptime":"$up","time":"$now"}
