// MqttPublisher — pushes moisture readings to the local MQTT broker.
//
// Wraps PubSubClient (knolleary). Library install required:
//     Arduino IDE -> Library Manager -> "PubSubClient" by Nick O'Leary
//
// Wire contract (matches monitoring/mqtt2prometheus/config.yaml):
//     topic:   watering/<device>/moisture
//     payload: {"s0":<raw>,..,"r0":<0|1>,..}   (sN = enabled moisture channels,
//              rN = enabled relay/pump states, 1=on; only the ids enabled in V11
//              appear — a key absent means that metric was not selected)
//     status:  watering/<device>/status  -> "online"/"offline" (retained, via LWT)
//
// The status topic is published for free via the MQTT Last Will & Testament: if this
// node drops off the network ungracefully, the broker publishes "offline" on our
// behalf. The exporter ignores it today (liveness comes from cache.timeout), but it
// is here for the future command/Grafana-annotation path — see the design notes.
//
// Network notes: uses its own WiFiClient (independent of the Blynk cloud socket) to
// reach the LAN broker. WiFi itself is owned by Blynk Edgent. The socket timeout is
// kept short so a missing broker cannot stall loop() for long; the relay hardware
// failsafe (esp_timer) is independent of loop() regardless.

#pragma once

#include <WiFi.h>
#include <PubSubClient.h>
#include "Moisture.h"

class MqttPublisher {
public:
  MqttPublisher() : _mqtt(_net) {}

  void begin() {
    _net.setConnectionTimeout(2000);  // ms — cap the TCP handshake (else core default 3 s)
    _mqtt.setSocketTimeout(2);        // s  — cap the MQTT CONNACK wait after TCP connects
    _mqtt.setKeepAlive(90);
    _configured = false;
  }

  // (Re)point at a broker + device. Cheap; call whenever V11 changes. Disconnects an
  // existing session so the next publish reconnects to the new target with new topics.
  void configure(const char *host, uint16_t port, const char *device) {
    strncpy(_host,   host,   sizeof(_host)   - 1); _host[sizeof(_host) - 1]     = '\0';
    strncpy(_device, device, sizeof(_device) - 1); _device[sizeof(_device) - 1] = '\0';
    _port = port;
    snprintf(_topic,       sizeof(_topic),       "watering/%s/moisture", _device);
    snprintf(_statusTopic, sizeof(_statusTopic), "watering/%s/status",   _device);

    _mqtt.setServer(_host, _port);
    if (_mqtt.connected()) _mqtt.disconnect();
    _configured = true;
  }

  // Service the MQTT keepalive/incoming. Call from loop().
  void loop() { if (_configured) _mqtt.loop(); }

  // Build {"s0":..,"r0":..,..} and publish once. Indices i in [0,count) / [0,relayCount)
  // are emitted only where the matching chEnabled[i] / relayEnabled[i] is true, keyed by
  // the real index (so a lone channel 2 publishes as "s2"). Connects on demand. Returns
  // true only if the broker accepted the publish.
  bool publish(const int *vals, const bool *chEnabled, uint8_t count,
               const bool *relayOn, const bool *relayEnabled, uint8_t relayCount) {
    if (!_configured || WiFi.status() != WL_CONNECTED) return false;
    if (!_ensureConnected()) return false;

    char payload[128];
    int  n = 0;
    bool first = true;
    n += snprintf(payload + n, sizeof(payload) - n, "{");
    for (uint8_t i = 0; i < count; i++) {
      if (!chEnabled[i]) continue;
      n += snprintf(payload + n, sizeof(payload) - n,
                    "%s\"s%u\":%d", (first ? "" : ","), (unsigned)i, vals[i]);
      first = false;
    }
    for (uint8_t i = 0; i < relayCount; i++) {
      if (!relayEnabled[i]) continue;
      n += snprintf(payload + n, sizeof(payload) - n,
                    "%s\"r%u\":%d", (first ? "" : ","), (unsigned)i, relayOn[i] ? 1 : 0);
      first = false;
    }
    snprintf(payload + n, sizeof(payload) - n, "}");

    return _mqtt.publish(_topic, payload);
  }

  bool connected() { return _mqtt.connected(); }

private:
  // One connect attempt. Registers the LWT and, on success, marks us "online".
  bool _ensureConnected() {
    if (_mqtt.connected()) return true;

    // Unique-ish client id: device name + low 32 bits of the eFuse MAC.
    char cid[MOIST_DEV_LEN + 12];
    snprintf(cid, sizeof(cid), "%s-%08x",
             _device, (unsigned)(uint32_t)ESP.getEfuseMac());

    // connect(id, user, pass, willTopic, willQoS, willRetain, willMsg)
    bool ok = _mqtt.connect(cid, NULL, NULL, _statusTopic, 0, true, "offline");
    if (ok) _mqtt.publish(_statusTopic, "online", true);
    return ok;
  }

  WiFiClient   _net;
  PubSubClient _mqtt;

  char     _host[MOIST_HOST_LEN];
  char     _device[MOIST_DEV_LEN];
  char     _topic[MOIST_DEV_LEN + 24];
  char     _statusTopic[MOIST_DEV_LEN + 24];
  uint16_t _port       = MOIST_DEFAULT_PORT;
  bool     _configured = false;
};
