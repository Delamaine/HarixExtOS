# lamp-remote.hx -- short check run by the scheduler every 30 s; exits at once.
# Local fallback: lamp on when dark. Remote control: "relay set lamp on|off" on
# <prefix>/shell/in; delete the if/else/end block for pure remote control.
# Wiring: LDR A0 to 3V3, 10k resistor A0 to GND (bright = high reading), relay module on D1 (GPIO5).

set raw = adc
if $raw <= 150
  relay set lamp on
else
  relay set lamp off
end
