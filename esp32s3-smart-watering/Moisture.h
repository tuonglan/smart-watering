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
//     <id>[,<id>...] ; <device_name> ; <mqtt_host>[:<port>] ; <interval_s>
//   - ids          : explicit list of which metrics to publish, drawn from
//                    {s0,s1,s2,r0,r1}, in any order, no duplicates, at least one.
//                    sN selects a moisture channel (s0->GPIO4, s1->GPIO5, s2->GPIO6);
//                    rN selects a relay/pump state (r0->GPIO38, r1->GPIO39, 1=on).
//                    Anything omitted is neither sampled nor published — the wire
//                    keys are exactly the ids listed here. (The relay states are
//                    added to the message by the .ino; this config only chooses
//                    whether each rN rides along.)
//   - device_name  : becomes the MQTT topic segment + Prometheus `sensor` label.
//                    charset [A-Za-z0-9_-], so it is always topic-safe.
//   - mqtt_host    : broker hostname/IP, optional ":port" (default 1883).
//   - interval_s   : publish period in seconds (optional, default 60, clamped
//                    to [10, 3600]).
//   examples:
//     s0,s1,s2,r0,r1;garden-node1;192.168.1.50:1883;60   (all five)
//     s2;balcony;mqtt.local                              (only channel 2, no relays)
//     s1,s2,r0;node1;192.168.1.50;30                     (channels 1&2 + relay 0)
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
#define MOIST_RELAYS            2           // r0->GPIO38, r1->GPIO39
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
  // Which metrics are enabled. ch in 0..MOIST_MAX_PINS-1, r in 0..MOIST_RELAYS-1.
  bool     channelEnabled(uint8_t ch) const { return ch < MOIST_MAX_PINS && (_chMask    & (1u << ch)); }
  bool     relayEnabled(uint8_t r)    const { return r  < MOIST_RELAYS   && (_relayMask & (1u << r)); }
  uint8_t  channelCount() const { return _popcount(_chMask); }
  uint8_t  relayCount()   const { return _popcount(_relayMask); }
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
    char buf[3 * (MOIST_MAX_PINS + MOIST_RELAYS) + MOIST_DEV_LEN + MOIST_HOST_LEN + 16];
    size_t len = strlen(raw);
    if (len == 0 || len >= sizeof(buf)) return false;
    memcpy(buf, raw, len + 1);

    // Split into sections on ';' — 3 or 4 (interval optional). More is malformed.
    if (_countChar(buf, ';') > 3) return false;
    char *sec[4];
    uint8_t nsec = _split(buf, ';', sec, 4);
    if (nsec < 3) return false;

    // --- section 0: explicit metric ids (s0..s2 / r0..r1, comma-separated) ---
    if (_countChar(sec[0], ',') > MOIST_MAX_PINS + MOIST_RELAYS - 1) return false;
    char *id[MOIST_MAX_PINS + MOIST_RELAYS];
    uint8_t nid = _split(sec[0], ',', id, MOIST_MAX_PINS + MOIST_RELAYS);
    for (uint8_t i = 0; i < nid; i++) {
      _trim(id[i]);
      if (!_addId(id[i])) return false;       // validates s0..s2/r0..r1, rejects dup
    }
    if (_chMask == 0 && _relayMask == 0) return false;   // must publish something

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

  // Averaged raw reading for channel i, or -1 if that channel is not enabled.
  int readChannel(uint8_t i) const {
    if (!channelEnabled(i)) return -1;
    return readRaw(i);
  }

  // Averaged raw reading for channel i, IGNORING the enabled mask (-1 if i is out of
  // range). For debug/diagnostics (e.g. the V41 get_moisture command) that wants to
  // see every sensor regardless of what V11 selected for publishing.
  static int readRaw(uint8_t i) {
    if (i >= MOIST_MAX_PINS) return -1;
    uint32_t acc = 0;
    for (uint8_t n = 0; n < MOIST_SAMPLES; n++) acc += analogRead(MOIST_ADC_GPIO[i]);
    return (int)(acc / MOIST_SAMPLES);
  }
#endif

private:
  uint8_t  _chMask;        // bit i set -> channel i (s<i>) enabled
  uint8_t  _relayMask;     // bit i set -> relay   i (r<i>) enabled
  char     _device[MOIST_DEV_LEN];
  char     _host[MOIST_HOST_LEN];
  uint16_t _port;
  uint16_t _interval_s;
  bool     _valid;

  void _clear() {
    _chMask     = 0;
    _relayMask  = 0;
    _device[0]  = '\0';
    _host[0]    = '\0';
    _port       = MOIST_DEFAULT_PORT;
    _interval_s = MOIST_DEFAULT_INTERVAL;
    _valid      = false;
  }

  // Set the bit for one id token ("s0".."s2" / "r0".."r1"). Returns false on a
  // malformed token, an out-of-range index, or a duplicate.
  bool _addId(const char *s) {
    if (strlen(s) != 2 || !isdigit((unsigned char)s[1])) return false;
    uint8_t idx = (uint8_t)(s[1] - '0');
    if (s[0] == 's') {
      if (idx >= MOIST_MAX_PINS) return false;
      uint8_t bit = (uint8_t)(1u << idx);
      if (_chMask & bit) return false;
      _chMask |= bit;
      return true;
    }
    if (s[0] == 'r') {
      if (idx >= MOIST_RELAYS) return false;
      uint8_t bit = (uint8_t)(1u << idx);
      if (_relayMask & bit) return false;
      _relayMask |= bit;
      return true;
    }
    return false;
  }

  static uint8_t _popcount(uint8_t m) {
    uint8_t n = 0;
    for (; m; m >>= 1) n += (m & 1u);
    return n;
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
