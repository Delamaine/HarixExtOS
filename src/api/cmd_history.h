#ifndef HARIXOS_CMD_HISTORY_H
#define HARIXOS_CMD_HISTORY_H

#include <stddef.h>

namespace harixos {
namespace api {
namespace history {

// RAM-only ring of recent command lines for recall. Populated by the
// serial console (up/down arrows) and MQTT shell/in lines; both entry
// points also expand whole-line "!n" references before pushing, and the
// `history`/`exec <n>` commands list and re-run entries. Cleared by
// reboot, same philosophy as the init gate (no flash wear).
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

// Absolute lookup: n = 1 is the oldest entry, n = count() the newest;
// nullptr when out of range.
const char *at(size_t n);

size_t count();

// Expands a whole-line "!n" reference (n = absolute number, 1 = oldest).
// Lines that are not "!<digits>" copy through unchanged and return true.
// An out-of-range "!n" copies an error message (no "ERROR: " prefix)
// into out and returns false. out must hold kMaxLine + 1 bytes.
bool expand(const char *line, char *out, size_t outSize);

// False for lines exec must not re-run: nested references ("exec ..."
// or "!...") would recurse.
bool canExec(const char *line);

}  // namespace history
}  // namespace api
}  // namespace harixos

#endif
