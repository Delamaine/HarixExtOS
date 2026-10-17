# oled-status.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/oled-status.hx". The init block brings up the SSD1306 and
# registers a 1-second refresh schedule once per boot; every tick rewrites
# four status lines (title, epoch time, free heap, uptime). Numeric-only by
# design -- the value tokens are all numbers, so TIME shows raw epoch seconds
# (decode it in the shell with "time"). The four I2C writes cost ~50 ms of
# bus time per second at 100 kHz; harmless on full/balanced, and the schedule
# keeps ticking under powerprofile light.
# Wiring: SSD1306 128x64 on I2C: SDA on D2 (GPIO4), SCL on D1 (GPIO5),
# module VCC 3.3V, GND to GND. Most 0.96" boards answer at 0x3C; if yours
# shows nothing, try "oled init 61" (0x3D) once and change the init line.

init
  oled init
  schedule add * * * * * * run /scripts/oled-status.hx
end

set t = time
set h = heap
set u = uptime
oled line 0 HARIXOS STATUS
oled line 1 TIME $t
oled line 2 HEAP $h B
oled line 3 UPTIME $u S
