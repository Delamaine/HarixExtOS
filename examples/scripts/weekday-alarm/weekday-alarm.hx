# weekday-alarm.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/weekday-alarm.hx". The init block registers two cron jobs once
# per boot: 07:00 Mon-Fri runs this script (three beeps + lamp on), and a
# second job turns the lamp off at 07:30. Six-field cron: sec min hour dom
# month dow, dow 1,2,3,4,5 = Mon-Fri (0 = Sunday). Both jobs need a valid
# system time; NTP syncs automatically once Wi-Fi is up.
# Retune over <prefix>/shell/in: schedule list -> schedule remove <id> ->
# schedule add <new six fields> <command>.
# Wiring: lamp relay on D1 (GPIO5), buzzer + on D5 (GPIO14) through a 100R
# resistor, buzzer - to GND.

init
  relay add 5 lamp
  schedule add 0 0 7 * * 1,2,3,4,5 run /scripts/weekday-alarm.hx
  schedule add 0 30 7 * * 1,2,3,4,5 relay set lamp off
end

beep 14 880 300
delay 400
beep 14 880 300
delay 400
beep 14 880 300
relay set lamp on
