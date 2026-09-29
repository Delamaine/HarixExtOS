#include "scheduler.h"
#include <time.h>
#include "../../api/script_engine.h"
#include "../filesystem/filesystem.h"

namespace harixos {
namespace kernel {
namespace {

const char kSchedulePath[] = "/harixos/schedule.cfg";

} // namespace

Scheduler systemScheduler;

Scheduler::Scheduler() : taskCount(0), nextId(1), lastExecutedSecond(-1), lastExecutedMinute(-1), lastExecutedHour(-1), timeWarningPrinted(false) {}

int Scheduler::add(const char *expression, const String &command) {
  if (expression == nullptr || taskCount >= MAX_TASKS) {
    return -1;
  }

  api::cron::Spec spec;
  if (api::cron::parse(expression, spec, nullptr) != nullptr) {
    return -1;
  }

  tasks[taskCount].id = nextId++;
  tasks[taskCount].spec = spec;
  tasks[taskCount].expression = expression;
  tasks[taskCount].command = command;
  taskCount++;
  return tasks[taskCount - 1].id;
}

bool Scheduler::removeTask(int id) {
  for (int i = 0; i < taskCount; ++i) {
    if (tasks[i].id == id) {
      for (int j = i; j < taskCount - 1; ++j) {
        tasks[j] = tasks[j + 1];
      }
      taskCount--;
      return true;
    }
  }
  return false;
}

void Scheduler::listTasks(Print &out) {
  if (taskCount == 0) {
    out.println(F("No scheduled tasks."));
    return;
  }
  out.println(F("Scheduled Tasks:"));
  out.println(F("ID | Cron              | Command"));
  out.println(F("-----------------------------------"));
  for (int i = 0; i < taskCount; ++i) {
    out.printf("%2d | %-17s | %s\r\n", tasks[i].id,
               tasks[i].expression.c_str(), tasks[i].command.c_str());
  }
}

void Scheduler::update() {
  time_t now = time(nullptr);
  struct tm *timeinfo = (now >= 1000000000) ? localtime(&now) : nullptr;

  if (timeinfo == nullptr) {
    // Until NTP has synced there is nothing to match against, and silently
    // doing nothing reads as a broken scheduler. Warn once per boot, and only
    // when a task exists that would otherwise have fired.
    if (!timeWarningPrinted && taskCount > 0) {
      timeWarningPrinted = true;
      Serial.println(F("\r\n[Scheduler] no valid system time, cron not running"));
    }
    return;
  }

  // Already checked for this exact second. Cron resolution is one second, so
  // tracking hour/min/sec is enough to keep each match to a single fire.
  if (timeinfo->tm_hour == lastExecutedHour &&
      timeinfo->tm_min == lastExecutedMinute &&
      timeinfo->tm_sec == lastExecutedSecond) {
    return;
  }
  lastExecutedHour = timeinfo->tm_hour;
  lastExecutedMinute = timeinfo->tm_min;
  lastExecutedSecond = timeinfo->tm_sec;

  for (int i = 0; i < taskCount; ++i) {
    if (api::cron::matches(tasks[i].spec, *timeinfo)) {
      Serial.printf("\r\n[Scheduler] Executing task #%d: %s\r\n", tasks[i].id,
                    tasks[i].command.c_str());
      harixos::api::ScriptEngine::executeCommand(tasks[i].command, Serial);
      Serial.print(F("\r\nHarixOS> "));
    }
  }
}

bool Scheduler::save() {
  char scratch[api::cron::kMaxSaveLine];
  String content;

  for (int i = 0; i < taskCount; ++i) {
    if (api::cron::makeLine(tasks[i].expression.c_str(),
                            tasks[i].command.c_str(), scratch,
                            sizeof(scratch)) != nullptr) {
      return false;
    }
    content += scratch;
  }

  return harixos::writeText(kSchedulePath, content, false);
}

bool Scheduler::load() {
  if (!harixos::exists(kSchedulePath)) {
    return true;
  }

  String content = harixos::readText(kSchedulePath);
  const int len = content.length();
  int start = 0;

  while (start <= len) {
    int end = start;
    while (end < len && content[end] != '\n') {
      ++end;
    }

    String raw = content.substring(start, end);
    start = end + 1;
    raw.trim();
    if (raw.length() == 0) {
      continue;
    }

    char lineBuf[api::cron::kMaxSaveLine];
    raw.toCharArray(lineBuf, sizeof(lineBuf));

    char cronBuf[64];
    const char *command = nullptr;
    const char *err =
        api::cron::splitLine(lineBuf, cronBuf, sizeof(cronBuf), &command);

    api::cron::Spec spec;
    if (err == nullptr) {
      err = api::cron::parse(cronBuf, spec, nullptr);
    }
    if (err != nullptr) {
      // A bad line costs one task, never the whole file.
      Serial.printf("[Scheduler] skipping bad schedule.cfg line: %s\r\n",
                    lineBuf);
      continue;
    }

    if (taskCount >= MAX_TASKS) {
      Serial.printf("[Scheduler] schedule.cfg holds more than %d tasks; "
                    "ignoring the rest.\r\n",
                    MAX_TASKS);
      break;
    }

    tasks[taskCount].id = nextId++;
    tasks[taskCount].spec = spec;
    tasks[taskCount].expression = cronBuf;
    tasks[taskCount].command = command;
    taskCount++;
  }

  // Ids are assigned 1..N in file order, so a reboot with an unchanged file
  // reproduces exactly the ids spec 7.5 requires to survive a power cycle.
  nextId = taskCount + 1;
  return true;
}

} // namespace kernel
} // namespace harixos
