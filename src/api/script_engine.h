#ifndef HARIXOS_SCRIPT_ENGINE_H
#define HARIXOS_SCRIPT_ENGINE_H

#include "api_types.h"
#include <Arduino.h>

namespace harixos {
namespace api {

// ===== Script Engine for .hx files =====
// .hx scripts use API commands for safe board control
// Example:
//   gpio 2 on
//   delay 1000
//   gpio 2 off
//   wifi scan
//   set count = 0
//   if count > 5
//     print done
//   end
// The full keyword list - including heap, uptime, adc, calc, pwd, cd, ls,
// cat, write, settings, time - is printed by `help` run inside a script or a
// scheduled command; see ScriptEngine::printHelp.

class ScriptEngine {
public:
  // Execute a .hx script from string
  static ApiResult executeScript(const String &script, Stream &output = Serial);
  
  // Execute a single command
  static ApiResult executeCommand(const String &command, Stream &output = Serial);
  
  // List available script commands
  static void printHelp(Stream &output = Serial);
  
private:
  // Parse and execute individual commands
  static ApiResult handleGpioCommand(const String &args, Stream &output);
  static ApiResult handleWifiCommand(const String &args, Stream &output);
  static ApiResult handleDelayCommand(const String &args, Stream &output);
  static ApiResult handleSystemCommand(const String &args, Stream &output);
  static ApiResult handlePrintCommand(const String &args, Stream &output);
  static ApiResult handleSetCommand(const String &args, Stream &output);
  static ApiResult handleRunCommand(const String &args, Stream &output);

  // Keyword groups widened so `schedule add` can run real commands. Each
  // delegates to the API or filesystem layer; none reimplements a shell
  // handler. See script_engine.cpp for the per-keyword mapping.
  static ApiResult handleSystemValueCommand(const String &name, const String &args, Stream &output);
  static ApiResult handleFilesystemCommand(const String &name, const String &args, Stream &output);
  static ApiResult handleSettingsTimeCommand(const String &name, const String &args, Stream &output);
  // Single IoT handler group (post, mqtt, onchange).
  static ApiResult handleIotCommand(const String &name, const String &args, Stream &output);
  
  // Tokenize a line into parts
  struct Command {
    String name;
    String args;
  };
  static Command parseCommand(const String &line);
};

}  // namespace api
}  // namespace harixos

#endif
