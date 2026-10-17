# desk-climate.hx -- self-contained: paste into notepad, then run it once
# with "run /scripts/desk-climate.hx". Registers the AHT20 as "desk" over
# shell/I2C first (see README), then every 30 s: read + publish the sensor
# (Home Assistant picks it up via discovery) and paint a 4-line OLED report
# with a comfort word between the $t_min/$t_max thresholds. Knobs expand
# $vars: "vars set t_max=28" / "vars set t_min=17", then "vars save", over
# <prefix>/shell/in. Both are setdefaulted in init, never overwritten.
# Wiring: AHT20 on I2C: SDA on D2 (GPIO4), SCL on D1 (GPIO5), VCC 3.3V,
# GND to GND; SSD1306 on the same bus (0x3C), AHT20 answers at 0x38.

init
  oled init
  vars setdefault t_min=18
  vars setdefault t_max=26
end

sensor publish desk
set t = sensor desk temperature
set h = sensor desk humidity
oled line 0 DESK CLIMATE
oled line 1 TEMP $t C
oled line 2 HUM $h %
if $t > $t_max
  oled line 3 WARM
else
  if $t < $t_min
    oled line 3 COOL
  else
    oled line 3 COMFORTABLE
  end
end
