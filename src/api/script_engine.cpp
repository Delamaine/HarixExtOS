#include "script_engine.h"
#include <time.h>
#include "block_stack.h"
#include "expr.h"
#include "gpio_api.h"
#include "wifi_api.h"
#include "system_api.h"
#include "../apps/settings/settings.h"
#include "../kernel/filesystem/filesystem.h"

namespace {

constexpr int kMaxScriptDepth = 8;
int sScriptDepth = 0;

// Splits off the first whitespace-delimited word of `text` in place, so a
// handler can take one argument without pulling in main.cpp's tokenizer.
String takeWord(String &text) {
  text.trim();
  int sp = text.indexOf(' ');
  if (sp < 0) {
    String first = text;
    text = "";
    return first;
  }
  String first = text.substring(0, sp);
  text = text.substring(sp + 1);
  text.trim();
  return first;
}

bool isRecursiveFlag(const String &word) {
  return word.equalsIgnoreCase("-r") || word.equalsIgnoreCase("-R");
}

// Tracks script nesting so a self- or mutually-referencing script cannot
// recurse until the stack overflows. Unwinds on every exit path.
class ScriptDepthGuard {
 public:
  ScriptDepthGuard() : acquired_(sScriptDepth < kMaxScriptDepth) {
    if (acquired_) {
      ++sScriptDepth;
    }
  }

  ~ScriptDepthGuard() {
    if (acquired_) {
      --sScriptDepth;
    }
  }

  ScriptDepthGuard(const ScriptDepthGuard &) = delete;
  ScriptDepthGuard &operator=(const ScriptDepthGuard &) = delete;

  bool acquired() const { return acquired_; }

 private:
  bool acquired_;
};

}  // namespace

namespace harixos {
namespace api {

ScriptEngine::Command ScriptEngine::parseCommand(const String &line) {
  String trimmed = line;
  trimmed.trim();
  
  if (trimmed.length() == 0) {
    return {"", ""};
  }
  
  int spacePos = trimmed.indexOf(' ');
  if (spacePos == -1) {
    return {trimmed, ""};
  }
  
  return {trimmed.substring(0, spacePos), trimmed.substring(spacePos + 1)};
}

ApiResult ScriptEngine::handleGpioCommand(const String &args, Stream &output) {
  // gpio <pin> <action> [param]
  // Examples:
  //   gpio 2 on
  //   gpio 2 off
  //   gpio 2 read
  //   gpio 2 mode output
  //   gpio 2 pulse 5 500
  
  int space1 = args.indexOf(' ');
  if (space1 == -1) {
    return ApiResult(API_INVALID_ARGUMENT, "Usage: gpio <pin> <on|off|read|mode|pulse|list>");
  }
  
  String pinStr = args.substring(0, space1);
  String rest = args.substring(space1 + 1);
  
  uint8_t pin = pinStr.toInt();
  
  int space2 = rest.indexOf(' ');
  String action = (space2 == -1) ? rest : rest.substring(0, space2);
  String param = (space2 == -1) ? "" : rest.substring(space2 + 1);
  
  action.toLowerCase();
  
  if (action == "on" || action == "1") {
    return GpioAPI::write(pin, HIGH);
  } else if (action == "off" || action == "0") {
    return GpioAPI::write(pin, LOW);
  } else if (action == "read") {
    GpioResult result = GpioAPI::read(pin);
    output.println(result.message);
    return result;
  } else if (action == "mode") {
    param.toLowerCase();
    uint8_t mode = INPUT;
    if (param == "output" || param == "out") mode = OUTPUT;
    else if (param == "input_pullup" || param == "pullup") mode = INPUT_PULLUP;
    return GpioAPI::setMode(pin, mode);
  } else if (action == "pulse") {
    int count = param.toInt();
    if (count <= 0) count = 1;
    return GpioAPI::pulse(pin, count, 250);
  } else if (action == "toggle") {
    return GpioAPI::toggle(pin);
  } else if (action == "list") {
    GpioAPI::listAvailablePins(output);
    return ApiResult(API_OK, "GPIO list displayed");
  } else {
    return ApiResult(API_INVALID_ARGUMENT, "Unknown GPIO action: " + action);
  }
}

ApiResult ScriptEngine::handleWifiCommand(const String &args, Stream &output) {
  // wifi <action> [params]
  // Examples:
  //   wifi scan
  //   wifi connect <ssid> <password>
  //   wifi disconnect
  //   wifi status
  
  int space = args.indexOf(' ');
  String action = (space == -1) ? args : args.substring(0, space);
  String rest = (space == -1) ? "" : args.substring(space + 1);
  
  action.toLowerCase();
  
  if (action == "scan") {
    return WiFiAPI::scan(output);
  } else if (action == "connect") {
    int spacePos = rest.indexOf(' ');
    if (spacePos == -1) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: wifi connect <ssid> <password>");
    }
    String ssid = rest.substring(0, spacePos);
    String password = rest.substring(spacePos + 1);
    return WiFiAPI::connect(ssid, password);
  } else if (action == "disconnect") {
    return WiFiAPI::disconnect();
  } else if (action == "status") {
    return WiFiAPI::status(output);
  } else if (action == "ip") {
    return WiFiAPI::getIP(output);
  } else {
    return ApiResult(API_INVALID_ARGUMENT, "Unknown WiFi action: " + action);
  }
}

ApiResult ScriptEngine::handleDelayCommand(const String &args, Stream &output) {
  // delay <milliseconds>
  uint32_t ms = args.toInt();
  if (ms == 0) {
    return ApiResult(API_INVALID_ARGUMENT, "Delay must be > 0");
  }
  SystemAPI::delay(ms);
  return ApiResult(API_OK, "Delayed " + String(ms) + " ms");
}

ApiResult ScriptEngine::handleSystemCommand(const String &args, Stream &output) {
  // system <action>
  // Examples:
  //   system info
  //   system heap
  //   system reboot
  
  String action = args;
  action.toLowerCase();
  
  if (action == "info") {
    return SystemAPI::printInfo(output);
  } else if (action == "heap") {
    output.printf("Heap free: %u bytes\r\n", SystemAPI::getHeapFree());
    return ApiResult(API_OK, "Heap info displayed");
  } else if (action == "reboot") {
    SystemAPI::reboot();
    return ApiResult(API_OK, "Rebooting...");
  } else {
    return ApiResult(API_INVALID_ARGUMENT, "Unknown system action");
  }
}

ApiResult ScriptEngine::handlePrintCommand(const String &args, Stream &output) {
  // print <text>
  if (args.length() == 0) {
    output.println();
  } else {
    output.println(args);
  }
  return ApiResult(API_OK, "");
}

ApiResult ScriptEngine::handleSetCommand(const String &args, Stream &output) {
  // set <name> = <expression>
  double value = 0;
  const char *err = harixos::api::expr::assign(args.c_str(), &value);
  if (err != nullptr) {
    return ApiResult(API_INVALID_ARGUMENT, err);
  }
  return ApiResult(API_OK, "");  // empty message so executeScript prints no [OK]
}

ApiResult ScriptEngine::handleRunCommand(const String &args, Stream &output) {
  // run <path>
  String path = args;
  path.trim();
  if (path.length() == 0) {
    return ApiResult(API_INVALID_ARGUMENT, "Usage: run <path>");
  }
  
  String content = harixos::readText(path);
  if (content.length() == 0) {
    return ApiResult(API_ERROR, "Failed to read script or script empty: " + path);
  }
  
  return executeScript(content, output);
}

// --- Widened keyword groups -------------------------------------------------
// Every branch below delegates to an API/filesystem function. Deliberately no
// main.cpp handler body is reproduced: a copy would drift from the shell.

ApiResult ScriptEngine::handleSystemValueCommand(const String &name,
                                                 const String &args,
                                                 Stream &output) {
  if (name == "heap") {
    return handleSystemCommand("heap", output);
  }
  if (name == "uptime") {
    output.printf("Uptime: %u ms\r\n", SystemAPI::getUptime());
    return ApiResult(API_OK, "");
  }
  if (name == "chip") {
    SystemInfo info = SystemAPI::getInfo();
    output.printf("Chip ID: 0x%08X\r\n", info.chipId);
    output.printf("CPU frequency: %u MHz\r\n", info.cpuFreq);
    output.printf("Flash size: %u bytes\r\n", info.flashSize);
    return ApiResult(API_OK, "");
  }
  if (name == "info") {
    return SystemAPI::printInfo(output);
  }
  if (name == "adc") {
    output.printf("ADC A0: %d / 1023\r\n", analogRead(A0));
    return ApiResult(API_OK, "");
  }
  if (name == "calc") {
    double value = 0;
    if (!harixos::api::expr::evaluate(args.c_str(), value) || isnan(value)) {
      return ApiResult(API_INVALID_ARGUMENT, "Invalid expression.");
    }
    output.printf("= %.10g\r\n", value);
    return ApiResult(API_OK, "");
  }
  return ApiResult(API_INVALID_ARGUMENT, "Unknown system command: " + name);
}

ApiResult ScriptEngine::handleFilesystemCommand(const String &name,
                                                const String &args,
                                                Stream &output) {
  const String &cwd = harixos::currentWorkingDirectory;

  if (name == "pwd") {
    output.println(cwd);
    return ApiResult(API_OK, "");
  }

  if (name == "cd") {
    if (args.length() == 0) {
      output.println(cwd);
      return ApiResult(API_OK, "");
    }
    String target = harixos::resolvePath(cwd, args);
    if (!harixos::exists(target)) {
      return ApiResult(API_ERROR, "cd: no such file or directory");
    }
    if (!harixos::isDirectory(target)) {
      return ApiResult(API_ERROR, "cd: not a directory");
    }
    harixos::currentWorkingDirectory = target;
    return ApiResult(API_OK, "");
  }

  if (name == "ls") {
    String remaining = args;
    bool recursive = false;
    String pathArg;
    for (int i = 0; i < 3; ++i) {
      String word = takeWord(remaining);
      if (word.length() == 0) break;
      if (isRecursiveFlag(word)) recursive = true;
      else if (pathArg.length() == 0) pathArg = word;
    }
    String path = pathArg.length() == 0 ? cwd : harixos::resolvePath(cwd, pathArg);
    harixos::listDirectory(path, output, recursive);
    return ApiResult(API_OK, "");
  }

  if (name == "mkdir" || name == "touch" || name == "cat") {
    if (args.length() == 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: " + name + " <path>");
    }
    String path = harixos::resolvePath(cwd, args);

    if (name == "mkdir") {
      if (!harixos::makeDirectory(path)) {
        return ApiResult(API_ERROR, "mkdir: failed to create directory");
      }
      output.printf("Created directory: %s\r\n", path.c_str());
      return ApiResult(API_OK, "");
    }

    if (name == "touch") {
      if (!harixos::touch(path)) {
        return ApiResult(API_ERROR, "touch: failed");
      }
      output.printf("Touched: %s\r\n", path.c_str());
      return ApiResult(API_OK, "");
    }

    String content = harixos::readText(path);
    if (content.length() == 0 && !harixos::exists(path)) {
      return ApiResult(API_ERROR, "cat: file not found");
    }
    output.print(content);
    if (!content.endsWith("\n")) {
      output.println();
    }
    return ApiResult(API_OK, "");
  }

  if (name == "rm") {
    String remaining = args;
    String first = takeWord(remaining);
    if (first.length() == 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: rm [-r] <path>");
    }
    bool recursive = isRecursiveFlag(first);
    String pathArg = recursive ? remaining : first;
    if (pathArg.length() == 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: rm [-r] <path>");
    }
    String path = harixos::resolvePath(cwd, pathArg);
    if (!harixos::exists(path)) {
      return ApiResult(API_ERROR, "rm: path not found");
    }
    if (!harixos::removePath(path)) {
      return ApiResult(API_ERROR, "rm: failed");
    }
    output.printf("Removed: %s%s\r\n", path.c_str(), recursive ? " (recursive)" : "");
    return ApiResult(API_OK, "");
  }

  if (name == "cp" || name == "mv") {
    String remaining = args;
    String firstArg = takeWord(remaining);
    String secondArg = takeWord(remaining);
    if (firstArg.length() == 0 || secondArg.length() == 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: " + name + " <source> <destination>");
    }
    String source = harixos::resolvePath(cwd, firstArg);
    String destination = harixos::resolvePath(cwd, secondArg);

    if (name == "cp") {
      if (!harixos::copyFile(source, destination)) {
        return ApiResult(API_ERROR, "cp: failed");
      }
      output.printf("Copied %s -> %s\r\n", source.c_str(), destination.c_str());
      return ApiResult(API_OK, "");
    }
    if (!harixos::movePath(source, destination)) {
      return ApiResult(API_ERROR, "mv: failed");
    }
    output.printf("Moved %s -> %s\r\n", source.c_str(), destination.c_str());
    return ApiResult(API_OK, "");
  }

  if (name == "write" || name == "append") {
    String remaining = args;
    String pathArg = takeWord(remaining);
    if (pathArg.length() == 0) {
      return ApiResult(API_INVALID_ARGUMENT, "Usage: " + name + " <path> <content>");
    }
    String path = harixos::resolvePath(cwd, pathArg);
    if (!harixos::writeText(path, remaining, name == "append")) {
      return ApiResult(API_ERROR, name + ": failed");
    }
    output.printf("%s %u bytes to %s\r\n",
                  name == "append" ? "Appended" : "Wrote",
                  remaining.length(), path.c_str());
    return ApiResult(API_OK, "");
  }

  return ApiResult(API_INVALID_ARGUMENT, "Unknown filesystem command: " + name);
}

ApiResult ScriptEngine::handleSettingsTimeCommand(const String &name,
                                                  const String &args,
                                                  Stream &output) {
  String action = args;
  action.trim();
  action.toLowerCase();

  if (name == "settings") {
    if (action.length() == 0 || action == "show") {
      harixos::printSettings(harixos::shellSettings, output);
      return ApiResult(API_OK, "");
    }
    if (action == "save") {
      if (harixos::saveSettings(harixos::shellSettings)) {
        output.println(F("Settings saved."));
        return ApiResult(API_OK, "");
      }
      return ApiResult(API_ERROR, "Failed to save settings.");
    }
    // banner/timezone/update/reload stay shell-only: their subcommand parsing
    // lives in main.cpp's handleSettings and reproducing it here would drift.
    return ApiResult(API_INVALID_ARGUMENT, "Usage: settings [show|save]");
  }

  if (name == "time") {
    if (action.length() == 0) {
      time_t now = time(nullptr);
      if (now < 1000000000) {
        return ApiResult(API_INVALID_ARGUMENT, "Time is not set. Use 'time sync'.");
      }
      struct tm *timeinfo = localtime(&now);
      char buf[64];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);
      output.printf("Current Time: %s\r\n", buf);
      return ApiResult(API_OK, "");
    }
    if (action == "sync") {
      if (WiFi.status() != WL_CONNECTED) {
        return ApiResult(API_ERROR, "Cannot sync time. WiFi is not connected.");
      }
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      output.println(F("NTP sync requested."));
      return ApiResult(API_OK, "");
    }
    return ApiResult(API_INVALID_ARGUMENT, "Usage: time [sync]");
  }

  if (name == "reboot") {
    return handleSystemCommand("reboot", output);
  }

  return ApiResult(API_INVALID_ARGUMENT, "Unknown command: " + name);
}

ApiResult ScriptEngine::executeCommand(const String &command, Stream &output) {
  Command cmd = parseCommand(command);
  
  if (cmd.name.length() == 0) {
    return ApiResult(API_OK, "");  // Empty line
  }
  
  cmd.name.toLowerCase();
  
  if (cmd.name == "print") {
    return handlePrintCommand(cmd.args, output);
  } else if (cmd.name == "set") {
    return handleSetCommand(cmd.args, output);
  } else if (cmd.name == "gpio") {
    return handleGpioCommand(cmd.args, output);
  } else if (cmd.name == "wifi") {
    return handleWifiCommand(cmd.args, output);
  } else if (cmd.name == "delay") {
    return handleDelayCommand(cmd.args, output);
  } else if (cmd.name == "system") {
    return handleSystemCommand(cmd.args, output);
  } else if (cmd.name == "run") {
    return handleRunCommand(cmd.args, output);
  } else if (cmd.name == "heap" || cmd.name == "uptime" || cmd.name == "chip" ||
             cmd.name == "info" || cmd.name == "adc" || cmd.name == "calc") {
    return handleSystemValueCommand(cmd.name, cmd.args, output);
  } else if (cmd.name == "pwd" || cmd.name == "cd" || cmd.name == "ls" ||
             cmd.name == "mkdir" || cmd.name == "touch" || cmd.name == "rm" ||
             cmd.name == "cp" || cmd.name == "mv" || cmd.name == "cat" ||
             cmd.name == "write" || cmd.name == "append") {
    return handleFilesystemCommand(cmd.name, cmd.args, output);
  } else if (cmd.name == "settings" || cmd.name == "time" ||
             cmd.name == "reboot") {
    return handleSettingsTimeCommand(cmd.name, cmd.args, output);
  } else if (cmd.name.startsWith("/") || cmd.name.endsWith(".hx")) {
    // Treat as script path if it looks like one
    return handleRunCommand(command, output);
  } else if (cmd.name == "help") {
    printHelp(output);
    return ApiResult(API_OK, "Help displayed");
  } else if (cmd.name == "#") {
    return ApiResult(API_OK, "");  // Comment line
  } else {
    return ApiResult(API_INVALID_ARGUMENT, "Unknown command: " + cmd.name);
  }
}

ApiResult ScriptEngine::executeScript(const String &script, Stream &output) {
  ScriptDepthGuard guard;
  if (!guard.acquired()) {
    String message = "Script nesting limit exceeded (max ";
    message += kMaxScriptDepth;
    message += ")";
    return ApiResult(API_ERROR, message);
  }

  // Split script into lines and execute each
  int startIdx = 0;
  int lineCount = 0;
  int errorCount = 0;
  int scriptLen = script.length();

  // if/else/end state lives for the whole script, not per command. Kept out
  // of executeCommand so a scheduled `if` still reports Unknown command.
  harixos::api::BlockStack blocks;

  output.println(F("--- Script Execution Start ---"));
  
  while (startIdx < scriptLen) {
    int endIdx = script.indexOf('\n', startIdx);
    if (endIdx == -1) {
      endIdx = scriptLen;
    }
    
    String line = script.substring(startIdx, endIdx);
    line.trim();
    
    if (line.length() > 0 && !line.startsWith("#")) {
      Command cmd = parseCommand(line);
      cmd.name.toLowerCase();

      if (cmd.name == "if") {
        bool cond = false;
        if (!blocks.skipping()) {
          double v = 0;
          if (!harixos::api::expr::evaluate(cmd.args.c_str(), v)) {
            output.printf("[ERROR] if %s: Invalid expression.\r\n", cmd.args.c_str());
            ++errorCount;
          } else {
            cond = !isnan(v) && v != 0;
          }
        }
        const char *err = blocks.onEvent(harixos::api::BlockEvent::If, cond);
        if (err != nullptr) {
          output.printf("[ERROR] %s: %s\r\n", line.c_str(), err);
          ++errorCount;
        }
      } else if (cmd.name == "else" || cmd.name == "end") {
        const char *err = blocks.onEvent(
            cmd.name == "else" ? harixos::api::BlockEvent::Else
                               : harixos::api::BlockEvent::End,
            false);
        if (err != nullptr) {
          output.printf("[ERROR] %s: %s\r\n", line.c_str(), err);
          ++errorCount;
        }
      } else if (!blocks.skipping()) {
        ApiResult result = executeCommand(line, output);
        if (result.isError()) {
          output.printf("[ERROR] %s: %s\r\n", line.c_str(), result.message.c_str());
          ++errorCount;
        } else if (result.message.length() > 0) {
          output.printf("[OK] %s\r\n", result.message.c_str());
        }
      }
      ++lineCount;
    }
    
    startIdx = endIdx + 1;
  }

  if (blocks.unclosedCount() > 0) {
    output.printf("[ERROR] %d unclosed if block(s)\r\n", blocks.unclosedCount());
    ++errorCount;
  }
  
  output.printf("--- Script Complete: %d lines, %d errors ---\r\n", lineCount, errorCount);
  
  return errorCount == 0 ? ApiResult(API_OK, "Script executed") 
                         : ApiResult(API_ERROR, String(errorCount) + " errors");
}

void ScriptEngine::printHelp(Stream &output) {
  output.println(F(".hx Script Commands:"));
  output.println();
  output.println(F("Output:"));
  output.println(F("  print <text>            Print text to console"));
  output.println();
  output.println(F("GPIO:"));
  output.println(F("  gpio <pin> on           Set GPIO HIGH"));
  output.println(F("  gpio <pin> off          Set GPIO LOW"));
  output.println(F("  gpio <pin> read         Read GPIO value"));
  output.println(F("  gpio <pin> toggle       Toggle GPIO"));
  output.println(F("  gpio <pin> mode <in|out|pullup>  Set pin mode"));
  output.println(F("  gpio <pin> pulse <count>  Pulse GPIO"));
  output.println(F("  gpio list               Show available pins"));
  output.println();
  output.println(F("WiFi:"));
  output.println(F("  wifi scan               Scan networks"));
  output.println(F("  wifi connect <ssid> <pass>  Connect"));
  output.println(F("  wifi disconnect         Disconnect"));
  output.println(F("  wifi status             Show status"));
  output.println(F("  wifi ip                 Show IP info"));
  output.println();
  output.println(F("System:"));
  output.println(F("  delay <ms>              Sleep (ms)"));
  output.println(F("  system info             Show system info"));
  output.println(F("  system heap             Show heap free"));
  output.println(F("  system reboot           Reboot device"));
  output.println();
  output.println(F("Values:"));
  output.println(F("  heap                    Show heap free"));
  output.println(F("  uptime                  Show uptime in ms"));
  output.println(F("  chip                    Show chip ID, CPU, flash"));
  output.println(F("  info                    Show system info"));
  output.println(F("  adc                     Read A0"));
  output.println(F("  calc <expression>       Evaluate an expression"));
  output.println();
  output.println(F("Files:"));
  output.println(F("  pwd                     Print working directory"));
  output.println(F("  cd <path>               Change directory"));
  output.println(F("  ls [-R] [path]          List files and folders"));
  output.println(F("  mkdir <path>            Create folders"));
  output.println(F("  touch <path>            Create empty files"));
  output.println(F("  rm [-r] <path>          Remove files or folders"));
  output.println(F("  cp <src> <dst>          Copy files"));
  output.println(F("  mv <src> <dst>          Move files"));
  output.println(F("  cat <path>              Print file content"));
  output.println(F("  write <path> <content>  Write a file"));
  output.println(F("  append <path> <content> Append to a file"));
  output.println();
  output.println(F("Settings:"));
  output.println(F("  settings [show|save]    Show or save settings"));
  output.println(F("  time [sync]             Show time or request NTP"));
  output.println(F("  reboot                  Reboot device"));
  output.println();
  output.println(F("Other:"));
  output.println(F("  set <name> = <expr>     Assign a variable"));
  output.println(F("  if/else/end             Conditional blocks"));
  output.println(F("  # comment               Script comment"));
  output.println(F("  help                    Show this help"));
}

}  // namespace api
}  // namespace harixos
