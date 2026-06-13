// Moisture — soil-moisture sensor configuration + ADC sampling.
//
// Reads up to three analog moisture sensors on ADC1 and exposes their raw values
// for the MQTT publisher (see MqttPublisher.h). Channels map positionally:
//
//     channel 0 -> GPIO4 (ADC1_CH3)
//     channel 1 -> GPIO5 (ADC1_CH4)
//     channel 2 -> GPIO6 (ADC1_CH5)
//
// ADC1 is deliberate: ADC2 is unusable while WiFi is active, and this node is
// always on WiFi. These pins sit on the board's left-hand (camera/ADC) header —
// free to use because this project does not wire the camera (see ESP32S3.md §2).
//
// Config string (V11, app->device):
//     <pin1>[,<pin2>[,<pin3>]] ; <device_name> ; <mqtt_host>[:<port>] ; <interval_s>
//   - pin names    : 1..3 comma-separated labels. The COUNT selects how many
//                    channels are sampled (names map positionally to GPIO4/5/6).
//                    The names themselves are NOT published — friendly names are a
//                    Grafana-side concern; the wire format is s0/s1/s2 by channel.
//                    They are kept only for logging/diagnostics.
//   - device_name  : becomes the MQTT topic segment + Prometheus `sensor` label.
//                    charset [A-Za-z0-9_-], so it is always topic-safe.
//   - mqtt_host    : broker hostname/IP, optional ":port" (default 1883).
//   - interval_s   : publish period in seconds (optional, default 60, clamped
//                    to [10, 3600]).
//   examples:
//     tomato,basil,mint;garden-node1;192.168.1.50:1883;60
//     tomato;balcony;mqtt.local            (1 sensor, default port + interval)
//
// The parser (parse/apply) is free of any Arduino/Blynk dependency so it can be
// host-compiled and unit-tested, exactly like Schedule.h. Only readChannel() and
// beginAdc() touch the Arduino ADC.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define MOIST_MAX_PINS          3
#define MOIST_NAME_LEN          16          // per pin label, incl. NUL
#define MOIST_DEV_LEN           32          // device name, incl. NUL
#define MOIST_HOST_LEN          64          // broker host, incl. NUL

#define MOIST_DEFAULT_PORT      1883
#define MOIST_DEFAULT_INTERVAL  60
#define MOIST_MIN_INTERVAL      10
#define MOIST_MAX_INTERVAL      3600

#define MOIST_RAW_MAX           4095        // 12-bit ADC
#define MOIST_SAMPLES           16          // reads averaged per channel (noise smoothing)

// channel -> GPIO. Index 0..2 == s0..s2.
static const uint8_t MOIST_ADC_GPIO[MOIST_MAX_PINS] = { 4, 5, 6 };

class MoistureConfig {
public:
  MoistureConfig() { _clear(); }

  bool     valid()      const { return _valid; }
  uint8_t  count()      const { return _count; }
  const char *name(uint8_t i) const { return (i < _count) ? _names[i] : ""; }
  const char *device()  const { return _device; }
  const char *host()    const { return _host; }
  uint16_t port()       const { return _port; }
  uint16_t intervalS()  const { return _interval_s; }
  uint32_t intervalMs() const { return (uint32_t)_interval_s * 1000UL; }

  // Apply a V11 config string. On any malformed input, clears the config (sampling
  // stops), writes "INVALID FORMAT" into echoOut, and returns true so the caller
  // rewrites V11 to show the rejection. On a valid string, stores it, leaves echoOut
  // untouched, and returns false (V11 stays as the user typed it). Mirrors
  // ScheduleManager::apply().
  bool apply(const char *raw, char *echoOut, size_t cap) {
    if (echoOut && cap) echoOut[0] = '\0';
    if (parse(raw)) return false;                 // valid -> nothing to rewrite

    _clear();                                     // invalid -> disable + echo
    if (echoOut && cap) snprintf(echoOut, cap, "INVALID FORMAT");
    return true;
  }

  // Pure parse: returns true and populates state on success, false on any error.
  // Leaves state cleared on failure. No Arduino/Blynk calls — safe to unit-test.
  bool parse(const char *raw) {
    _clear();
    if (!raw) return false;

    // Bounded working copy (sized for the longest plausible V11 string).
    char buf[MOIST_NAME_LEN * MOIST_MAX_PINS + MOIST_DEV_LEN + MOIST_HOST_LEN + 16];
    size_t len = strlen(raw);
    if (len == 0 || len >= sizeof(buf)) return false;
    memcpy(buf, raw, len + 1);

    // Split into sections on ';' — 3 or 4 (interval optional). More is malformed.
    if (_countChar(buf, ';') > 3) return false;
    char *sec[4];
    uint8_t nsec = _split(buf, ';', sec, 4);
    if (nsec < 3) return false;

    // --- section 0: pin names (1..3, comma-separated) ---
    if (_countChar(sec[0], ',') > MOIST_MAX_PINS - 1) return false;
    char *pin[MOIST_MAX_PINS];
    uint8_t np = _split(sec[0], ',', pin, MOIST_MAX_PINS);
    for (uint8_t i = 0; i < np; i++) {
      _trim(pin[i]);
      size_t pl = strlen(pin[i]);
      if (pl == 0 || pl >= MOIST_NAME_LEN) return false;
      memcpy(_names[i], pin[i], pl + 1);
    }
    _count = np;

    // --- section 1: device name (topic-safe charset) ---
    _trim(sec[1]);
    if (!_isToken(sec[1]) || strlen(sec[1]) >= MOIST_DEV_LEN) return false;
    strcpy(_device, sec[1]);

    // --- section 2: host[:port] ---
    _trim(sec[2]);
    char *colon = strchr(sec[2], ':');
    if (colon) {
      *colon = '\0';
      if (!_parsePort(colon + 1, _port)) return false;
    }
    if (!_isHost(sec[2]) || strlen(sec[2]) >= MOIST_HOST_LEN) return false;
    strcpy(_host, sec[2]);

    // --- section 3: interval (optional) ---
    if (nsec == 4) {
      _trim(sec[3]);
      uint32_t iv;
      if (!_parseUInt(sec[3], iv)) return false;
      if (iv < MOIST_MIN_INTERVAL) iv = MOIST_MIN_INTERVAL;
      if (iv > MOIST_MAX_INTERVAL) iv = MOIST_MAX_INTERVAL;
      _interval_s = (uint16_t)iv;
    }

    _valid = true;
    return true;
  }

  // ---- Arduino-only helpers (no-ops if you host-compile without Arduino.h) ----
#ifdef ARDUINO
  // Configure ADC1 once at startup: 12-bit, full ~0-3.3 V range.
  static void beginAdc() {
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);   // ~0-3.3 V; alias of ADC_ATTEN_DB_12 on core 3.x
  }

  // Averaged raw reading for channel i (0..count-1), or -1 if out of range.
  int readChannel(uint8_t i) const {
    if (i >= _count) return -1;
    uint32_t acc = 0;
    for (uint8_t n = 0; n < MOIST_SAMPLES; n++) acc += analogRead(MOIST_ADC_GPIO[i]);
    return (int)(acc / MOIST_SAMPLES);
  }
#endif

private:
  char     _names[MOIST_MAX_PINS][MOIST_NAME_LEN];
  uint8_t  _count;
  char     _device[MOIST_DEV_LEN];
  char     _host[MOIST_HOST_LEN];
  uint16_t _port;
  uint16_t _interval_s;
  bool     _valid;

  void _clear() {
    for (uint8_t i = 0; i < MOIST_MAX_PINS; i++) _names[i][0] = '\0';
    _count      = 0;
    _device[0]  = '\0';
    _host[0]    = '\0';
    _port       = MOIST_DEFAULT_PORT;
    _interval_s = MOIST_DEFAULT_INTERVAL;
    _valid      = false;
  }

  static uint8_t _countChar(const char *s, char c) {
    uint8_t n = 0;
    for (; *s; ++s) if (*s == c) n++;
    return n;
  }

  // Split `buf` in place on `sep` into up to maxParts pointers, preserving empty
  // fields. Stops splitting at maxParts (any extra separators stay in the last part).
  static uint8_t _split(char *buf, char sep, char **parts, uint8_t maxParts) {
    uint8_t n = 0;
    parts[n++] = buf;
    for (char *p = buf; *p && n < maxParts; ++p) {
      if (*p == sep) { *p = '\0'; parts[n++] = p + 1; }
    }
    return n;
  }

  static void _trim(char *s) {
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
  }

  // Non-empty, charset [A-Za-z0-9_-] — safe as an MQTT topic segment / Prom label.
  static bool _isToken(const char *s) {
    if (!*s) return false;
    for (; *s; ++s) {
      char c = *s;
      if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
    }
    return true;
  }

  // Non-empty hostname/IP charset: token chars plus '.' (no spaces, no '/').
  static bool _isHost(const char *s) {
    if (!*s) return false;
    for (; *s; ++s) {
      char c = *s;
      if (!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) return false;
    }
    return true;
  }

  // Strictly all-digits unsigned, no overflow past uint32.
  static bool _parseUInt(const char *s, uint32_t &out) {
    if (!*s) return false;
    uint32_t v = 0;
    for (; *s; ++s) {
      if (!isdigit((unsigned char)*s)) return false;
      v = v * 10 + (uint32_t)(*s - '0');
      if (v > 4000000000UL) return false;
    }
    out = v;
    return true;
  }

  static bool _parsePort(const char *s, uint16_t &out) {
    uint32_t v;
    if (!_parseUInt(s, v)) return false;
    if (v < 1 || v > 65535) return false;
    out = (uint16_t)v;
    return true;
  }
};
