#include "gpio_api.h"

#include "pins.h"
#include "motor.h"
#include "servo.h"

namespace harixos {
namespace api {

bool GpioAPI::isAvailablePin(uint8_t pin) {
  return pins::isAvailable(pin);
}

bool GpioAPI::isBootStrapPin(uint8_t pin) {
  return pin == 0 || pin == 2 || pin == 15;
}

ApiResult GpioAPI::setMode(uint8_t pin, uint8_t mode) {
  if (!isAvailablePin(pin)) {
    return ApiResult(API_INVALID_PIN, "GPIO" + String(pin) + " is reserved or invalid");
  }
  
  if (mode != INPUT && mode != OUTPUT && mode != INPUT_PULLUP) {
    return ApiResult(API_INVALID_GPIO_MODE, "Invalid GPIO mode");
  }
  
  pinMode(pin, mode);
  return ApiResult(API_OK, "GPIO" + String(pin) + " mode set");
}

ApiResult GpioAPI::write(uint8_t pin, uint8_t value) {
  if (!isAvailablePin(pin)) {
    return ApiResult(API_INVALID_PIN, "GPIO" + String(pin) + " is reserved or invalid");
  }
  
  // Warn about boot-critical pins
  if (isBootStrapPin(pin)) {
    if ((pin == 0 || pin == 2) && value == LOW) {
      return ApiResult(API_INVALID_ARGUMENT, "Cannot set GPIO0/GPIO2 LOW (boot required HIGH)");
    }
    if (pin == 15 && value == HIGH) {
      return ApiResult(API_INVALID_ARGUMENT, "Cannot set GPIO15 HIGH (boot required LOW)");
    }
  }
  
  pinMode(pin, OUTPUT);
  digitalWrite(pin, value);
  return ApiResult(API_OK, "GPIO" + String(pin) + " = " + (value ? "HIGH" : "LOW"));
}

GpioResult GpioAPI::read(uint8_t pin) {
  if (!isAvailablePin(pin)) {
    return GpioResult(API_INVALID_PIN, 0, "GPIO" + String(pin) + " is reserved or invalid");
  }
  
  pinMode(pin, INPUT);
  int value = digitalRead(pin);
  return GpioResult(API_OK, value, "GPIO" + String(pin) + " = " + String(value));
}

ApiResult GpioAPI::toggle(uint8_t pin) {
  if (!isAvailablePin(pin)) {
    return ApiResult(API_INVALID_PIN, "GPIO" + String(pin) + " is reserved or invalid");
  }
  
  if (isBootStrapPin(pin)) {
    return ApiResult(API_INVALID_ARGUMENT, "Cannot toggle boot-critical pins");
  }
  
  pinMode(pin, OUTPUT);
  int current = digitalRead(pin);
  digitalWrite(pin, current ? LOW : HIGH);
  return ApiResult(API_OK, "GPIO" + String(pin) + " toggled");
}

ApiResult GpioAPI::pulse(uint8_t pin, int count, int delayMs) {
  if (!isAvailablePin(pin)) {
    return ApiResult(API_INVALID_PIN, "GPIO" + String(pin) + " is reserved or invalid");
  }
  
  if (count <= 0 || delayMs < 0) {
    return ApiResult(API_INVALID_ARGUMENT, "Invalid pulse parameters");
  }
  
  pinMode(pin, OUTPUT);
  for (int i = 0; i < count; ++i) {
    digitalWrite(pin, HIGH);
    delay(delayMs);
    digitalWrite(pin, LOW);
    delay(delayMs);
    yield();
  }
  
  // Leave pin in safe state for boot-critical pins
  if (isBootStrapPin(pin)) {
    if (pin == 0 || pin == 2) {
      digitalWrite(pin, HIGH);
    } else if (pin == 15) {
      digitalWrite(pin, LOW);
    }
  }
  
  return ApiResult(API_OK, "GPIO" + String(pin) + " pulsed " + String(count) + " times");
}

ApiResult GpioAPI::beep(const String &args, Stream &out) {
  String rest = args;
  String pinTok = rest.substring(0, rest.indexOf(' '));
  pinTok.trim();
  rest = rest.substring(pinTok.length());
  rest.trim();
  String hzTok = rest.substring(0, rest.indexOf(' '));
  hzTok.trim();
  String msTok = rest.substring(hzTok.length());
  msTok.trim();
  if (pinTok.length() == 0) {
    return ApiResult(API_INVALID_ARGUMENT, "Usage: beep <pin> [hz] [ms]");
  }
  int pin = pinTok.toInt();
  int hz = hzTok.length() ? hzTok.toInt() : 1000;
  int ms = msTok.length() ? msTok.toInt() : 200;
  if (!isAvailablePin(pin)) {
    return ApiResult(API_INVALID_PIN, "GPIO" + String(pin) + " is reserved or invalid");
  }
  // ponytail: tone() collides with ledc/servo drivers; check live users.
  if (ServoAPI::findByPin(static_cast<uint8_t>(pin))) {
    return ApiResult(API_INVALID_ARGUMENT, "GPIO" + String(pin) + " is attached to a servo");
  }
  for (int i = 0; i < MotorAPI::kMaxMotors; ++i) {
    MotorAPI *m = MotorAPI::findByIndex(static_cast<uint8_t>(i));
    if (m && m->initialized() &&
        (m->config().namePin == pin || m->config().speedPin == pin)) {
      return ApiResult(API_INVALID_ARGUMENT, "GPIO" + String(pin) + " is used by a motor");
    }
  }
  if (hz < 37 || hz > 5000) {
    return ApiResult(API_INVALID_ARGUMENT, "hz must be 37-5000");
  }
  if (ms < 10 || ms > 10000) {
    return ApiResult(API_INVALID_ARGUMENT, "ms must be 10-10000");
  }
  tone(static_cast<uint8_t>(pin), static_cast<unsigned int>(hz));
  delay(static_cast<unsigned long>(ms));
  noTone(static_cast<uint8_t>(pin));
  return ApiResult(API_OK, "Beep GPIO" + String(pin) + " " + String(hz) + "Hz " + String(ms) + "ms");
}

bool GpioAPI::isPwmPin(uint8_t pin) {
  // ESP8266 ledc supports PWM on all GPIO except GPIO6-11 (flash) and GPIO1/3 (UART)
  // GPIO0, 2, 4, 5, 12, 13, 14, 15 all support PWM
  return isAvailablePin(pin);
}

void GpioAPI::listAvailablePins(Stream &output) {
  uint32_t flashSize = ESP.getFlashChipRealSize();
  bool isEsp01 = (flashSize == 1048576);
  
  output.println(F("GPIO"));
  output.printf("  Chip ID: 0x%08X\r\n", ESP.getChipId());
  
  if (isEsp01) {
    output.println(F("  Board: ESP-01 (1MB flash)"));
    output.println(F("  Exposed pins: GPIO0, GPIO2"));
    output.println(F("  Boot pins: GPIO0=HIGH, GPIO2=HIGH"));
  } else {
    output.println(F("  Board: ESP8266 standard (4MB+ flash)"));
    output.println(F("  Usable pins: GPIO0, GPIO2, GPIO4, GPIO5, GPIO12, GPIO13, GPIO14, GPIO15, GPIO16"));
    output.println(F("  Boot pins: GPIO0=HIGH, GPIO2=HIGH, GPIO15=LOW"));
  }
  
  output.println(F("  Reserved: GPIO1/3 (UART), GPIO6-11 (flash)"));
}

}  // namespace api
}  // namespace harixos
