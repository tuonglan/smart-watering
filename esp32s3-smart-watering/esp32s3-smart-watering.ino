// Blynk Edgent — UICPAL ESP32-S3-CAM N16R8 (ESP32-S3-WROOM-1)
// WiFi credentials and auth token are provisioned at runtime via the Blynk app.
// No hardcoded passwords or tokens.
//
// Virtual pin layout (from Smart Watering.csv):
//   V0  Switch Relay 0   (button 0/1)     → GPIO38
//   V1  Running Time 0   (slider 0-300 s)
//   V2  Time On 0        (read-only display, seconds elapsed)
//   V3  Run Max 0        (toggle: run to MAX_RUNNING_TIME)
//   V4  Switch Relay 1   (button 0/1)     → GPIO39
//   V5  Running Time 1   (slider 0-300 s)
//   V6  Time On 1        (read-only display, seconds elapsed)
//   V7  Run Max 1        (toggle: run to MAX_RUNNING_TIME)
//   V8  Uptime           (String "2d 03h 14m", updated every 10 min, read-only)
//   V9  Device datetime  (String "YYYY-MM-DD HH:MM:SS +07" local, every 5 min, read-only)
//   V10 Schedule config  (String, app→device — see format below)
//   V11 Moisture config  (String, app→device — see Moisture.h)
//
// Relay index = vpin / 4 , pin offset = vpin % 4
//
// Schedule config (V10):  <relay0>|<relay1>     (at most one '|')
//   - no '|' -> relay 0 only;  leading '|' -> relay 1 only;  empty -> no schedule
//   - per relay:  <day>[,<day>]*;<time>[,<time>]*
//       day  : Mon Tue Wed Thu Fri Sat Sun (case-insensitive) or '*' = every day
//       time : HH:MM:SS or HH:MM (seconds default 0)
//   - example:  "Mon,Tue,Wed,Fri;10:30:00,16:30:30"  or  "*;06:00|*;06:05"
//   - a malformed side is echoed back as "INVALID FORMAT" and that relay is disabled
//   A scheduled start uses that relay's Running Time (V1/V5) and the same 300 s
//   ceiling + failsafe as a manual start. Starts only fire once the clock is
//   NTP-valid; until then V9 shows "SYNCING...".
//
// Moisture monitoring (V11): up to 3 analog soil sensors on ADC1 (GPIO4/5/6) are
// sampled and pushed over MQTT to a local broker every <interval> s, where a
// mqtt2prometheus exporter turns them into Prometheus metrics. See Moisture.h for
// the V11 string format and monitoring/ for the broker + exporter stack.

#define BLYNK_FIRMWARE_VERSION  "1.2.0"
#define BLYNK_PRINT             Serial
#define APP_DEBUG

// Tell Edgent which board we are on. The local Settings.h overrides the button
// and LED pins directly, so this define is informational, but keep it accurate.
#define USE_ESP32S3_DEV_MODULE

#define BLYNK_TEMPLATE_ID   "TMPL6yGZY2olP"
#define BLYNK_TEMPLATE_NAME "Smart Watering"

#include "Settings.h"
#include "BlynkEdgent.h"
#include "esp_timer.h"   // hardware-backed failsafe timer (independent of loop())

// ------------------------------------------------------------------ //
//  Scheduling configuration (override the header-file defaults here)   //
// ------------------------------------------------------------------ //
#define SCHED_TZ          "<+07>-7"   // Vietnam, UTC+7, no DST (POSIX sign is inverted)
#define SCHED_TZ_SUFFIX   "+07"       // appended to the V9 datetime string
#define MAX_LATENESS_S    600         // skip a scheduled start more than 10 min late

#include "TimeManager.h"   // NTP wall-clock (Blynk-free)
#include "Schedule.h"      // schedule parsing + per-second engine (Blynk-free)
#include "Moisture.h"      // V11 config parsing + ADC1 sampling
#include "MqttPublisher.h" // moisture -> local MQTT broker (needs PubSubClient lib)

// ------------------------------------------------------------------ //
//  Virtual-pin aliases                                                 //
//  Names for the V# datastreams documented in the table at the top of  //
//  this file. (Relay vpins are still derived as relay_idx*4 + offset    //
//  inside RelayController; these aliases cover the fixed/global pins and //
//  the syncVirtual list.)                                               //
// ------------------------------------------------------------------ //
#define VPIN_RELAY0_SWITCH   V0
#define VPIN_RELAY0_RUNTIME  V1
#define VPIN_RELAY0_TIMEON   V2
#define VPIN_RELAY0_RUNMAX   V3
#define VPIN_RELAY1_SWITCH   V4
#define VPIN_RELAY1_RUNTIME  V5
#define VPIN_RELAY1_TIMEON   V6
#define VPIN_RELAY1_RUNMAX   V7
#define VPIN_UPTIME          V8
#define VPIN_DATETIME        V9
#define VPIN_SCHEDULE_CFG    V10
#define VPIN_MOISTURE_CFG    V11

// ------------------------------------------------------------------ //
//  Hardware pin for each relay                                         //
// ------------------------------------------------------------------ //
//  GPIO38 / GPIO39 are free general-purpose pins on this N16R8 board:
//  they are clear of the octal-PSRAM data lines (GPIO35-37) and are only
//  labelled SD_CMD / SD_CLK (used by the unused on-board SD slot). HIGH = on.
static const uint8_t RELAY_GPIO[2] = {38, 39};

// ------------------------------------------------------------------ //
//  RelayController                                                     //
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

// ------------------------------------------------------------------ //
//  Globals                                                             //
// ------------------------------------------------------------------ //
RelayController relays[2];
BlynkTimer      mainTimer;
uint32_t        boot_ms;

TimeManager     timeMgr;
ScheduleManager schedMgr;
bool            g_time_was_valid = false;   // rising-edge latch for first NTP sync

MoistureConfig  moistCfg;                   // V11 config (pins, device, broker, interval)
MqttPublisher   mqttPub;                     // pushes readings to the local broker
uint32_t        last_publish_ms = 0;         // 0 = publish on the next tick

// Scheduler asks us to start a relay; we honour the "skip if already running" rule.
static void onScheduleTrigger(uint8_t relay_idx, void * /*ctx*/) {
  if (relay_idx < 2 && !relays[relay_idx].isOn()) {
    relays[relay_idx].startScheduled();
  }
}

// ------------------------------------------------------------------ //
//  Blynk callbacks                                                     //
// ------------------------------------------------------------------ //

// Route all virtual writes through the relay index (vpin / 4)
BLYNK_WRITE_DEFAULT() {
  uint8_t vpin = (uint8_t)request.pin;
  if (vpin <= VPIN_RELAY1_RUNMAX) {
    relays[vpin / 4].vpin_handler(vpin, param);
  }
}

// Sync Blynk widgets to device state after reconnect
BLYNK_CONNECTED() {
  // WiFi is up here — (re)start NTP. Idempotent.
  timeMgr.begin();
  // V10 restores the stored schedule, V11 the moisture/MQTT config (each triggers
  // its BLYNK_WRITE below).
  Blynk.syncVirtual(VPIN_RELAY0_SWITCH, VPIN_RELAY0_RUNTIME, VPIN_RELAY0_TIMEON, VPIN_RELAY0_RUNMAX,
                    VPIN_RELAY1_SWITCH, VPIN_RELAY1_RUNTIME, VPIN_RELAY1_TIMEON, VPIN_RELAY1_RUNMAX,
                    VPIN_SCHEDULE_CFG, VPIN_MOISTURE_CFG);
}

// Schedule config from the app. Parse, (re)arm, and echo "INVALID FORMAT" back for
// any malformed side so the user sees the rejection.
BLYNK_WRITE(VPIN_SCHEDULE_CFG) {
  char echo[2 * SCHED_SIDE_LEN + 2];
  bool rewrite = schedMgr.apply(param.asStr(), echo, sizeof(echo),
                                timeMgr.now(), timeMgr.valid());
  if (rewrite) {
    Blynk.virtualWrite(VPIN_SCHEDULE_CFG, echo);
  }
}

// Moisture + MQTT config from the app. Parse, echo "INVALID FORMAT" on a bad string,
// and re-point the publisher at the (possibly new) broker/device. Resetting
// last_publish_ms makes the next tick publish promptly with the new config.
BLYNK_WRITE(VPIN_MOISTURE_CFG) {
  char echo[24];
  bool rewrite = moistCfg.apply(param.asStr(), echo, sizeof(echo));
  if (rewrite) {
    Blynk.virtualWrite(VPIN_MOISTURE_CFG, echo);
  }
  if (moistCfg.valid()) {
    mqttPub.configure(moistCfg.host(), moistCfg.port(), moistCfg.device());
    last_publish_ms = 0;
  }
}

// ------------------------------------------------------------------ //
//  Uptime                                                              //
// ------------------------------------------------------------------ //
void uptimeEvent() {
  uint32_t total_s = (millis() - boot_ms) / 1000UL;
  uint32_t days =  total_s / 86400UL;
  uint32_t hrs  = (total_s % 86400UL) / 3600UL;
  uint32_t mins = (total_s % 3600UL)  / 60UL;

  char buf[24];
  if (days > 0) {
    snprintf(buf, sizeof(buf), "%lud %02luh %02lum",
             (unsigned long)days, (unsigned long)hrs, (unsigned long)mins);
  } else {
    snprintf(buf, sizeof(buf), "%luh %02lum",
             (unsigned long)hrs, (unsigned long)mins);
  }
  Blynk.virtualWrite(VPIN_UPTIME, buf);
}

// ------------------------------------------------------------------ //
//  Time + scheduling ticks                                             //
// ------------------------------------------------------------------ //

// Push the device's local datetime to V9 (or "SYNCING..." until NTP is valid).
void datetimeEvent() {
  char buf[32];
  timeMgr.formatLocal(buf, sizeof(buf));   // writes "SYNCING..." when not yet valid
  Blynk.virtualWrite(VPIN_DATETIME, buf);
}

// Per-second schedule engine. Gated on a valid clock; arms next-run epochs on the
// first valid tick, then evaluates due starts.
void scheduleEvent() {
  if (!timeMgr.valid()) return;

  time_t now = timeMgr.now();
  if (!g_time_was_valid) {
    g_time_was_valid = true;
    schedMgr.onTimeValid(now);   // compute next-run for both relays now that we know the time
  }
  schedMgr.evaluate(now);
}

// Moisture publish tick (1 Hz). Self-paces to V11's interval via last_publish_ms, so
// changing the interval needs no timer juggling. Samples each configured channel and
// pushes one MQTT message; a failed publish just retries next interval.
void moistureEvent() {
  if (!moistCfg.valid()) return;

  uint32_t now = millis();
  if (last_publish_ms != 0 && (now - last_publish_ms) < moistCfg.intervalMs()) return;
  last_publish_ms = now;

  int vals[MOIST_MAX_PINS];
  uint8_t c = moistCfg.count();
  for (uint8_t i = 0; i < c; i++) vals[i] = moistCfg.readChannel(i);

  bool ok = mqttPub.publish(vals, c);
  DEBUG_PRINT(ok ? "moisture: published" : "moisture: publish failed");
}

// ------------------------------------------------------------------ //
//  Arduino entry points                                                //
// ------------------------------------------------------------------ //
void setup() {
  Serial.begin(115200);
  delay(100);

  relays[0].begin(0);
  relays[1].begin(1);

  boot_ms = millis();
  schedMgr.begin(onScheduleTrigger, nullptr);

  MoistureConfig::beginAdc();   // ADC1: 12-bit, full-range attenuation
  mqttPub.begin();

  mainTimer.setInterval(600000L, uptimeEvent);    // every 10 min — uptime needs no finer resolution (Blynk message quota)
  mainTimer.setInterval(300000L, datetimeEvent);  // V9 device datetime every 5 min
  mainTimer.setInterval(1000L,   scheduleEvent);  // schedule engine — 1 Hz, sends nothing unless it fires a relay
  mainTimer.setInterval(1000L,   moistureEvent);  // moisture publish — 1 Hz tick, self-paced to V11 interval

  // Edgent handles WiFi provisioning, auth token storage, OTA, and
  // the factory-reset button (GPIO0 held 5 s).
  BlynkEdgent.begin();
}

void loop() {
  BlynkEdgent.run();
  mainTimer.run();
  mqttPub.loop();

  relays[0].run();
  relays[1].run();
}
