// TimeManager — wall-clock time for the watering scheduler.
//
// Wraps the ESP-IDF SNTP client (via the Arduino configTzTime helper). The device
// has no battery-backed RTC, so the clock is meaningless until the first NTP sync
// completes; valid() reports whether that has happened. All other code MUST gate on
// valid() before trusting now() / formatLocal().
//
// Timezone is compiled in (Vietnam, UTC+7, no DST). POSIX TZ strings invert the
// sign, so UTC+7 is written "<+07>-7" — this is correct, not a typo.
//
// No Blynk/Arduino-runtime dependencies beyond <time.h> + the SDK SNTP API, so this
// class can be compiled and unit-tested on a host with plain g++ later.

#pragma once

#include <time.h>

#ifndef SCHED_TZ
#define SCHED_TZ   "<+07>-7"      // Vietnam, UTC+7, no DST
#endif

#ifndef SCHED_TZ_SUFFIX
#define SCHED_TZ_SUFFIX  "+07"    // human-readable suffix appended to V9
#endif

class TimeManager {
public:
  // Configure TZ + start SNTP. Idempotent: safe to call on every Blynk reconnect.
  // The SDK resyncs roughly hourly by default — plenty, since an always-awake S3
  // drifts only ~1 s/day off its main crystal.
  void begin() {
    // pool.ntp.org first (geo-balanced), with public fallbacks.
    configTzTime(SCHED_TZ, "pool.ntp.org", "time.nist.gov", "time.google.com");
  }

  // True once the clock has been set by NTP at least once this power cycle.
  // Before the first sync time() sits near the 1970 epoch; we treat anything
  // before 2024 as "not yet valid".
  bool valid() const {
    return time(nullptr) >= VALID_THRESHOLD;
  }

  time_t now() const {
    return time(nullptr);
  }

  // Writes "YYYY-MM-DD HH:MM:SS +07" (local) into buf. Returns false (and leaves a
  // placeholder) if the clock is not yet valid.
  bool formatLocal(char *buf, size_t n) const {
    if (n == 0) return false;
    time_t t = time(nullptr);
    if (t < VALID_THRESHOLD) {
      snprintf(buf, n, "SYNCING...");
      return false;
    }
    struct tm lt;
    localtime_r(&t, &lt);
    snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d %s",
             lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
             lt.tm_hour, lt.tm_min, lt.tm_sec, SCHED_TZ_SUFFIX);
    return true;
  }

private:
  // 2024-01-01 00:00:00 UTC. Any real NTP time is well past this; 1970-ish is not.
  static const time_t VALID_THRESHOLD = 1704067200;
};
