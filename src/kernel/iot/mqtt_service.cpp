#include "mqtt_service.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "../../apps/settings/settings.h"
#include "../../api/script_engine.h"
#include "../../api/system_api.h"
#include "../../utils/string_stream.h"
#include "inbound_queue.h"

namespace harixos {
namespace iot {
namespace {

constexpr uint32_t kMinHeap = 10240;  // refuse to connect below this free heap
constexpr uint32_t kMinHeapForTelemetry = 8192;  // skip telemetry below this
constexpr uint16_t kKeepAliveS = 60;
constexpr uint16_t kSocketTimeoutS = 3;
constexpr uint16_t kBufferSize = 1024;
constexpr uint32_t kBackoffInitialMs = 5000;
constexpr uint32_t kBackoffMaxMs = 60000;

WiFiClient wifiClient;
PubSubClient client(wifiClient);

uint32_t nextAttemptMs = 0;  // millis() deadline; 0 = attempt due now
uint32_t backoffMs = kBackoffInitialMs;
bool availabilityPending = false;  // publish "online" once after each connect

uint32_t connectFailures = 0;
uint32_t publishFailures = 0;
uint32_t droppedTelemetry = 0;
uint32_t callbackCount = 0;
bool subscribed = false;
String lastPublishedTopic;
String lastPublishedPayload;
uint32_t selfPublishStartMs = 0;

// Shell-in topic prefix (resolved at runtime from settings).
String shellInTopic() {
  return shellSettings.mqttPrefix + "/shell/in";
}

// MQTT callback for <prefix>/shell/in.
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  callbackCount++;
  String t(topic);
  String expected = shellInTopic();
  
  if (t == expected) {
    // Echo avoidance: skip messages we just published ourselves (within 200ms)
    String dump;
    for (unsigned int i = 0; i < length && i < 80; i++) {
      dump += (char)payload[i];
    }
    uint32_t now = millis();
    if ((now - selfPublishStartMs) < 200 && dump == lastPublishedPayload && t == lastPublishedTopic) {
      return;
    }
    
    if (length == 0) {
      // Empty payload → publish error immediately.
      publishRaw(shellSettings.mqttPrefix + "/shell/out", "ERROR: empty line");
      return;
    }
    // Extract the line; reject if len >= 80.
    String line;
    for (unsigned int i = 0; i < length; i++) {
      line += (char)payload[i];
    }
    if (line.length() >= 80) {
      publishRaw(shellSettings.mqttPrefix + "/shell/out", "ERROR: line too long");
      return;
    }
    inbound().push((const char *)payload, length);
  }
}

// Telemetry tick state (Task 6)
uint32_t lastTelemetryMs = 0;
bool firstConnect = true;
bool discoveryPublished = false;
bool s_scriptActive = false;

// HA discovery topic prefix mapping: settings prefix "harixos/abc" ->
// "harixos_abc" for HA uniq_id and name fields.
String haPrefix() {
  String p = shellSettings.mqttPrefix;
  p.replace("/", "_");
  return p;
}

String availabilityTopic() {
  return shellSettings.mqttPrefix + "/availability";
}

// Retained HA switch state: <prefix>/mqtt_enabled = "on"|"off".
// Published on connect, and on `mqtt start` / `mqtt stop`.
String enabledStateTopic() {
  return shellSettings.mqttPrefix + "/mqtt_enabled";
}

void publishEnabledState() {
  publishRaw(enabledStateTopic(), shellSettings.mqttEnabled ? "on" : "off", true);
}

void resetBackoff() {
  backoffMs = kBackoffInitialMs;
  nextAttemptMs = 0;
}

// Refusals (no host, heap guard) wait a flat 5 s; real connect failures
// double the delay 5 s -> 60 s cap per the backoff contract.
void scheduleRetry(uint32_t delayMs, bool growBackoff) {
  nextAttemptMs = millis() + delayMs;
  if (growBackoff) {
    uint32_t doubled = delayMs * 2;
    backoffMs = doubled > kBackoffMaxMs ? kBackoffMaxMs : doubled;
  }
}

bool attemptDue() {
  // nextAttemptMs == 0 is the explicit "due now" sentinel; millis() - deadline
  // as a signed diff keeps the comparison correct across the millis() wrap.
  return nextAttemptMs == 0 || (int32_t)(millis() - nextAttemptMs) >= 0;
}

// One connect attempt: refusals print a reason and retry without touching the
// connect counter; a real attempt failure counts. LWT rides on connect()
// because PubSubClient has no setWill: <prefix>/availability = "offline",
// retained, QoS 0, clean session.
void attemptConnect() {
  const AppSettings &settings = shellSettings;
  uint32_t heap = ESP.getFreeHeap();
  if (heap < kMinHeap) {
    Serial.printf("mqtt: heap guard: %u < %u, not connecting\r\n",
                  (unsigned)heap, (unsigned)kMinHeap);
    scheduleRetry(kBackoffInitialMs, false);
    return;
  }
  if (settings.mqttHost.length() == 0) {
    Serial.println(F("mqtt: no host configured"));
    scheduleRetry(kBackoffInitialMs, false);
    return;
  }

  String willTopic = settings.mqttPrefix + "/availability";
  String clientId = "harixos-" + String(ESP.getChipId(), HEX);
  const char *user =
      settings.mqttUser.length() > 0 ? settings.mqttUser.c_str() : NULL;
  const char *pass = (user != NULL && settings.mqttPass.length() > 0)
                         ? settings.mqttPass.c_str()
                         : NULL;

  // setServer stores the raw pointer: refresh it from the live String right
  // before every attempt so a `settings reload` can never leave it dangling.
  client.setServer(settings.mqttHost.c_str(), settings.mqttPort);
  bool ok = client.connect(clientId.c_str(), user, pass, willTopic.c_str(),
                           0, true, "offline", true);
  if (!ok) {
    ++connectFailures;
    Serial.printf("mqtt: connect failed (rc=%d), retry in %lus\r\n",
                  client.state(), (unsigned long)(backoffMs / 1000));
    scheduleRetry(backoffMs, true);
    return;
  }
  resetBackoff();
  availabilityPending = true;
  Serial.println(F("mqtt: connected"));
}

void disconnectGracefully() {
  if (!client.connected()) {
    return;
  }
  publishRaw(availabilityTopic(), "offline", true);
  client.disconnect();
  subscribed = false;
}

}  // namespace

void begin() {
  client.setKeepAlive(kKeepAliveS);
  client.setSocketTimeout(kSocketTimeoutS);
  client.setBufferSize(kBufferSize);
  client.setServer(shellSettings.mqttHost.c_str(), shellSettings.mqttPort);
  client.setCallback(mqttCallback);
  resetBackoff();
  // No connect here; update() owns the connection lifecycle.
}

// Build telemetry JSON payload manually (no JSON library).
// Payload: {"heap":<u>,"uptime":<u>,"rssi":<i>,"adc":<u>}
String buildTelemetryPayload() {
  String payload = String();
  payload += F("{\"heap\":");
  payload += String(ESP.getFreeHeap());
  payload += F(",\"uptime\":");
  payload += String(harixos::api::SystemAPI::getUptime() / 1000);
  payload += F(",\"rssi\":");
  payload += String((int)WiFi.RSSI());
  payload += F(",\"adc\":");
  payload += String(analogRead(A0));
  payload += F("}");
  return payload;
}

// Publish HA discovery messages for telemetry sensor and mqtt_enabled switch.
// Called once after first successful connect when mqttDiscover is enabled.
void publishHADiscovery() {
  if (!shellSettings.mqttDiscover) return;
  if (discoveryPublished) return;
  if (ESP.getFreeHeap() < kMinHeapForTelemetry) {
    ++droppedTelemetry;
    return;
  }

  String hp = haPrefix();
  String prefix = shellSettings.mqttPrefix;

  // Sensor config: homeassistant/sensor/<prefix>/telemetry/config
  String sensorConfigTopic = "homeassistant/sensor/";
  sensorConfigTopic += hp;
  sensorConfigTopic += F("/telemetry/config");

  String sensorPayload = String();
  sensorPayload += F("{\"name\":\"");
  sensorPayload += prefix;
  sensorPayload += F(" Telemetry\",\"state_topic\":\"");
  sensorPayload += prefix;
  sensorPayload += F("/telemetry\",\"value_template\":\"{{ value_json.heap }}\"");
  sensorPayload += F(",\"unit_of_measurement\":\"bytes\"");
  sensorPayload += F(",\"uniq_id\":\"");
  sensorPayload += hp;
  sensorPayload += F("_telemetry\"");
  sensorPayload += F(",\"name\":\"");
  sensorPayload += hp;
  sensorPayload += F("_telemetry\"");
  sensorPayload += F(",\"dev\":{\"ids\":\"");
  sensorPayload += hp;
  sensorPayload += F("\"}}");

  publishRaw(sensorConfigTopic, sensorPayload, true);

  // Switch config: homeassistant/switch/<prefix>/mqtt_enabled/config
  String switchConfigTopic = "homeassistant/switch/";
  switchConfigTopic += hp;
  switchConfigTopic += F("/mqtt_enabled/config");

  String switchPayload = String();
  switchPayload += F("{\"name\":\"");
  switchPayload += prefix;
  switchPayload += F(" MQTT\",\"state_topic\":\"");
  switchPayload += prefix;
  switchPayload += F("/mqtt_enabled\",\"value_template\":\"{{ value }}\"");
  switchPayload += F(",\"command_topic\":\"");
  switchPayload += prefix;
  switchPayload += F("/shell/in\",\"payload_on\":\"mqtt on\"");
  switchPayload += F(",\"payload_off\":\"mqtt off\"");
  switchPayload += F(",\"uniq_id\":\"");
  switchPayload += hp;
  switchPayload += F("_mqtt_enabled\"");
  switchPayload += F(",\"name\":\"");
  switchPayload += hp;
  switchPayload += F("_mqtt_enabled\"");
  switchPayload += F(",\"dev\":{\"ids\":\"");
  switchPayload += hp;
  switchPayload += F("\"}}");

  publishRaw(switchConfigTopic, switchPayload, true);
  discoveryPublished = true;
}

void update() {
  if (!shellSettings.mqttEnabled) {
    // Covers `mqtt stop` as well as enabled=off via `settings reload`.
    disconnectGracefully();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    nextAttemptMs = 0;  // attempt as soon as WiFi comes back
    return;
  }
  if (!client.connected()) {
    if (!attemptDue()) {
      return;  // backoff window still running
    }
    attemptConnect();
    if (!client.connected()) {
      return;
    }
    // First connect: publish HA discovery then telemetry
    if (firstConnect) {
      firstConnect = false;
      publishHADiscovery();
    }
    // Subscribe to shell/in (first connect + reconnection)
    if (!subscribed) {
      String topic = shellInTopic();
      subscribed = client.subscribe(topic.c_str(), 0);
    }
  }
  if (availabilityPending) {
    availabilityPending = false;
    publishRaw(availabilityTopic(), "online", true);
    publishEnabledState();
  }
  client.loop();

  // Drain loop (Task 7): process queued inbound lines through ScriptEngine.
  while (inbound().size() > 0) {
    char line[InboundQueue::kLineMax];
    if (!inbound().pop(line)) break;

    StringStream capture;
    harixos::api::ApiResult r =
        harixos::api::ScriptEngine::executeCommand(String(line), capture);

    String reply;
    if (r.isError()) {
      reply = String("ERROR: ") + r.message;
    } else if (capture.text().isEmpty()) {
      reply = "ok";
    } else {
      reply = capture.text();
    }

    // Truncate at cap and append marker if needed.
    bool truncated = false;
    if (reply.length() > kReplyCap) {
      reply = reply.substring(0, (int)kReplyCap);
      reply += "…[truncated]";
      truncated = true;
    }

    // Publish in chunks of <= kReplyChunk bytes.
    unsigned int pos = 0;
    while (pos < reply.length()) {
      unsigned int chunkLen =
          (pos + kReplyChunk < reply.length()) ? kReplyChunk
                                               : (reply.length() - pos);
      String chunk = reply.substring((int)pos, (int)(pos + chunkLen));
      publishRaw(shellSettings.mqttPrefix + "/shell/out", chunk);
      
      pos += chunkLen;
      if (truncated && pos >= kReplyCap) break;
    }
  }

  // Telemetry tick (Task 6)
  // Skip if script is actively blocking, or if interval not yet reached.
  // Use wrap-safe subtraction: (now - lastTelemetryMs) >= interval
  if (s_scriptActive) {
    return;
  }
  uint32_t now = millis();
  uint32_t intervalMs = (uint32_t)shellSettings.mqttInterval * 1000UL;
  if (intervalMs > 0 && (now - lastTelemetryMs) < intervalMs) {
    return;  // not yet time
  }
  // Check heap before building payload
  if (ESP.getFreeHeap() < kMinHeapForTelemetry) {
    ++droppedTelemetry;
    return;
  }
  String payload = buildTelemetryPayload();
  if (publishRaw(shellSettings.mqttPrefix + "/telemetry", payload, false).isError()) {
    ++droppedTelemetry;
  }
  lastTelemetryMs = millis();
}

bool isConnected() {
  return client.connected();
}

harixos::api::ApiResult publishRaw(const String &topic, const String &payload,
                                    bool retained) {
  if (!client.connected()) {
    return api::ApiResult(api::API_ERROR, "mqtt: not connected");
  }
  bool ok = client.publish(topic.c_str(), payload.c_str(), retained);
  if (!ok) {
    ++publishFailures;
    return api::ApiResult(api::API_ERROR, "mqtt: publish failed");
  }
  // Record publish timestamp for echo avoidance
  lastPublishedTopic = topic;
  lastPublishedPayload = payload;
  selfPublishStartMs = millis();
  return api::ApiResult(api::API_OK, "");
}

String statusText() {
  const AppSettings &settings = shellSettings;
  uint32_t heap = ESP.getFreeHeap();
  bool guardAllow = heap >= kMinHeap;

  String text;
  text += F("connected: ");
  text += isConnected() ? F("yes") : F("no");
  text += F("\r\n");
  text += F("heap: ");
  text += String(heap);
  text += F(" (guard: ");
  text += guardAllow ? F("allow") : F("deny");
  text += F(", min ");
  text += String(kMinHeap);
  text += F(")\r\n");
  text += F("config: ");
  text += settings.mqttHost;
  text += ':';
  text += String(settings.mqttPort);
  text += F(" prefix=");
  text += settings.mqttPrefix;
  text += F(" interval=");
  text += String(settings.mqttInterval);
  text += F("s discover=");
  text += settings.mqttDiscover ? F("on") : F("off");
  text += F(" enabled=");
  text += settings.mqttEnabled ? F("on") : F("off");
  text += F("\r\n");
  if (settings.mqttUser.length() > 0 || settings.mqttPass.length() > 0) {
    text += F("auth: user=");
    text += settings.mqttUser;
    text += F(" pass=***\r\n");  // never echo the real password
  }
  text += F("counters: connect=");
  text += String(connectFailures);
  text += F(" publish=");
  text += String(publishFailures);
  text += F(" telemetry-drop=");
  text += String(droppedTelemetry);
  text += F(" queue-drop=");
  text += String(inbound().dropped());
  return text;
}

void setScriptActive(bool active) {
  s_scriptActive = active;
}

bool isScriptActive() {
  return s_scriptActive;
}

harixos::api::ApiResult handleCommand(const String &argsIn, Stream &output) {
  String rest = argsIn;
  rest.trim();
  int split = rest.indexOf(' ');
  String action = split < 0 ? rest : rest.substring(0, split);
  String tail = split < 0 ? String() : rest.substring(split + 1);
  tail.trim();
  action.toLowerCase();

  if (action == "status") {
    if (shellSettings.mqttEnabled && shellSettings.mqttHost.length() == 0) {
      output.println(F("mqtt: no host configured"));
    }
    output.println(statusText());
    return api::ApiResult(api::API_OK, "");
  }

  if (action == "start" || action == "on") {
    shellSettings.mqttEnabled = true;
    resetBackoff();
    if (!saveSettings(shellSettings)) {
      return api::ApiResult(api::API_ERROR,
                            "mqtt: enabled, but settings save failed");
    }
    publishEnabledState();
    return api::ApiResult(api::API_OK, "mqtt: enabled");
  }

  if (action == "stop" || action == "off") {
    shellSettings.mqttEnabled = false;
    publishEnabledState();
    disconnectGracefully();
    resetBackoff();
    if (!saveSettings(shellSettings)) {
      return api::ApiResult(api::API_ERROR,
                            "mqtt: disabled, but settings save failed");
    }
    return api::ApiResult(api::API_OK, "mqtt: disabled");
  }

  if (action == "pub") {
    int sp = tail.indexOf(' ');
    String topic = sp < 0 ? tail : tail.substring(0, sp);
    String payload = sp < 0 ? String() : tail.substring(sp + 1);
    payload.trim();
    if (topic.length() == 0 || payload.length() == 0) {
      return api::ApiResult(api::API_INVALID_ARGUMENT,
                            "Usage: mqtt pub <topic> <payload>");
    }
    api::ApiResult result = publishRaw(topic, payload);
    if (result.isError()) {
      return result;
    }
    return api::ApiResult(api::API_OK, "mqtt: published");
  }

  return api::ApiResult(api::API_INVALID_ARGUMENT,
                        "Usage: mqtt status|start|stop|on|off|pub <topic> <payload>");
}

}  // namespace iot
}  // namespace harixos
