#include "oled.h"

#include <Wire.h>

#include "oled_font.h"

namespace harixos {
namespace api {

namespace {

// 0 = not initialized; a failed init resets it so later commands re-error.
uint8_t s_addr = 0;
const char* s_lastError = nullptr;

bool isDigits(const String& s) {
  if (s.length() == 0) return false;
  for (unsigned int i = 0; i < s.length(); ++i) {
    if (!isDigit(s[i])) return false;
  }
  return true;
}

// Control byte 0x00 = command stream.
bool sendCmd(const uint8_t* b, uint8_t n) {
  Wire.beginTransmission(s_addr);
  Wire.write(0x00);
  Wire.write(b, n);
  return Wire.endTransmission() == 0;
}

// Control byte 0x40 = data stream; Wire's buffer is 128 B, so chunk at 64.
bool sendData(const uint8_t* d, uint16_t n) {
  while (n > 0) {
    uint8_t chunk = n > 64 ? 64 : (uint8_t)n;
    Wire.beginTransmission(s_addr);
    Wire.write(0x40);
    Wire.write(d, chunk);
    if (Wire.endTransmission() != 0) return false;
    d += chunk;
    n -= chunk;
  }
  return true;
}

// Page addressing: page 0..7 + column nibbles.
bool setPageCol(uint8_t page, uint8_t col) {
  uint8_t cmds[3] = {(uint8_t)(0xB0 | page), (uint8_t)(0x00 | (col & 0x0F)),
                     (uint8_t)(0x10 | (col >> 4))};
  return sendCmd(cmds, 3);
}

bool clearAll() {
  static const uint8_t kZeros[64] = {0};
  for (uint8_t p = 0; p < 8; ++p) {
    if (!setPageCol(p, 0) || !sendData(kZeros, 64) || !sendData(kZeros, 64)) {
      return false;
    }
  }
  return true;
}

bool hwInit(uint8_t addr) {
  static const uint8_t kInit[] = {
      0xAE,        // display off
      0xD5, 0x80,  // clock divide
      0xA8, 0x3F,  // multiplex 64
      0xD3, 0x00,  // display offset
      0x40,        // start line 0
      0x8D, 0x14,  // charge pump on
      0x20, 0x02,  // page addressing
      0xA1, 0xC8,  // segment remap + COM scan direction
      0xDA, 0x12,  // COM pins
      0x81, 0x7F,  // contrast
      0xD9, 0xF1,  // precharge
      0xDB, 0x40,  // VCOMH
      0xA4,        // resume from RAM
      0xA6,        // normal (not inverted)
      0xAF,        // display on
  };
  s_addr = addr;
  if (!sendCmd(kInit, sizeof(kInit))) {
    s_lastError = "no SSD1306 on I2C";
    s_addr = 0;
    return false;
  }
  if (!clearAll()) {
    s_lastError = "SSD1306 write failed";
    s_addr = 0;
    return false;
  }
  return true;
}

bool requireInit() {
  if (s_addr == 0) {
    s_lastError = "oled not initialized (oled init)";
    return false;
  }
  return true;
}

const char* kUsage =
    "Usage: oled init [addr]|clear|line <0-7> <text>|on|off|status";

}  // namespace

ApiResult OledAPI::runCommand(const String& args, Stream& out) {
  String rest = args;
  rest.trim();
  if (rest.length() == 0) {
    return ApiResult(API_INVALID_ARGUMENT, kUsage);
  }
  int sp = rest.indexOf(' ');
  String action = (sp > 0) ? rest.substring(0, sp) : rest;
  String tail = (sp > 0) ? rest.substring(sp + 1) : "";
  action.trim();
  action.toLowerCase();
  tail.trim();

  if (action == "init") {
    uint8_t addr = 0x3C;
    if (tail.length() > 0) {
      if (!isDigits(tail)) {
        return ApiResult(API_INVALID_ARGUMENT, "i2c address must be decimal (60=0x3C, 61=0x3D)");
      }
      int v = tail.toInt();
      if (v < 0x08 || v > 0x77) {
        return ApiResult(API_INVALID_ARGUMENT, "i2c address out of range (8..119)");
      }
      addr = (uint8_t)v;
    }
    if (!hwInit(addr)) {
      return ApiResult(API_ERROR, s_lastError);
    }
    out.printf("oled initialized at 0x%02X\r\n", addr);
    return ApiResult(API_OK, "");
  }

  if (!requireInit()) {
    return ApiResult(API_ERROR, s_lastError);
  }

  if (action == "clear") {
    if (!clearAll()) return ApiResult(API_ERROR, "SSD1306 write failed");
    return ApiResult(API_OK, "");
  }
  if (action == "on") {
    if (!sendCmd((const uint8_t*)"\xAF", 1)) return ApiResult(API_ERROR, "SSD1306 write failed");
    return ApiResult(API_OK, "");
  }
  if (action == "off") {
    if (!sendCmd((const uint8_t*)"\xAE", 1)) return ApiResult(API_ERROR, "SSD1306 write failed");
    return ApiResult(API_OK, "");
  }
  if (action == "line") {
    int sp2 = tail.indexOf(' ');
    if (sp2 < 0) return ApiResult(API_INVALID_ARGUMENT, kUsage);
    String pageStr = tail.substring(0, sp2);
    if (!isDigits(pageStr) || pageStr.toInt() > 7) {
      return ApiResult(API_INVALID_ARGUMENT, "line number must be 0..7");
    }
    String text = tail.substring(sp2 + 1);  // may contain spaces
    uint8_t buf[OLED_LINE_BYTES];
    oledPackLine(text.c_str(), buf);
    if (!setPageCol((uint8_t)pageStr.toInt(), 0) || !sendData(buf, OLED_LINE_BYTES)) {
      return ApiResult(API_ERROR, "SSD1306 write failed");
    }
    return ApiResult(API_OK, "");
  }
  if (action == "status") {
    if (s_addr) out.printf("oled: inited at 0x%02X\r\n", s_addr);
    else out.println(F("oled: not initialized"));
    return ApiResult(API_OK, "");
  }
  return ApiResult(API_INVALID_ARGUMENT, kUsage);
}

}  // namespace api
}  // namespace harixos
