#ifndef HARIXOS_INIT_GATE_H
#define HARIXOS_INIT_GATE_H

#include <stddef.h>

namespace harixos {
namespace api {

// Once-per-boot table for script `init ... end` blocks. RAM-only by design:
// a reboot is the reset, so init runs again after power loss and restores the
// pin modes and pwm setup the board forgets.
// ponytail: 8 paths x 39 chars; bump the table if a device hosts more scripts.
bool initAlreadyDone(const char *path);
void markInitDone(const char *path);
void clearInitDone();

}  // namespace api
}  // namespace harixos

#endif
