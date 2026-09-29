#pragma once

#include <stddef.h>

namespace harixos {
namespace api {

// Device-only value tokens: heap, adc, readpin <n>, uptime, millis, time.
// Deliberately excluded from the native build_src_filter — it needs
// ESP/Arduino and cannot compile on the host.
bool resolveDeviceValueToken(const char *token, size_t tokenLen, double &out);

}  // namespace api
}  // namespace harixos
