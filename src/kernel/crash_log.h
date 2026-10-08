#pragma once

#include <stddef.h>
#include <stdint.h>

namespace harixos {

// One crash-log line: reason|exccause|0xEP C1|0xEXCVADDR|0xDEPC|boots=N
// sized so kCrashMaxLines lines always fit in kCrashLogMax bytes.
constexpr size_t kCrashLineMax = 96;
constexpr size_t kCrashMaxLines = 8;
constexpr size_t kCrashLogMax = 768;

// ESP8266 rst_info.reason: 0 power-on, 1 HW WDT, 2 exception, 3 SW WDT,
// 4 software restart, 5 deep-sleep wake, 6 external reset.
bool crashReasonIsAbnormal(uint32_t reasonCode);

// Boots value of the last "boots=" occurrence in existing; 0 if none.
int crashBootCount(const char* existing);

// Writes "reason|exccause|0xEPC1|0xEXCVADDR|0xDEPC|boots=N" (hex uppercase).
// Returns bytes written, or -1 if the line is truncated.
int crashFormatLine(char* out, size_t n, const char* reasonStr, uint32_t exccause,
                    uint32_t epc1, uint32_t excvaddr, uint32_t depc, int boots);

// Appends line as a new '\n'-terminated line, dropping the oldest lines
// beyond kCrashMaxLines. Returns the new line count, or -1 if the result
// exceeds n (then out[0] = '\0').
int crashAppendLine(char* out, size_t n, const char* existing, const char* line);

}  // namespace harixos
