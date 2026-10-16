# daily-digest.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/daily-digest.hx". The init block registers a daily 08:00 cron
# job once per boot. Each run prints a one-line health digest (heap, uptime,
# epoch) to the console/web UI and posts the same numbers as JSON to ntfy --
# print and post expand $vars leniently, so the braces and quotes in the JSON
# body survive untouched while $h/$up/$now substitute. Needs a valid system
# time; NTP syncs automatically once Wi-Fi is up. Retune the hour via
# schedule list -> schedule remove <id> -> schedule add <new six fields>
# <command>, and the ntfy topic with "vars set topic=yourtopic" + "vars save".
# Wiring: none.

init
  schedule add 0 0 8 * * * run /scripts/daily-digest.hx
  vars setdefault topic=harixos-digest
end

set h = heap
set up = uptime
set now = time
print heap=$h uptime=$up time=$now
post https://ntfy.sh/$topic {"heap":"$h","uptime":"$up","time":"$now"}
