#ifndef HARIXOS_MQTT_SERVICE_H
#define HARIXOS_MQTT_SERVICE_H

#include <Arduino.h>

#include "../../api/api_types.h"

namespace harixos { namespace iot {
// Plain-TCP MQTT service: backoff connect, LWT availability, status and raw
// publish. Telemetry/discovery (Task 6) and the inbound queue drain
// (Task 7) extend update() later.
void begin();    // configure client from settings; no connect
void update();   // if enabled: WiFi check -> backoff connect -> availability
                  // -> client.loop() -> telemetry tick -> drain loop
bool isConnected();
harixos::api::ApiResult publishRaw(const String &topic, const String &payload,
                                   bool retained = false);  // topic verbatim
String statusText();
// Reply contract: chunk size and cap for shell/out replies (Task 7).
constexpr size_t kReplyChunk = 900;
constexpr size_t kReplyCap = 4000;
// Script-level pause: set to true during blocking scripts so update()
// skips telemetry ticks (exactly one fires after script completes).
void setScriptActive(bool active);
bool isScriptActive();
// Shared body behind both dispatchers' `mqtt status|start|stop|pub ...`.
harixos::api::ApiResult handleCommand(const String &args, Stream &output);
}}

#endif
