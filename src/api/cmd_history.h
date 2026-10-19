#ifndef HARIXOS_CMD_HISTORY_H
#define HARIXOS_CMD_HISTORY_H

#include <stddef.h>

namespace harixos {
namespace api {
namespace history {

// RAM-only ring of recent console commands for up/down recall. Serial
// console only — MQTT/HTTP lines are stateless and never enter the ring.
// Cleared by reboot, same philosophy as the init gate (no flash wear).
// ponytail: 16 x 160 static slots (~2.6 KB .bss); bump kCapacity if the
// console outgrows it.
constexpr size_t kCapacity = 16;
constexpr size_t kMaxLine = 160;

void reset();

// Stores a trimmed command line. Skips nullptr, empty lines and an
// immediate repeat of the newest entry. Longer lines are truncated.
void push(const char *line);

// stepsBack 1 = newest entry; nullptr when 0 or past the oldest.
const char *get(size_t stepsBack);

size_t count();

}  // namespace history
}  // namespace api
}  // namespace harixos

#endif
