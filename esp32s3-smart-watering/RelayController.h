// RelayController — one watering relay/pump, with a hard guarantee that the pin is
// NEVER HIGH longer than MAX_RUNNING_TIME_S (300 s).
//
// Safe-protocol (3 independent layers, all enforced here so they can never drift
// apart from the constants that define them):
//   1. Every requested duration is clamped to MAX_RUNNING_TIME_S in _start_timers(),
//      and the manual Running Time slider (V1/V5) is clamped in _handle_run_time().
//   2. A BlynkTimer software stop fires at the intended duration (_cb_stop) and a
//      software backstop in run() forces the pin LOW at _hard_cap_ms.
//   3. A hardware-backed esp_timer (priority-22 task, independent of loop()) forces
//      the pin LOW even if loop() is blocked during a WiFi/Blynk reconnect. Its
//      deadline is itself clamped to the ceiling, so layer 3 is the last word.
//
// One instance per relay; index 0 -> GPIO38/V0.., index 1 -> GPIO39/V4.. .
//
// Dependency: Blynk (BlynkParam, BlynkTimer, Blynk.virtualWrite). Following this
// project's convention, Blynk is NOT included here — the .ino includes BlynkEdgent.h
// before this header, so the symbols are already visible (same as Terminal.h).

#pragma once

#include <Arduino.h>
#include "esp_timer.h"   // hardware-backed failsafe timer (independent of loop())

// ------------------------------------------------------------------ //
//  Hardware pin for each relay                                         //
// ------------------------------------------------------------------ //
//  GPIO38 / GPIO39 are free general-purpose pins on this N16R8 board:
//  they are clear of the octal-PSRAM data lines (GPIO35-37) and are only
//  labelled SD_CMD / SD_CLK (used by the unused on-board SD slot). HIGH = on.
static const uint8_t RELAY_GPIO[2] = {38, 39};

// ------------------------------------------------------------------ //
//  Run-time ceiling — the heart of the safe-protocol                   //
// ------------------------------------------------------------------ //
static const uint32_t MAX_RUNNING_TIME_S = 300;    // absolute ceiling, seconds — the pin is NEVER HIGH longer than this
static const uint32_t FAILSAFE_MARGIN_MS = 2000;   // grace so the normal timer wins before the failsafe trips

class RelayController {
public:
  RelayController() {}

  void begin(uint8_t relay_idx) {
    _idx = relay_idx;
    _gpio = RELAY_GPIO[relay_idx];
    // Vpin base: relay 0 → V0, relay 1 → V4
    _vpin_switch    = relay_idx * 4;
    _vpin_run_time  = relay_idx * 4 + 1;
    _vpin_time_on   = relay_idx * 4 + 2;
    _vpin_run_max   = relay_idx * 4 + 3;

    // Create the hardware-backed failsafe BEFORE touching the pin, so
    // _disable_relay() (which stops it) always has a valid handle.
    _failsafe_tripped = false;
    esp_timer_create_args_t fs_args = {};
    fs_args.callback        = &_failsafe_cb;
    fs_args.arg             = this;
    fs_args.dispatch_method = ESP_TIMER_TASK;   // high-priority task, preempts loop()
    fs_args.name            = "relay_failsafe";
    esp_timer_create(&fs_args, &_failsafe_timer);

    pinMode(_gpio, OUTPUT);
    _disable_relay();

    _running_time_s   = 10;      // matches Blynk datastream default
    _run_max          = false;
    _relay_on         = false;
    _flag_stop        = false;
    _start_ms         = 0;
    _hard_cap_ms      = 0;
    _last_elapsed_s   = 0;

    _timer_countdown_id = -1;
    _timer_timeon_id    = -1;
  }

  // Call every loop iteration
  void run() {
    _timer.run();

    // The hardware failsafe already forced the pin LOW from its own task.
    // Reconcile bookkeeping and notify Blynk here, in loop context.
    if (_failsafe_tripped) {
      _failsafe_tripped = false;
      _shutdown(true);
      return;
    }

    // Software backstop (secondary to the esp_timer failsafe).
    if (_relay_on) {
      uint32_t elapsed_ms = millis() - _start_ms;
      if (_flag_stop || elapsed_ms >= _hard_cap_ms) {
        _shutdown(true);
      }
    }
  }

  // Called by BLYNK_WRITE_DEFAULT
  void vpin_handler(uint8_t vpin, BlynkParam const &param) {
    uint8_t offset = vpin % 4;
    switch (offset) {
      case 0: _handle_switch(param);    break;
      case 1: _handle_run_time(param);  break;
      case 2: /* time_on is read-only */ break;
      case 3: _handle_run_max(param);   break;
    }
  }

  bool isOn() const { return _relay_on; }

  // Start a timed run from the scheduler. Identical to a manual switch-on (same
  // Running Time, same 300 s ceiling + 3-layer failsafe), and reflects the run in
  // the app. The caller is responsible for skipping this when already running.
  void startScheduled() {
    if (_relay_on || _run_max) return;
    _turnOn();
    Blynk.virtualWrite(_vpin_switch, 1);
  }

private:
  uint8_t  _idx;
  uint8_t  _gpio;
  uint8_t  _vpin_switch;
  uint8_t  _vpin_run_time;
  uint8_t  _vpin_time_on;
  uint8_t  _vpin_run_max;

  int      _running_time_s;
  bool     _run_max;
  bool     _relay_on;
  bool     _flag_stop;
  uint32_t _start_ms;
  uint32_t _hard_cap_ms;        // backstop deadline, ms since _start_ms (<= MAX_RUNNING_TIME_S)
  uint32_t _last_elapsed_s;

  int _timer_countdown_id;
  int _timer_timeon_id;
  BlynkTimer _timer;

  esp_timer_handle_t _failsafe_timer;     // fires from a high-priority task, not loop()
  volatile bool      _failsafe_tripped;   // set by the failsafe ISR-equivalent, cleared in run()

  // ---------- relay hardware ----------

  void _enable_relay() {
    digitalWrite(_gpio, HIGH);
    _relay_on = true;
  }

  // Energise the relay and arm the timers for the configured Running Time. Shared by
  // the manual switch handler and the scheduler so both paths behave identically.
  void _turnOn() {
    _enable_relay();
    _start_timers((uint32_t)_running_time_s * 1000UL);
  }

  void _disable_relay() {
    esp_timer_stop(_failsafe_timer);   // disarm failsafe whenever the pin goes LOW
    digitalWrite(_gpio, LOW);
    _relay_on = false;
  }

  // Runs in the esp_timer task (priority 22), so it fires even if loop() is
  // blocked during a WiFi/Blynk reconnect. Kills the pump immediately.
  static void _failsafe_cb(void *ctx) {
    RelayController *rc = (RelayController *)ctx;
    digitalWrite(rc->_gpio, LOW);
    rc->_failsafe_tripped = true;   // loop() reconciles state + tells Blynk
  }

  // ---------- timer helpers ----------

  static void _cb_stop(void *ctx) {
    ((RelayController *)ctx)->_flag_stop = true;
  }

  static void _cb_timeon(void *ctx) {
    RelayController *rc = (RelayController *)ctx;
    uint32_t elapsed_s = (millis() - rc->_start_ms) / 1000UL;
    if (elapsed_s != rc->_last_elapsed_s) {
      rc->_last_elapsed_s = elapsed_s;
      Blynk.virtualWrite(rc->_vpin_time_on, elapsed_s);
    }
  }

  void _start_timers(uint32_t duration_ms) {
    // Never run longer than the absolute ceiling, no matter what was requested.
    uint32_t max_ms = MAX_RUNNING_TIME_S * 1000UL;
    if (duration_ms > max_ms) duration_ms = max_ms;

    _start_ms       = millis();
    _flag_stop      = false;
    _last_elapsed_s = 0;

    // Backstop fires a little past the intended stop, but is itself clamped
    // to the ceiling — so the pin is never HIGH beyond MAX_RUNNING_TIME_S.
    _hard_cap_ms = duration_ms + FAILSAFE_MARGIN_MS;
    if (_hard_cap_ms > max_ms) _hard_cap_ms = max_ms;

    _timer_countdown_id = _timer.setTimeout(duration_ms, _cb_stop, this);
    _timer_timeon_id    = _timer.setInterval(1000L, _cb_timeon, this);

    // Arm the hardware failsafe. This is the guarantee that survives a
    // blocked loop(): the esp_timer task will force the pin LOW regardless.
    esp_timer_stop(_failsafe_timer);
    esp_timer_start_once(_failsafe_timer, (uint64_t)_hard_cap_ms * 1000ULL);
  }

  void _stop_timers() {
    if (_timer_countdown_id >= 0) {
      _timer.deleteTimer(_timer_countdown_id);
      _timer_countdown_id = -1;
    }
    if (_timer_timeon_id >= 0) {
      _timer.deleteTimer(_timer_timeon_id);
      _timer_timeon_id = -1;
    }
  }

  // ---------- full shutdown — push state to Blynk ----------

  void _shutdown(bool update_blynk) {
    _stop_timers();
    _disable_relay();
    _flag_stop = false;

    if (update_blynk) {
      Blynk.virtualWrite(_vpin_switch,   0);
      Blynk.virtualWrite(_vpin_run_max,  0);
    }
    Blynk.virtualWrite(_vpin_time_on, 0);
    _run_max = false;
  }

  // ---------- Blynk vpin handlers ----------

  void _handle_switch(BlynkParam const &param) {
    int value = param.asInt();

    // Run Max takes priority — ignore switch presses while active
    if (_run_max) {
      Blynk.virtualWrite(_vpin_switch, 0);
      return;
    }

    if (value == 1) {
      _turnOn();
    } else {
      _shutdown(false);
    }
  }

  void _handle_run_time(BlynkParam const &param) {
    int v = param.asInt();
    if (v < 1) v = 1;
    if (v > (int)MAX_RUNNING_TIME_S) v = (int)MAX_RUNNING_TIME_S;  // never above the ceiling
    _running_time_s = v;
  }

  void _handle_run_max(BlynkParam const &param) {
    int value = param.asInt();

    // Switch already on — ignore
    if (_relay_on && !_run_max) {
      Blynk.virtualWrite(_vpin_run_max, 0);
      return;
    }

    if (value == 1) {
      _run_max = true;
      _enable_relay();
      _start_timers(MAX_RUNNING_TIME_S * 1000UL);
    } else {
      _run_max = false;
      _shutdown(false);
    }
  }
};
