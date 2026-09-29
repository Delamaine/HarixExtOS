#include "scheduler.h"
#include <time.h>
#include "../../api/script_engine.h"
#include "../filesystem/filesystem.h"

namespace harixos {
namespace kernel {

Scheduler systemScheduler;

Scheduler::Scheduler() : taskCount(0), nextId(1), lastExecutedSecond(-1), lastExecutedMinute(-1), lastExecutedHour(-1) {}

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

} // namespace kernel
} // namespace harixos
