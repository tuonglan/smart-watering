/*
 * nano-watchdog — external hardware reset guardian for the ESP32-S3 smart-watering devices
 * Board: classic Arduino Nano (ATmega328P, 5 V logic), no WiFi, no radios — on purpose.
 *
 * WHY THIS EXISTS
 * ---------------
 * After a LONG power outage the ESP32-S3 boards sometimes fail their cold boot on the
 * slow power-rail ramp: GPIO0 is sampled at the wrong moment and the chip lands in
 * UART download mode, so the firmware never runs. Only a reset (asserting EN once the
 * rails are stable) recovers it — exactly what pressing the reset button does. This Nano
 * automates that button press so an unattended device recovers on its own.
 *
 * The Nano is the right tool for the job because its power-on reset is dead simple and
 * robust (internal POR + brown-out detect, no strapping pins, no boot-mode trap) — it
 * survives the very cold-start condition that trips up the ESP32, so the guardian itself
 * never gets stuck.
 *
 * HOW IT WORKS
 * ------------
 *   - Each ESP32-S3 toggles a "heartbeat" GPIO (GPIO21) at ~2 Hz while its loop() runs.
 *   - The Nano counts edges on that line. Every CHECK_INTERVAL it asks: did this device
 *     produce a healthy stream of edges in the last window?
 *       * yes -> the device is alive, clear its strike count.
 *       * no  -> one strike. After MAX_STRIKES consecutive empty windows the device is
 *                considered hung/never-booted, and the Nano pulses its reset.
 *   - Reset is OPEN-DRAIN: the Nano only ever pulls the ESP32 EN/CHIP_PU pin DOWN to GND
 *     (then releases to high-Z). It never drives 5 V onto the 3.3 V ESP32 — this is the
 *     same thing the physical reset button does, and it is safe across the voltage gap.
 *
 * FAIL-SAFE
 * ---------
 * Idle state of every reset pin is INPUT (high-Z), so a dead/hung/unplugged Nano can
 * never hold an ESP32 in reset. Worst case the guardian does nothing and you are back to
 * today's behaviour (manual reset) — it only ever ADDS a recovery path.
 *
 * WIRING — see README.md in this directory. In short, per device:
 *   ESP32 GPIO21 --[1k]--> Nano HB pin , and a 100k pulldown from that Nano pin to GND
 *   Nano RESET pin -------> ESP32 EN/CHIP_PU node (the non-GND side of its reset button)
 *   ALL grounds common (Nano GND <-> both ESP32 GNDs <-> supply GND).
 */

// ------------------------------------------------------------------ //
//  Configuration                                                       //
// ------------------------------------------------------------------ //
static const uint8_t NUM_DEVICES = 2;

// Heartbeat INPUTs (one per ESP32). D2/D3 are interrupt-capable but we poll.
// Do NOT enable the internal pull-up on these — it pulls to 5 V and would back-feed
// the 3.3 V ESP32 when it is powered off. Use the external 100k pulldown instead.
static const uint8_t HEARTBEAT_PIN[NUM_DEVICES] = { 2, 3 };

// Reset OUTPUTs (one per ESP32). Open-drain: OUTPUT-LOW to reset, INPUT to release.
static const uint8_t RESET_PIN[NUM_DEVICES]     = { 4, 5 };

static const uint8_t STATUS_LED = LED_BUILTIN;  // D13 — blinks each check, flashes on reset

// Detection window. The user's rule: "check every 60 s, reset after 3 misses."
// 3 empty windows ~= 180 s, which also gives a freshly reset device plenty of time to
// boot and start its own heartbeat before it could be struck again.
static const uint32_t CHECK_INTERVAL_MS = 60000UL;
static const uint8_t  MAX_STRIKES       = 3;

// How often to print a live status line for each device (silence duration, edge count,
// strikes). This is just chatter for the serial monitor — it does NOT trigger resets;
// the per-window check above is the only thing that does. Lets you watch a device go
// quiet in real time instead of waiting up to a full minute for the next check.
static const uint32_t STATUS_INTERVAL_MS = 5000UL;

// Once a device has been silent for this long, the status line escalates from a plain
// report to a "WARNING" so a developing problem is obvious well before the reset fires.
static const uint32_t SILENCE_WARN_MS = 30000UL;

// A healthy device emits ~240 edges per 60 s window (2 Hz square wave). Requiring a
// handful, not just one, rejects stray electrical noise from being read as "alive".
static const uint16_t MIN_EDGES_PER_WINDOW = 4;

// How long to hold EN low. The ESP32 needs only a few ms; 200 ms is comfortable margin.
static const uint16_t RESET_PULSE_MS = 200;

// ------------------------------------------------------------------ //
//  Per-device state                                                    //
// ------------------------------------------------------------------ //
struct DeviceState {
  bool     lastLevel;     // last sampled heartbeat level, for edge detection
  uint16_t edges;         // edges counted in the current window
  uint8_t  strikes;       // consecutive empty windows
  uint32_t resetCount;    // how many times we've reset this device (for logging)
  uint32_t lastEdgeMs;    // millis() of the most recent edge — drives the silence report
};

static DeviceState dev[NUM_DEVICES];
static uint32_t     lastCheckMs  = 0;
static uint32_t     lastStatusMs = 0;

// ------------------------------------------------------------------ //
//  Reset — open-drain pulse on the ESP32 EN/CHIP_PU line               //
// ------------------------------------------------------------------ //
static void pulseReset(uint8_t i) {
  digitalWrite(RESET_PIN[i], LOW);    // ensure the latch is LOW before we drive...
  pinMode(RESET_PIN[i], OUTPUT);      // ...so enabling the driver pulls EN straight to GND
  delay(RESET_PULSE_MS);
  pinMode(RESET_PIN[i], INPUT);       // release to high-Z; the ESP32's pull-up restores EN

  // Quick triple-flash so the event is visible on the onboard LED too.
  for (uint8_t f = 0; f < 3; f++) {
    digitalWrite(STATUS_LED, HIGH); delay(60);
    digitalWrite(STATUS_LED, LOW);  delay(60);
  }
}

// ------------------------------------------------------------------ //
//  Setup                                                               //
// ------------------------------------------------------------------ //
void setup() {
  Serial.begin(115200);

  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  for (uint8_t i = 0; i < NUM_DEVICES; i++) {
    pinMode(HEARTBEAT_PIN[i], INPUT);   // no pull-up (see note above)
    pinMode(RESET_PIN[i], INPUT);       // released (high-Z) — fail-safe idle
    dev[i].lastLevel  = digitalRead(HEARTBEAT_PIN[i]);
    dev[i].edges      = 0;
    dev[i].strikes    = 0;
    dev[i].resetCount = 0;
    dev[i].lastEdgeMs = millis();   // assume alive at boot; the first window proves it
  }

  lastCheckMs  = millis();   // first evaluation is one full window from now (boot grace)
  lastStatusMs = millis();

  Serial.println();
  Serial.println(F("nano-watchdog: guardian online"));
  Serial.print(F("  devices: "));        Serial.println(NUM_DEVICES);
  Serial.print(F("  window:  "));        Serial.print(CHECK_INTERVAL_MS / 1000); Serial.println(F(" s"));
  Serial.print(F("  strikes: "));        Serial.println(MAX_STRIKES);
  Serial.println(F("  reset:   open-drain pulse on EN (safe for 3.3 V ESP32)"));
}

// ------------------------------------------------------------------ //
//  Loop                                                                //
// ------------------------------------------------------------------ //
void loop() {
  uint32_t now = millis();

  // 1) Continuously sample every heartbeat line and count edges. This must run far
  //    faster than the heartbeat (2 Hz) so we never miss a transition.
  for (uint8_t i = 0; i < NUM_DEVICES; i++) {
    bool level = digitalRead(HEARTBEAT_PIN[i]);
    if (level != dev[i].lastLevel) {
      dev[i].lastLevel  = level;
      dev[i].lastEdgeMs = now;
      if (dev[i].edges < 0xFFFF) dev[i].edges++;
    }
  }

  // 2) Frequent status chatter (does not trigger resets). Shows, per device, how long
  //    it has been since the last edge and how many edges this window has — so you can
  //    watch a device fall silent live instead of waiting for the 60 s check.
  if (now - lastStatusMs >= STATUS_INTERVAL_MS) {
    lastStatusMs = now;
    for (uint8_t i = 0; i < NUM_DEVICES; i++) {
      uint32_t silentMs = now - dev[i].lastEdgeMs;
      Serial.print(F("  dev")); Serial.print(i);
      Serial.print(F(": edges=")); Serial.print(dev[i].edges);
      Serial.print(F(" lastSeen=")); Serial.print(silentMs / 1000); Serial.print(F("s ago"));
      if (dev[i].strikes) { Serial.print(F(" strikes=")); Serial.print(dev[i].strikes); }
      if (silentMs >= SILENCE_WARN_MS) {
        Serial.print(F("  *** WARNING: SILENT for "));
        Serial.print(silentMs / 1000);
        Serial.print(F("s — reset in ~"));
        // Rough countdown: strikes already banked + the windows still needed.
        uint32_t winLeft = (MAX_STRIKES > dev[i].strikes) ? (MAX_STRIKES - dev[i].strikes) : 0;
        Serial.print((winLeft * CHECK_INTERVAL_MS) / 1000);
        Serial.print(F("s ***"));
      }
      Serial.println();
    }
  }

  // 3) Once per window, judge each device on the edges it produced.
  if (now - lastCheckMs >= CHECK_INTERVAL_MS) {
    lastCheckMs = now;

    // Slow blink to show the guardian itself is alive and checking.
    digitalWrite(STATUS_LED, HIGH); delay(20); digitalWrite(STATUS_LED, LOW);

    for (uint8_t i = 0; i < NUM_DEVICES; i++) {
      bool alive = (dev[i].edges >= MIN_EDGES_PER_WINDOW);

      Serial.print(F("check dev")); Serial.print(i);
      Serial.print(F(": edges=")); Serial.print(dev[i].edges);
      Serial.print(alive ? F(" ALIVE") : F(" SILENT"));

      if (alive) {
        dev[i].strikes = 0;
        Serial.println();
      } else {
        dev[i].strikes++;
        Serial.print(F(" strike ")); Serial.print(dev[i].strikes);
        Serial.print(F("/")); Serial.println(MAX_STRIKES);

        if (dev[i].strikes >= MAX_STRIKES) {
          dev[i].resetCount++;
          Serial.print(F(">>> dev")); Serial.print(i);
          Serial.print(F(": no heartbeat — pulsing RESET (#"));
          Serial.print(dev[i].resetCount); Serial.println(F(")"));
          pulseReset(i);
          // Clear strikes so the freshly reset device gets a full fresh window-set
          // (~180 s) to boot and start its heartbeat before it could be struck again.
          dev[i].strikes = 0;
        }
      }

      dev[i].edges = 0;   // reset the counter for the next window
    }
  }

  delay(2);   // ~500 samples/s — plenty for a 2 Hz heartbeat, easy on the CPU
}
