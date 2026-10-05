#ifndef HARIXOS_SCHEDULER_H
#define HARIXOS_SCHEDULER_H

#include <Arduino.h>

#include "../../api/cron.h"

namespace harixos {
namespace kernel {

struct ScheduledTask {
  int id;
  api::cron::Spec spec;
  String expression;   // original 6-field text, for list and save
  String command;
};

class Scheduler {
public:
  Scheduler();

  // Add a task from a 6-field cron expression. Returns the new id, or -1
  // when the table is full or the expression does not parse.
  int add(const char *expression, const String &command);

  // Remove task by ID
  bool removeTask(int id);

  // Execute a specific task by ID regardless of schedule. Returns 0 on success,
  // -1 if the id is unknown.
  int runTask(int id);

  // List all tasks to output
  void listTasks(Print &out);

  // Check and execute tasks. Should be called in loop()
  void update();

  // Persist / restore the table to /harixos/schedule.cfg.
  bool save();
  bool load();

private:
  static const int MAX_TASKS = 20;
  ScheduledTask tasks[MAX_TASKS];
  int taskCount;
  int nextId;
  int lastExecutedSecond;
  int lastExecutedMinute;
  int lastExecutedHour;
  bool timeWarningPrinted;
};

// Global scheduler instance
extern Scheduler systemScheduler;

} // namespace kernel
} // namespace harixos

#endif // HARIXOS_SCHEDULER_H
