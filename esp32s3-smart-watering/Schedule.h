// Schedule — time-of-day relay start schedules for the watering controller.
//
// Two classes, both free of any Blynk/Arduino-runtime dependency (only the C
// standard library + <time.h>), so they can be compiled and unit-tested on a host
// with plain g++ later.
//
//   RelaySchedule   — one relay's parsed schedule (which weekdays, which times) and
//                     the rule for computing the next firing instant.
//   ScheduleManager — owns both relays' schedules + their precomputed next-run
//                     epochs, parses the combined V10 config string, and drives the
//                     per-second evaluation engine via a trigger callback.
//
// Config string (V10):  <relay0>|<relay1>     (at most one '|')
//   - no '|'      -> relay 0 only
//   - leading '|' -> relay 1 only
//   - empty       -> no schedule
//   per relay:    <day>[,<day>]*;<time>[,<time>]*
//     day  : Mon Tue Wed Thu Fri Sat Sun (case-insensitive) or '*' = every day
//     time : HH:MM:SS or HH:MM (seconds default 0), range-checked
//
// Algorithm: nextRun[r] is a precomputed epoch (0 = disabled). evaluate() fires when
// the clock reaches it (skipping a fire that is >MAX_LATENESS_S late), then always
// advances nextRun[r] to the next occurrence strictly after now. nextRun is never
// persisted — it is recomputed when the clock first becomes valid or the schedule
// changes, so a slot missed during a reboot is simply skipped.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

#ifndef MAX_SCHED_TIMES
#define MAX_SCHED_TIMES   12          // max start times per relay
#endif

#ifndef MAX_LATENESS_S
#define MAX_LATENESS_S    600         // skip a scheduled start more than this late (s)
#endif

#ifndef SCHED_SIDE_LEN
#define SCHED_SIDE_LEN    200         // max length of one relay's config substring
#endif

// ------------------------------------------------------------------ //
//  RelaySchedule                                                       //
// ------------------------------------------------------------------ //
class RelaySchedule {
public:
  RelaySchedule() { clear(); }

  void clear() { _dayMask = 0; _count = 0; }
  bool empty() const { return _count == 0; }

  // Parse one relay's config substring. Returns false on any malformed input.
  // An empty/whitespace-only string is valid and means "disabled". Does not modify
  // its argument (works on an internal copy).
  bool parse(const char *s) {
    clear();
    if (!s) return true;

    while (*s && isspace((unsigned char)*s)) s++;     // ltrim
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;   // rtrim
    if (len == 0) return true;                         // empty -> disabled
    if (len >= SCHED_SIDE_LEN) return false;           // too long

    char buf[SCHED_SIDE_LEN];
    memcpy(buf, s, len);
    buf[len] = '\0';

    char *semi = strchr(buf, ';');
    if (!semi) return false;                           // need <days>;<times>
    if (strchr(semi + 1, ';')) return false;           // only one ';'
    *semi = '\0';

    uint8_t mask = 0;
    if (!_parseDays(buf, mask)) return false;

    uint32_t tmp[MAX_SCHED_TIMES];
    uint8_t cnt = 0;
    if (!_parseTimes(semi + 1, tmp, cnt)) return false;
    if (cnt == 0) return false;

    _sortAsc(tmp, cnt);
    _dayMask = mask;
    _count = cnt;
    for (uint8_t i = 0; i < cnt; i++) _times[i] = tmp[i];
    return true;
  }

  // Smallest scheduled instant strictly greater than `now`, or 0 if disabled.
  // Builds each candidate from tm fields + mktime so day rollover and DST are
  // handled correctly.
  time_t nextAfter(time_t now) const {
    if (empty()) return 0;

    struct tm ref;
    localtime_r(&now, &ref);

    for (int d = 0; d <= 7; d++) {
      struct tm day = ref;
      day.tm_mday += d;
      day.tm_hour = 0;
      day.tm_min = 0;
      day.tm_sec = 0;
      day.tm_isdst = -1;
      time_t midnight = mktime(&day);        // normalizes the rolled-over date

      struct tm norm;
      localtime_r(&midnight, &norm);
      if (!(_dayMask & (1 << norm.tm_wday))) continue;

      for (uint8_t i = 0; i < _count; i++) { // times are sorted ascending
        struct tm cand = norm;
        cand.tm_hour = _times[i] / 3600;
        cand.tm_min  = (_times[i] % 3600) / 60;
        cand.tm_sec  = _times[i] % 60;
        cand.tm_isdst = -1;
        time_t c = mktime(&cand);
        if (c > now) return c;
      }
    }
    return 0;   // unreachable for a non-empty weekly schedule
  }

private:
  uint8_t  _dayMask;                 // bit n set => fires on tm_wday n (Sun=0..Sat=6)
  uint8_t  _count;                   // number of valid times
  uint32_t _times[MAX_SCHED_TIMES];  // seconds since local midnight, ascending

  static void _trim(char *s) {
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
  }

  // "*" or a comma list of 3-char weekday abbreviations (case-insensitive).
  static bool _parseDays(char *part, uint8_t &mask) {
    mask = 0;
    char *save;
    for (char *tok = strtok_r(part, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
      _trim(tok);
      if (*tok == '\0') continue;
      if (strcmp(tok, "*") == 0) {
        mask = 0x7F;                 // every day
        continue;
      }
      int wd = _dayIndex(tok);
      if (wd < 0) return false;
      mask |= (uint8_t)(1 << wd);
    }
    return mask != 0;
  }

  // Returns tm_wday (Sun=0..Sat=6) or -1. Requires exactly 3 chars.
  static int _dayIndex(const char *t) {
    char c[4];
    int i = 0;
    for (; i < 3 && t[i]; i++) c[i] = (char)tolower((unsigned char)t[i]);
    c[i] = '\0';
    if (i != 3 || t[3] != '\0') return -1;
    static const char *kDays[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };
    for (int d = 0; d < 7; d++) if (strcmp(c, kDays[d]) == 0) return d;
    return -1;
  }

  static bool _parseTimes(char *part, uint32_t *out, uint8_t &cnt) {
    cnt = 0;
    char *save;
    for (char *tok = strtok_r(part, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
      _trim(tok);
      if (*tok == '\0') continue;
      if (cnt >= MAX_SCHED_TIMES) return false;
      uint32_t v;
      if (!_parseTime(tok, v)) return false;
      out[cnt++] = v;
    }
    return true;
  }

  // Parses 1-2 digit fields. Advances p past the field. false if no digit / >2 digits.
  static bool _parseField(const char *&p, int &val) {
    if (!isdigit((unsigned char)*p)) return false;
    int v = 0, digits = 0;
    while (isdigit((unsigned char)*p)) {
      v = v * 10 + (*p - '0');
      p++;
      if (++digits > 2) return false;
    }
    val = v;
    return true;
  }

  // "HH:MM" or "HH:MM:SS", strictly. Rejects trailing garbage.
  static bool _parseTime(const char *tok, uint32_t &out) {
    const char *p = tok;
    int h, m, s = 0;
    if (!_parseField(p, h)) return false;
    if (*p != ':') return false;
    p++;
    if (!_parseField(p, m)) return false;
    if (*p == ':') {
      p++;
      if (!_parseField(p, s)) return false;
    }
    if (*p != '\0') return false;
    if (h > 23 || m > 59 || s > 59) return false;
    out = (uint32_t)h * 3600 + (uint32_t)m * 60 + (uint32_t)s;
    return true;
  }

  static void _sortAsc(uint32_t *a, uint8_t n) {
    for (uint8_t i = 1; i < n; i++) {
      uint32_t key = a[i];
      int j = i - 1;
      while (j >= 0 && a[j] > key) { a[j + 1] = a[j]; j--; }
      a[j + 1] = key;
    }
  }
};

// ------------------------------------------------------------------ //
//  ScheduleManager                                                     //
// ------------------------------------------------------------------ //
class ScheduleManager {
public:
  typedef void (*TriggerCb)(uint8_t relay, void *ctx);

  void begin(TriggerCb cb, void *ctx) {
    _cb = cb;
    _ctx = ctx;
    _next[0] = 0;
    _next[1] = 0;
  }

  // Apply a combined V10 config string. Writes a replacement string into echoOut and
  // returns true ONLY when V10 should be rewritten (some side was invalid). On a
  // valid string echoOut is untouched and the function returns false, leaving V10 as
  // the user typed it. timeValid/now are used to (re)arm nextRun immediately.
  bool apply(const char *raw, char *echoOut, size_t cap, time_t now, bool timeValid) {
    if (echoOut && cap) echoOut[0] = '\0';
    if (!raw) raw = "";

    int pipes = 0;
    for (const char *p = raw; *p; ++p) if (*p == '|') pipes++;

    if (pipes > 1) {                              // structural error
      _sched[0].clear();
      _sched[1].clear();
      _next[0] = _next[1] = 0;
      if (echoOut) snprintf(echoOut, cap, "INVALID FORMAT");
      return true;
    }

    char side[2][SCHED_SIDE_LEN];
    const char *bar = strchr(raw, '|');
    if (!bar) {
      _copy(side[0], raw, raw + strlen(raw));
      side[1][0] = '\0';
    } else {
      _copy(side[0], raw, bar);
      _copy(side[1], bar + 1, bar + 1 + strlen(bar + 1));
    }

    bool ok[2];
    for (int r = 0; r < 2; r++) {
      ok[r] = _sched[r].parse(side[r]);
      if (!ok[r]) {
        _sched[r].clear();
        _next[r] = 0;
      } else {
        _next[r] = timeValid ? _sched[r].nextAfter(now) : 0;
      }
    }

    if (ok[0] && ok[1]) return false;             // all valid -> leave V10 as typed

    if (!echoOut) return true;
    const char *e0 = ok[0] ? side[0] : "INVALID FORMAT";
    if (bar) {
      const char *e1 = ok[1] ? side[1] : "INVALID FORMAT";
      snprintf(echoOut, cap, "%s|%s", e0, e1);
    } else {
      snprintf(echoOut, cap, "%s", e0);
    }
    return true;
  }

  // Recompute both next-run epochs (call when the clock first becomes valid).
  void onTimeValid(time_t now) {
    for (int r = 0; r < 2; r++) _next[r] = _sched[r].nextAfter(now);
  }

  // Per-second engine: fire due relays (unless too late), always advance.
  void evaluate(time_t now) {
    for (int r = 0; r < 2; r++) {
      if (_next[r] == 0) continue;                // disabled
      if (now >= _next[r]) {
        time_t late = now - _next[r];
        if (late <= MAX_LATENESS_S && _cb) _cb((uint8_t)r, _ctx);
        _next[r] = _sched[r].nextAfter(now);      // strictly > now (skip-if-on is the cb's job)
      }
    }
  }

  time_t nextRun(uint8_t r) const { return (r < 2) ? _next[r] : 0; }

private:
  RelaySchedule _sched[2];
  time_t        _next[2] = {0, 0};
  TriggerCb     _cb = nullptr;
  void         *_ctx = nullptr;

  static void _copy(char *dst, const char *begin, const char *end) {
    size_t len = (size_t)(end - begin);
    if (len >= SCHED_SIDE_LEN) len = SCHED_SIDE_LEN - 1;
    memcpy(dst, begin, len);
    dst[len] = '\0';
  }
};
