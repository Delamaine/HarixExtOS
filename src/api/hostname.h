#pragma once

namespace harixos {
namespace api {
namespace hostname {

// Validates a mDNS hostname label set: lowercase [a-z0-9-], no empty
// labels, total length (dots included) <= 63. Callers lowercase first.
bool valid(const char *name);

}  // namespace hostname
}  // namespace api
}  // namespace harixos
