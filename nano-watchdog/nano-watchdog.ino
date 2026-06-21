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
 *   - Each ESP32-S3 sends the string "HB\n" over UART at 9600 baud (GPIO21) every 2 s
 *     while its loop() runs.
 *   - The Nano listens on SoftwareSerial (D2 / D3), switching between devices every
 *     LISTEN_SLOT_MS. When it receives the exact token "HB", it records the timestamp.
 *   - Every CHECK_INTERVAL it asks: did this device send a valid heartbeat in the last
 *     window?
 *       * yes -> alive, clear its strike count.
 *       * no  -> one strike. After MAX_STRIKES consecutive empty windows the device is
 *                considered hung/never-booted, and the Nano pulses its reset.
 *   - Reset is OPEN-DRAIN: the Nano only ever pulls the ESP32 EN/CHIP_PU pin DOWN to GND
 *     (then releases to high-Z). It never drives 5 V onto the 3.3 V ESP32 — this is the
 *     same thing the physical reset button does, and it is safe across the voltage gap.
 *
 * WHY UART (not a square wave)
 * ----------------------------
 * Edge counting on a GPIO is fooled by a boot-looping device: the firmware briefly runs
 * before each crash, toggles the pin a few times, and the Nano counts those as "alive".
 * A UART string check is not: the ESP32 boot ROM spews its own output at 115200 baud,
 * which SoftwareSerial (listening at 9600) sees only as framing noise — never "HB".
 * Your specific token can only arrive from correctly-running application firmware.
 *
 * VOLTAGE COMPATIBILITY
 * ---------------------
 * ESP32 TX is 3.3 V logic. The 1 k series resistor + 100 k pulldown form a divider:
 * when ESP32 drives HIGH the Nano sees 3.3 V × 100 k/(101 k) ≈ 3.27 V, above the
 * ATmega's 3.0 V VIH threshold. No level shifter needed for this one-way link.
 *
 * FAIL-SAFE
 * ---------
 * Idle state of every reset pin is INPUT (high-Z), so a dead/hung/unplugged Nano can
 * never hold an ESP32 in reset. Worst case the guardian does nothing and you are back to
 * today's behaviour (manual reset) — it only ever ADDS a recovery path.
 *
 * WIRING (per device, unchanged from the edge-counting version)
 * -----
 *   ESP32 GPIO21 (TX) --[1k]--> Nano D2 (dev0) or D3 (dev1)
 *   100k pulldown from that Nano pin to GND  (keeps line LOW when ESP32 is off)
 *   Nano D4 (dev0) or D5 (dev1) --> ESP32 EN/CHIP_PU (open-drain reset)
 *   ALL grounds common (Nano GND <-> both ESP32 GNDs <-> supply GND)
 */

#include <SoftwareSerial.h>

// ------------------------------------------------------------------ //
//  Configuration                                                       //
// ------------------------------------------------------------------ //
static const uint8_t NUM_DEVICES = 2;

// Heartbeat RX pins (one per ESP32). These receive UART TX from the ESP32.
// Internal pull-up is NOT enabled — it pulls to 5 V and would back-feed the
// 3.3 V ESP32 when powered off. Use the external 100k pulldown to GND instead.
static const uint8_t HB_RX_PIN[NUM_DEVICES]  = { 2, 3 };

// Reset OUTPUTs (one per ESP32). Open-drain: OUTPUT-LOW to reset, INPUT to release.
static const uint8_t RESET_PIN[NUM_DEVICES]  = { 4, 5 };

static const uint8_t STATUS_LED = LED_BUILTIN;   // D13

// Heartbeat UART baud rate. 9600 is plenty for a 3-byte "HB\n" every 2 s.
static const uint16_t HB_BAUD = 9600;

// Listener slot: how long to listen to one device before switching to the other.
// Must be longer than one heartbeat period (2 s) so we're guaranteed to see a
// message if the device is alive; 3 s gives one full period of headroom.
static const uint32_t LISTEN_SLOT_MS = 3000UL;

// Detection window. The user's rule: "check every 60 s, reset after 3 misses."
static const uint32_t CHECK_INTERVAL_MS = 60000UL;
static const uint8_t  MAX_STRIKES       = 3;

// How often to print a live status line (silence duration + strikes).
static const uint32_t STATUS_INTERVAL_MS = 5000UL;

// Once a device has been silent this long, escalate to WARNING in the log.
static const uint32_t SILENCE_WARN_MS = 30000UL;

// How long to hold EN low. The ESP32 needs only a few ms; 200 ms is comfortable.
static const uint16_t RESET_PULSE_MS = 200;

// ------------------------------------------------------------------ //
//  Per-device state                                                    //
// ------------------------------------------------------------------ //
struct DeviceState {
  uint8_t  strikes;
  uint32_t resetCount;
  uint32_t lastValidMs;   // millis() when last valid "HB" token was received
};

static DeviceState dev[NUM_DEVICES];
static uint32_t    lastCheckMs  = 0;
static uint32_t    lastStatusMs = 0;
static uint32_t    lastSwitchMs = 0;
static uint8_t     activeIdx    = 0;   // which device is currently being listened to

// Two SoftwareSerial instances, TX disabled (255 = no TX pin).
// Only one may .listen() at a time — we alternate.
static SoftwareSerial ss0(HB_RX_PIN[0], 255);
static SoftwareSerial ss1(HB_RX_PIN[1], 255);
static SoftwareSerial *const ss[2] = { &ss0, &ss1 };

// Single line buffer — only needs to hold the active device's current line.
static char    lineBuf[16];
static uint8_t lineLen = 0;

// ------------------------------------------------------------------ //
//  Reset — open-drain pulse on the ESP32 EN/CHIP_PU line               //
// ------------------------------------------------------------------ //
static void pulseReset(uint8_t i) {
  digitalWrite(RESET_PIN[i], LOW);    // ensure latch is LOW before driving
  pinMode(RESET_PIN[i], OUTPUT);      // enabling the driver pulls EN straight to GND
  delay(RESET_PULSE_MS);
  pinMode(RESET_PIN[i], INPUT);       // release to high-Z; ESP32 pull-up restores EN

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

  uint32_t now = millis();
  for (uint8_t i = 0; i < NUM_DEVICES; i++) {
    pinMode(RESET_PIN[i], INPUT);   // released (high-Z) — fail-safe idle
    dev[i].strikes     = 0;
    dev[i].resetCount  = 0;
    dev[i].lastValidMs = now;       // assume alive at boot; first window proves it
  }

  ss0.begin(HB_BAUD);
  ss1.begin(HB_BAUD);
  ss[activeIdx]->listen();

  lastCheckMs = lastStatusMs = lastSwitchMs = now;

  Serial.println();
  Serial.println(F("nano-watchdog: guardian online"));
  Serial.print(F("  devices: "));   Serial.println(NUM_DEVICES);
  Serial.print(F("  window:  "));   Serial.print(CHECK_INTERVAL_MS / 1000); Serial.println(F(" s"));
  Serial.print(F("  strikes: "));   Serial.println(MAX_STRIKES);
  Serial.print(F("  heartbeat: ")); Serial.print(HB_BAUD); Serial.println(F(" baud UART, token \"HB\""));
  Serial.println(F("  reset:   open-drain pulse on EN (safe for 3.3 V ESP32)"));
}

// ------------------------------------------------------------------ //
//  Loop                                                                //
// ------------------------------------------------------------------ //
void loop() {
  uint32_t now = millis();

  // 1) Drain the active SoftwareSerial into a line buffer, look for "HB".
  //    Anything else (boot-ROM output, noise, partial lines) is silently discarded.
  while (ss[activeIdx]->available()) {
    char c = (char)ss[activeIdx]->read();
    if (c == '\n' || c == '\r') {
      lineBuf[lineLen] = '\0';
      if (lineLen == 2 && lineBuf[0] == 'H' && lineBuf[1] == 'B') {
        dev[activeIdx].lastValidMs = now;
      }
      lineLen = 0;
    } else if (lineLen < (uint8_t)(sizeof(lineBuf) - 1)) {
      lineBuf[lineLen++] = c;
    } else {
      lineLen = 0;   // line too long — discard and resync
    }
  }

  // 2) Switch which device we're listening to every LISTEN_SLOT_MS.
  //    lastValidMs persists across switches, so a heartbeat received in the
  //    previous slot is still "fresh" when we come back.
  if (now - lastSwitchMs >= LISTEN_SLOT_MS) {
    lastSwitchMs = now;
    lineLen  = 0;                                     // discard partial line
    activeIdx = (activeIdx + 1) % NUM_DEVICES;
    ss[activeIdx]->listen();
  }

  // 3) Frequent status chatter — shows silence duration and strikes per device.
  if (now - lastStatusMs >= STATUS_INTERVAL_MS) {
    lastStatusMs = now;
    for (uint8_t i = 0; i < NUM_DEVICES; i++) {
      uint32_t silentMs = now - dev[i].lastValidMs;
      Serial.print(F("  dev")); Serial.print(i);
      Serial.print(F(": lastSeen=")); Serial.print(silentMs / 1000); Serial.print(F("s ago"));
      if (dev[i].strikes) { Serial.print(F(" strikes=")); Serial.print(dev[i].strikes); }
      if (silentMs >= SILENCE_WARN_MS) {
        Serial.print(F("  *** WARNING: SILENT for "));
        Serial.print(silentMs / 1000);
        Serial.print(F("s — reset in ~"));
        uint32_t winLeft = (MAX_STRIKES > dev[i].strikes) ? (MAX_STRIKES - dev[i].strikes) : 0;
        Serial.print((winLeft * CHECK_INTERVAL_MS) / 1000);
        Serial.print(F("s ***"));
      }
      Serial.println();
    }
  }

  // 4) Once per window, judge each device on whether it sent a valid heartbeat.
  if (now - lastCheckMs >= CHECK_INTERVAL_MS) {
    lastCheckMs = now;

    digitalWrite(STATUS_LED, HIGH); delay(20); digitalWrite(STATUS_LED, LOW);

    for (uint8_t i = 0; i < NUM_DEVICES; i++) {
      uint32_t silentMs = now - dev[i].lastValidMs;
      bool alive = (silentMs < CHECK_INTERVAL_MS);

      Serial.print(F("check dev")); Serial.print(i);
      Serial.print(F(": lastSeen=")); Serial.print(silentMs / 1000); Serial.print(F("s"));
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
          dev[i].strikes = 0;
        }
      }
    }
  }

  // No delay — SoftwareSerial needs fast polling to drain its receive buffer.
}
