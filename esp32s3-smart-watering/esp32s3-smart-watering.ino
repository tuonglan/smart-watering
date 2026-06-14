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
//   V40 Log level        (int 0-4, app→device — runtime serial verbosity)
//       0=OFF 1=ERROR 2=WARN 3=INFO 4=DEBUG. Gates OUR logs only; the ESP32
//       boot ROM messages and Blynk's own banner/output always print. See
//       Settings.h for the LOG_* macros. Default 0 at boot; synced from app.
//   V41 Debug terminal   (WidgetTerminal, app↔device — type debug commands, see
//       output on the same widget). Commands: "ping <ip|host>", "get_moisture", "help",
//       "clear".
//       Parsed once + cached; run by terminalEvent. NOT synced on reconnect. A help
//       guide is printed once on the first connection after boot. See Terminal.h.
//   V42 Terminal mode    (Switch, app→device — 1 = re-run the entered command at 1 Hz
//       until cleared / flipped off / 5-min auto-stop; 0 = run once per entry).
//   V43 Re-run button    (Switch, app↔device — re-runs the last command. Once mode:
//       runs once then auto-releases to 0. Continuous mode: (re)starts the live tail,
//       stays 1 until tapped again or the 5-min guard fires. NOT synced on reconnect.)
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
// mqtt2prometheus exporter turns them into Prometheus metrics. The same message also
// carries both relay/pump states (r0/r1, 1=on); a relay change is published within
// ~1 s so short runs are not missed. See Moisture.h for the V11 string format and
// monitoring/ for the broker + exporter stack.

#define BLYNK_FIRMWARE_VERSION  "1.5.0"
#define BLYNK_PRINT             Serial
#define APP_DEBUG

// Tell Edgent which board we are on. The local Settings.h overrides the button
// and LED pins directly, so this define is informational, but keep it accurate.
#define USE_ESP32S3_DEV_MODULE

#define BLYNK_TEMPLATE_ID   "TMPL6yGZY2olP"
#define BLYNK_TEMPLATE_NAME "Smart Watering"

#include "Settings.h"
#include "BlynkEdgent.h"

// ------------------------------------------------------------------ //
//  Scheduling configuration (override the header-file defaults here)   //
// ------------------------------------------------------------------ //
#define SCHED_TZ          "<+07>-7"   // Vietnam, UTC+7, no DST (POSIX sign is inverted)
#define SCHED_TZ_SUFFIX   "+07"       // appended to the V9 datetime string
#define MAX_LATENESS_S    600         // skip a scheduled start more than 10 min late

#include "TimeManager.h"     // NTP wall-clock (Blynk-free)
#include "Schedule.h"        // schedule parsing + per-second engine (Blynk-free)
#include "Moisture.h"        // V11 config parsing + ADC1 sampling
#include "MqttPublisher.h"   // moisture -> local MQTT broker (needs PubSubClient lib)
#include "Terminal.h"        // V41 debug WidgetTerminal + command framework (needs ESP32Ping lib)
#include "RelayController.h" // one relay/pump + the 300 s safe-protocol (RELAY_GPIO, MAX_RUNNING_TIME_S)

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
#define VPIN_LOG_LEVEL       V40
#define VPIN_TERMINAL        V41   // WidgetTerminal: debug command in/out (see Terminal.h)
#define VPIN_TERM_MODE       V42   // Switch: 1 = run command @1Hz, 0 = run once
#define VPIN_TERM_RERUN      V43   // Switch: re-run the last command (see Terminal.h)

// The RelayController class, the RELAY_GPIO pin map, and the safe-protocol
// constants (MAX_RUNNING_TIME_S, FAILSAFE_MARGIN_MS) now live in RelayController.h,
// included above. They are kept together there so the 300 s ceiling can never drift
// apart from the code that enforces it.

// ------------------------------------------------------------------ //
//  Globals                                                             //
// ------------------------------------------------------------------ //
RelayController relays[2];
BlynkTimer      mainTimer;
uint32_t        boot_ms;

// Runtime serial-log verbosity, driven by V40 (see Settings.h). Boot default
// 0/OFF: a quiet boot showing only the framework banner + any promoted errors,
// until the app syncs V40 (or the user raises it live). Referenced by the LOG_*
// macros when APP_DEBUG is defined; harmless if it isn't.
uint8_t         g_logLevel = LOG_LEVEL_OFF;

TimeManager     timeMgr;
ScheduleManager schedMgr;
bool            g_time_was_valid = false;   // rising-edge latch for first NTP sync

MoistureConfig  moistCfg;                   // V11 config (pins, device, broker, interval)
MqttPublisher   mqttPub;                     // pushes readings to the local broker
uint32_t        last_publish_ms = 0;         // 0 = publish on the next tick
bool            last_relay_state[2] = { false, false };  // edge-detect for out-of-cadence publishes

TerminalManager dbgTerm;                     // V41 debug console + command framework
bool            g_termContinuous = false;    // V42: true = re-run command @1Hz, false = once
bool            g_termHelpShown   = false;   // latch: print the V41 help once, on first connect

// Scheduler asks us to start a relay; we honour the "skip if already running" rule.
static void onScheduleTrigger(uint8_t relay_idx, void * /*ctx*/) {
  if (relay_idx >= 2) return;
  if (!relays[relay_idx].isOn()) {
    relays[relay_idx].startScheduled();
    LOG_INFO(String("schedule: relay ") + relay_idx + " started by schedule");
  } else {
    DEBUG_PRINT(String("schedule: relay ") + relay_idx + " trigger skipped (already running)");
  }
}

// DEBUG dump of each relay's next scheduled firing (local time), e.g. after a V10
// change or the first NTP sync. The work is skipped unless we're actually at DEBUG.
static void logNextRuns() {
#ifdef APP_DEBUG
  if (g_logLevel < LOG_LEVEL_DEBUG) return;
  for (uint8_t r = 0; r < 2; r++) {
    time_t nr = schedMgr.nextRun(r);
    if (nr == 0) {
      DEBUG_PRINT(String("schedule: relay ") + r + " next run: (disabled)");
      continue;
    }
    struct tm lt;
    localtime_r(&nr, &lt);
    char when[32];
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &lt);
    DEBUG_PRINT(String("schedule: relay ") + r + " next run: " + when);
  }
#endif
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
  DEBUG_PRINT("blynk: connected — NTP (re)started, syncing datastreams");
  // V10 restores the stored schedule, V11 the moisture/MQTT config (each triggers
  // its BLYNK_WRITE below).
  // V42 restores the terminal run mode. V41 is deliberately NOT synced: a command left
  // in the terminal datastream must not auto-resume after a reboot/reconnect.
  Blynk.syncVirtual(VPIN_RELAY0_SWITCH, VPIN_RELAY0_RUNTIME, VPIN_RELAY0_TIMEON, VPIN_RELAY0_RUNMAX,
                    VPIN_RELAY1_SWITCH, VPIN_RELAY1_RUNTIME, VPIN_RELAY1_TIMEON, VPIN_RELAY1_RUNMAX,
                    VPIN_SCHEDULE_CFG, VPIN_MOISTURE_CFG, VPIN_LOG_LEVEL, VPIN_TERM_MODE);

  // Print the command guide once per boot (on the first connection), not on every
  // reconnect — so a WiFi blip doesn't spam the terminal.
  if (!g_termHelpShown) {
    dbgTerm.printHelp();
    Blynk.virtualWrite(VPIN_TERM_RERUN, 0);   // start with the re-run button released (clear any stale state)
    g_termHelpShown = true;
  }
}

// Runtime serial-log verbosity from the app (0=OFF 1=ERROR 2=WARN 3=INFO 4=DEBUG).
// Clamped to range. The confirmation uses BLYNK_LOG1 directly so it prints even at
// level 0 (it's framework output, not gated by g_logLevel) — you always get feedback
// that the level changed.
BLYNK_WRITE(VPIN_LOG_LEVEL) {
  int v = param.asInt();
  if (v < LOG_LEVEL_OFF)   v = LOG_LEVEL_OFF;
  if (v > LOG_LEVEL_DEBUG) v = LOG_LEVEL_DEBUG;
  g_logLevel = (uint8_t)v;
  BLYNK_LOG1(String("log level -> ") + g_logLevel);
}

// Debug terminal (V41): a typed line. Parsed + cached in onInput() and run once for
// immediate feedback; if V42 (continuous) is on it keeps re-running via terminalEvent.
BLYNK_WRITE(VPIN_TERMINAL) {
  dbgTerm.onInput(param.asStr(), g_termContinuous);
}

// Terminal run mode (V42): 1 = re-run the entered command continuously @1Hz, 0 = once.
BLYNK_WRITE(VPIN_TERM_MODE) {
  g_termContinuous = (param.asInt() != 0);
  LOG_INFO(String("terminal mode -> ") + (g_termContinuous ? "continuous" : "once"));
}

// Re-run button (V43): tap to re-run the last command. In once mode it runs a single
// time and the button auto-releases (V43->0); in continuous mode it (re)starts the live
// tail and stays engaged until tapped again (->0) or the 5-min guard fires.
BLYNK_WRITE(VPIN_TERM_RERUN) {
  if (param.asInt() != 0) dbgTerm.rerun(g_termContinuous);
  else                    dbgTerm.stopRerun();
}

// Schedule config from the app. Parse, (re)arm, and echo "INVALID FORMAT" back for
// any malformed side so the user sees the rejection.
BLYNK_WRITE(VPIN_SCHEDULE_CFG) {
  char echo[2 * SCHED_SIDE_LEN + 2];
  bool rewrite = schedMgr.apply(param.asStr(), echo, sizeof(echo),
                                timeMgr.now(), timeMgr.valid());
  if (rewrite) {
    Blynk.virtualWrite(VPIN_SCHEDULE_CFG, echo);
    LOG_WARN(String("V10 had an invalid side, rewritten to: ") + echo);
  } else {
    LOG_INFO(String("V10 applied: ") + param.asStr());
  }
  logNextRuns();   // DEBUG: show the (re)armed next-run instants
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
    LOG_INFO(String("V11 applied: ") + moistCfg.channelCount() + " ch, " +
             moistCfg.relayCount() + " relay -> " + moistCfg.device() + " @ " +
             moistCfg.host() + ":" + moistCfg.port() + " every " + moistCfg.intervalS() + "s");
  } else {
    LOG_WARN(String("V11 rejected: '") + param.asStr() + "' -> INVALID FORMAT");
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
  DEBUG_PRINT(String("datetime: ") + buf);
}

// Per-second schedule engine. Gated on a valid clock; arms next-run epochs on the
// first valid tick, then evaluates due starts.
void scheduleEvent() {
  if (!timeMgr.valid()) return;

  time_t now = timeMgr.now();
  if (!g_time_was_valid) {
    g_time_was_valid = true;
    schedMgr.onTimeValid(now);   // compute next-run for both relays now that we know the time
    char buf[32];
    timeMgr.formatLocal(buf, sizeof(buf));
    LOG_INFO(String("NTP synced: ") + buf + " — schedules armed");
    logNextRuns();
  }
  schedMgr.evaluate(now);
}

// Moisture + relay publish tick (1 Hz). Self-paces to V11's interval via
// last_publish_ms, so changing the interval needs no timer juggling. Also publishes
// out of cadence whenever an enabled relay changes state, so a short pump run
// (default 10 s) is not missed by the periodic grid. Samples each enabled channel,
// tacks on each enabled relay state, and pushes one MQTT message; only the ids
// selected in V11 are published. A failed publish just retries next tick.
void moistureEvent() {
  // Edge-logged so the "nothing is publishing and I don't know why" case is visible
  // once (not 1×/s) when V11 is unset/invalid. Cleared when a valid config arrives.
  static bool warnedInvalid = false;
  if (!moistCfg.valid()) {
    if (!warnedInvalid) {
      LOG_WARN("moisture: V11 not set or invalid — not publishing");
      warnedInvalid = true;
    }
    return;
  }
  warnedInvalid = false;

  bool relayOn[MOIST_RELAYS]      = { relays[0].isOn(), relays[1].isOn() };
  bool relayEnabled[MOIST_RELAYS] = { moistCfg.relayEnabled(0), moistCfg.relayEnabled(1) };

  bool relayChanged = false;
  for (uint8_t i = 0; i < MOIST_RELAYS; i++)
    if (relayEnabled[i] && relayOn[i] != last_relay_state[i]) relayChanged = true;

  uint32_t now = millis();
  bool due = (last_publish_ms == 0) || ((now - last_publish_ms) >= moistCfg.intervalMs());
  if (!due && !relayChanged) return;

  last_publish_ms = now;
  for (uint8_t i = 0; i < MOIST_RELAYS; i++) last_relay_state[i] = relayOn[i];

  int  vals[MOIST_MAX_PINS];
  bool chEnabled[MOIST_MAX_PINS];
  for (uint8_t i = 0; i < MOIST_MAX_PINS; i++) {
    chEnabled[i] = moistCfg.channelEnabled(i);
    vals[i]      = chEnabled[i] ? moistCfg.readChannel(i) : 0;
  }

  bool ok = mqttPub.publish(vals, chEnabled, MOIST_MAX_PINS,
                            relayOn, relayEnabled, MOIST_RELAYS);
  // Success summary here; the specific failure reason (WiFi down / broker
  // unreachable+rc / broker rejected) is logged inside MqttPublisher.
  if (ok) LOG_INFO("moisture: published");
}

// Debug-terminal tick (1 Hz). Idle unless a continuous (V42=1) command is cached; then
// it re-runs that command and enforces the 5-min auto-stop quota guard. See Terminal.h.
void terminalEvent() {
  dbgTerm.tick(g_termContinuous);
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
  mainTimer.setInterval(1000L,   terminalEvent);  // V41 debug terminal — 1 Hz tick, idle unless a command is active

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
