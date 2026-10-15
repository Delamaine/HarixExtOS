# lamp-remote.hx -- self-contained: paste into notepad, then run it once with
# "run /scripts/lamp-remote.hx". The init block registers the schedules once per
# boot; after a reboot the persisted schedule fires again and re-runs init itself.
# Local fallback: lamp on when dark. Remote control: "relay set lamp on|off" on
# <prefix>/shell/in; delete the if/else/end block for pure remote control.
# Wiring: LDR A0 to 3V3, 10k resistor A0 to GND (bright = high reading), relay module on D1 (GPIO5).

init
  relay add 5 lamp
  schedule add */30 * * * * * run /scripts/lamp-remote.hx
  schedule add 0 */5 * * * * * sensor publish light
end

set raw = adc
if $raw <= 150
  relay set lamp on
else
  relay set lamp off
end
