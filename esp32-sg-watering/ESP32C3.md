# ESP32-C3 Super Mini — Exhaustive Technical Reference

> **Document scope:** Offline reference covering every hardware and software aspect of the ESP32-C3 Super Mini development board. Compiled from Espressif official datasheets, Technical Reference Manual, Random Nerd Tutorials, Last Minute Engineers, Mischianti, espboards.dev, done.land, sigmdel.ca, Studio Pieters, GitHub tutorial repositories, Arduino Forum, and Espressif community discussions. Cross-verified across all sources — conflicts noted explicitly.

---

## 1. Board Overview

The **ESP32-C3 Super Mini** is an ultra-compact, breadboard-compatible IoT development board built around Espressif's ESP32-C3 system-on-chip. It is manufactured by multiple third-party vendors ("no-logo" boards) and is not an official Espressif devkit, though it uses genuine ESP32-C3 silicon.

### Physical Dimensions

| Property | Value |
|----------|-------|
| Board width | 18 mm |
| Board length | 22.5 mm (body); ~24 mm including USB-C protrusion |
| Pin pitch | 2.54 mm (standard 0.1" breadboard spacing) |
| Total header pins | 16 (8 per side, castellated pads) |
| Power/GND pins | 3 (one 5V, one 3.3V, one GND) |
| I/O header pins | 13 |
| Form factor | Thumb-sized; single-sided SMD |

### SoC Chip Variant

The board uses the **ESP32-C3FN4** — the "N4" suffix denotes 4 MB of embedded flash, meaning the flash is inside the SoC package itself (no separate flash chip on the PCB).

### Board Variants

| Variant | Description |
|---------|-------------|
| ESP32-C3 Super Mini (standard) | 18×22.5 mm, ceramic PCB antenna, 4 MB flash |
| ESP32-C3 Super Mini Plus (v2) | Larger board, more pins exposed, same chip |
| XIAO ESP32-C3 (Seeed Studio) | Similar chip, different physical form factor |

---

## 2. Processor Specifications

### CPU Core

| Property | Value |
|----------|-------|
| Architecture | RISC-V 32-bit (single-core) |
| ISA extensions | I (base integer), M (multiply/divide), C (compressed 16-bit instructions) |
| Pipeline | 4-stage, in-order, scalar |
| Maximum clock frequency | 160 MHz |
| Default boot clock | External crystal ÷ 2 (typically 20 MHz at boot) |
| Performance | 160–200 MIPS |
| Floating-point unit | **None** — no hardware FPU; F/D RISC-V extensions are NOT implemented |
| Floating-point operations | Handled in software by compiler/runtime libraries (slower than ESP32/S3) |
| Co-processors | **None** — no ULP, no DSP, no dedicated math co-processor |

### Debug & Protection

| Feature | Detail |
|---------|--------|
| Debug interface | RISC-V Debug Specification v0.13 (via USB-Serial/JTAG) |
| Hardware breakpoints/watchpoints | Up to 8 |
| Interrupt controller | Up to 31 vectored interrupts, programmable priority and threshold |
| Physical Memory Protection (PMP) | Up to 16 configurable regions |

### Clock System

- **CPU clock sources:** External main crystal (40 MHz typical), internal RC oscillators
- **RTC slow clock:** ~32 kHz — used for RTC counter, RTC watchdog, low-power controller
- **RTC fast clock:** ~17.5 MHz — used for RTC peripherals and sensor controllers
- **System timer:** 52-bit hardware timer with 2 counters and 3 comparators

---

## 3. Memory

| Memory Type | Size | Notes |
|-------------|------|-------|
| On-chip SRAM | 400 KB total | Shared by data and instructions |
| — of which cache | 16 KB | 8-way set-associative, read-only instruction cache |
| ROM | 384 KB | Boot ROM; contains bootloader and library code |
| RTC FAST memory | 8 KB SRAM | Retained during deep sleep; accessible by main CPU |
| RTC SLOW memory | 8 KB SRAM | Retained during deep sleep |
| Embedded flash | 4 MB | Inside ESP32-C3FN4 SoC package (QIO mode) |
| External flash support | Up to 16 MB | Via SPI0/SPI1 (not present on Super Mini) |
| PSRAM | **Not available** | No PSRAM on ESP32-C3 — 400 KB SRAM is all you get |

### RTC Memory for Deep Sleep

```cpp
RTC_DATA_ATTR int bootCount = 0;  // Survives deep sleep; cleared on full power reset
```

---

## 4. Power

### Input Voltage

| Source | Voltage Range | Notes |
|--------|---------------|-------|
| USB-C port | 5V (USB standard) | Powers onboard LDO; VBUS also goes to 5V header pin |
| 5V header pin | 4.5V – 6V | Input to onboard LDO; **do not exceed 6V** |
| 3.3V header pin | 3.0V – 3.6V | **Bypasses the LDO** — feeds directly to SoC VDD; exceeding 3.6V causes permanent damage |

> **CRITICAL:** Never connect USB and the 5V pin simultaneously — this will damage the computer's USB port or the board's LDO regulator.

### Onboard Voltage Regulator

| Property | Value |
|----------|-------|
| Regulator model | ME6211x33 LDO (or equivalent; varies by production batch) |
| Output voltage | 3.3V |
| Maximum output current | 500 mA (some early batches use 250 mA variants — see Quirks) |
| Dropout voltage | ~100 mV typical |

**Logic level:** All GPIO pins operate at **3.3V**. Never apply 5V signals to any GPIO — this causes immediate permanent damage.

### Current Consumption

| Mode | Current | Notes |
|------|---------|-------|
| Deep sleep (chip only) | ~5 µA | Per sigmdel.ca real-world measurement |
| Deep sleep (datasheet) | ~43 µA | Espressif spec under ideal conditions |
| Deep sleep (with onboard LEDs) | ~600 µA | Realistic — power LED draws most of this current |
| WiFi RX | ~85 mA | Active receiving |
| WiFi TX (maximum) | ~275 mA | Peak at full power; may exceed 250 mA LDO limit on early batches |
| WiFi TX (reduced power) | ~100–150 mA | With `WiFi.setTxPower(WIFI_POWER_8_5dBm)` |

> The onboard **red power indicator LED** is always on when powered; it is the primary reason deep sleep current is 600 µA instead of 43 µA. Removing or desoldering it brings consumption close to datasheet spec.

### Power Indicator LEDs

| LED | Color | GPIO | Controllable? |
|-----|-------|------|--------------|
| Power LED | Red | N/A (tied to 5V rail) | No |
| User LED | Blue | GPIO8 | Yes — **active LOW** (inverted logic) |

---

## 5. Connectivity

### WiFi

| Property | Value |
|----------|-------|
| Standard | IEEE 802.11 b/g/n |
| Frequency band | 2.4 GHz only (no 5 GHz) |
| Maximum data rate | 150 Mbps (802.11n) |
| Channels | 1–14 (country-dependent; 1–11 USA, 1–13 Europe) |
| Security protocols | WPA, WPA2, WPA3-Personal, WPS |
| Modes | Station (STA), Access Point (AP), STA+AP simultaneous, promiscuous/sniffer |
| Antenna | Onboard ceramic PCB antenna (shared with BT via hardware coexistence) |

> Some 2024-batch boards have the crystal oscillator placed only 0.3 mm from the antenna (minimum spec is ≥1.0 mm). This degrades WiFi reliability. Workaround: reduce TX power with `WiFi.setTxPower(WIFI_POWER_8_5dBm)`.

### Bluetooth

| Property | Value |
|----------|-------|
| Standard | Bluetooth 5.0 |
| Type | **BLE only** — Classic Bluetooth is NOT supported |
| Long Range (LR) | Supported (125 Kbps coded PHY) |
| Speed range | 125 Kbps (LR coded) to 2 Mbps (LE 2M PHY) |
| TX power levels | −24 dBm to +18 dBm |
| Coexistence | Hardware WiFi/BT coexistence arbitration built in |

---

## 6. Complete Pinout

The ESP32-C3 has 22 GPIO pins. The Super Mini exposes **13 GPIO pins** on its header (GPIO0–10, GPIO20, GPIO21). GPIO12–17 are used internally for SPI flash. GPIO18 and GPIO19 are USB D−/D+.

### Header Layout (USB-C facing up)

```
LEFT SIDE              RIGHT SIDE
──────────────         ──────────────
GND                    3.3V
GPIO3  (A3)            GPIO8  (LED, active-LOW)
GPIO2  (A2)            GPIO9  (BOOT button, active-LOW)
GPIO1  (A1)            GPIO7
GPIO0  (A0)            GPIO6
GPIO10                 GPIO5  (A5 — ADC broken, see Quirks)
GND                    GPIO4  (A4)
5V                     GPIO21 (TX)
                       GPIO20 (RX)
```

### Full GPIO Function Table

| GPIO | ADC | RTC Wake | PWM | UART | JTAG | Strapping | Notes |
|------|-----|----------|-----|------|------|-----------|-------|
| **GPIO0** | ADC1_CH0 | ✅ | ✅ | — | — | — | Fully safe for general use |
| **GPIO1** | ADC1_CH1 | ✅ | ✅ | — | — | — | Fully safe for general use |
| **GPIO2** | ADC1_CH2 | ✅ | ✅ | — | — | ⚠️ | Must be HIGH at boot; external 10kΩ pull-up on board |
| **GPIO3** | ADC1_CH3 | ✅ | ✅ | — | — | — | Fully safe for general use |
| **GPIO4** | ADC1_CH4 | ✅ | ✅ | — | JTAG TMS | — | JTAG when debugging |
| **GPIO5** | ADC2_CH0 | ✅ | ✅ | — | JTAG TDI | — | **ADC2 broken (SoC errata) — digital use only** |
| **GPIO6** | — | — | ✅ | — | JTAG TCK | — | SPI SCK (IOMUX default); JTAG when debugging |
| **GPIO7** | — | — | ✅ | — | JTAG TDO | — | SPI MOSI (IOMUX default); JTAG when debugging |
| **GPIO8** | — | — | ✅ | — | — | ⚠️ | **Onboard blue LED (active-LOW)**; strapping pin |
| **GPIO9** | — | — | ✅ | — | — | ⚠️ | **BOOT button (active-LOW)**; strapping pin; internal+external pull-up |
| **GPIO10** | — | — | ✅ | — | — | — | Fully safe for general use |
| **GPIO18** | — | — | — | — | — | — | **USB D−** — not on header; do not use as GPIO while USB active |
| **GPIO19** | — | — | — | — | — | — | **USB D+** — not on header; do not use as GPIO while USB active |
| **GPIO20** | — | — | ✅ | UART0 RX | — | — | Default hardware UART RX; boot messages during startup |
| **GPIO21** | — | — | ✅ | UART0 TX | — | — | Default hardware UART TX; boot messages during startup |
| **GPIO12–17** | — | — | — | — | — | — | **Internal SPI flash — never connect externally** |

### GPIO Safety Classification

| Category | GPIOs | Recommendation |
|----------|-------|----------------|
| Fully safe | 0, 1, 3, 10 | Use freely for any purpose |
| Use with caution | 2, 8, 9 | Strapping pins — external pull-downs can prevent boot |
| JTAG (fine for GPIO) | 4, 5, 6, 7 | Available as normal GPIO when JTAG not in use |
| Boot console UART | 20, 21 | Usable but emit serial data during boot |
| Reserved (internal flash) | 12–17 | Never connect externally |
| USB (not on header) | 18, 19 | Cannot use as GPIO while USB CDC active |

---

## 7. Strapping Pins

Strapping pins are sampled once at boot to determine startup behavior. They have pull-up resistors and should not be externally pulled low during power-on or reset.

| Pin | Normal State | Effect if LOW at Boot | Notes |
|-----|-------------|----------------------|-------|
| **GPIO2** | HIGH (external 10kΩ pull-up) | Combined with GPIO9 LOW: enters serial download mode | Avoid external pull-down |
| **GPIO8** | HIGH (external 10kΩ pull-up) | GPIO8=LOW + GPIO9=LOW = **INVALID STATE** | Also the LED pin; avoid pull-down |
| **GPIO9** | HIGH (internal + external pull-up) | Enters serial download mode (bootloader) | BOOT button pulls this LOW |

### Strapping State Reference

| GPIO2 | GPIO8 | GPIO9 | Boot Mode |
|-------|-------|-------|-----------|
| HIGH | HIGH | HIGH | Normal boot from flash ✅ |
| HIGH | HIGH | LOW | Serial download mode (firmware flash) |
| LOW | HIGH | HIGH | Serial download mode (firmware flash) |
| any | LOW | LOW | **INVALID — unpredictable behavior** ❌ |

### Rules

1. Do not pull GPIO8 LOW if GPIO9 is also LOW — this is an invalid strapping combination.
2. External I²C pull-up resistors on GPIO8/GPIO9 are fine (they pull UP, not down).
3. A button to GND on GPIO9 is safe during normal sketch execution; dangerous only if held during reset/power-on.

### Manual Bootloader Entry (Download Mode)

```
1. Press and HOLD the BOOT button (pulls GPIO9 LOW)
2. Press and RELEASE the RESET button
3. Release the BOOT button
4. Board is now in download mode — upload firmware normally
```

---

## 8. Built-in LED (GPIO8)

| Property | Value |
|----------|-------|
| GPIO | **GPIO8** |
| Color | Blue |
| Logic | **Active LOW (inverted)** — `LOW` = ON, `HIGH` = OFF |
| Type | Standard SMD LED (not WS2812 RGB) |
| `LED_BUILTIN` | Often undefined or wrong in board packages — **always `#define LED_BUILTIN 8`** |

> Coming from standard Arduino boards, this trips everyone up. `digitalWrite(8, LOW)` turns it ON. `digitalWrite(8, HIGH)` turns it OFF.

```cpp
#define LED_BUILTIN 8

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // Start OFF (inverted logic)
}

void loop() {
  digitalWrite(LED_BUILTIN, LOW);   // ON
  delay(500);
  digitalWrite(LED_BUILTIN, HIGH);  // OFF
  delay(500);
}
```

---

## 9. BOOT Button (GPIO9)

| Property | Value |
|----------|-------|
| GPIO | **GPIO9** |
| Logic | **Active LOW** — button connects GPIO9 to GND |
| Normal state | HIGH (internal pull-up + external 10kΩ pull-up) |
| Pressed state | LOW |
| Primary use | Enter bootloader when held during reset |
| Secondary use | General-purpose input button in running sketch |

```cpp
#define BOOT_BUTTON_PIN 9

void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);  // Pull-up already present; redundant but safe
}

void loop() {
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    Serial.println("Button pressed");
    delay(50);  // Simple debounce
  }
}
```

---

## 10. USB

| Property | Value |
|----------|-------|
| Connector | USB-C |
| USB standard | USB 1.1 Full-Speed (12 Mbps) |
| USB pins | GPIO18 = D−, GPIO19 = D+ (not on header) |
| USB peripheral | Native USB-Serial/JTAG — **no external CH340 or CP2102** |
| USB functions | CDC serial (virtual COM port) + JTAG debugging |
| VBUS | Connected to 5V header pin |
| Driver required | Usually none on Linux/macOS; Windows auto-installs |

### USB CDC Serial

The ESP32-C3 Super Mini uses the chip's built-in USB-Serial/JTAG peripheral (no bridge chip). This means:

- Board appears as a CDC virtual COM port: `COM3` (Windows), `/dev/ttyACM0` (Linux), `/dev/cu.usbmodem...` (macOS)
- **"USB CDC on Boot" must be ENABLED** in Arduino IDE Tools menu for `Serial.print()` to appear on USB
- When disabled, `Serial` routes to hardware UART pins (GPIO20/GPIO21)
- Baud rate argument in `Serial.begin(115200)` is technically ignored by USB CDC (it's a virtual port), but must still be called
- Use a **data-capable** USB-C cable — charging-only cables have no data lines and cannot program or communicate

### USB JTAG Debugging

GPIO18 and GPIO19 also serve JTAG. `openocd` can connect directly over USB without an external debug probe. Reconfiguring GPIO18/GPIO19 as regular GPIOs in firmware disables USB — recovery requires manual bootloader entry.

---

## 11. UART / Serial

### USB CDC vs Hardware UART

| Feature | USB CDC Serial | Hardware UART0 |
|---------|---------------|----------------|
| Pins | GPIO18/19 (internal) | GPIO20 (RX), GPIO21 (TX) |
| Arduino object | `Serial` | `Serial0` |
| Requires USB cable | Yes | No (use external USB-serial adapter) |
| Baud rate | Irrelevant (USB CDC) | Must match at both ends |
| Active during boot | Only if "USB CDC on Boot" enabled | Yes — boot messages go here |

### UART Controllers

| UART | Default TX | Default RX | Notes |
|------|-----------|-----------|-------|
| UART0 | GPIO21 | GPIO20 | Console/bootloader; `Serial0` in Arduino |
| UART1 | Configurable | Configurable | No default IOMUX pins; assign via GPIO matrix |

### Arduino Serial Configuration

```cpp
// USB CDC serial (most common — requires "USB CDC on Boot: Enabled")
Serial.begin(115200);

// Hardware UART0 on GPIO20/GPIO21
Serial0.begin(115200);

// Hardware UART1 on custom pins (assign any available GPIOs)
Serial1.begin(115200, SERIAL_8N1, /*RX=*/3, /*TX=*/10);
```

---

## 12. SPI

### SPI Controllers

| Controller | Use | Available? |
|------------|-----|------------|
| SPI0 | Internal flash (read) | No |
| SPI1 | Internal flash (write) | No |
| SPI2 (FSPI) | General purpose | **Yes — the only user SPI bus** |

### Default SPI2 Pins (Conflicting Community Documentation)

| Source | MISO | MOSI | SCK | CS |
|--------|------|------|-----|-----|
| Espressif IOMUX (official) | GPIO2 | GPIO7 | GPIO6 | — |
| Arduino ESP32 core (C3 Dev) | GPIO2 | GPIO7 | GPIO6 | GPIO10 |
| `nologo_esp32c3_super_mini` board def | GPIO5 | GPIO6 | GPIO4 | GPIO7 |
| Common community examples | GPIO6 | GPIO7 | GPIO10 | GPIO5 |

> **Recommendation: always explicitly define SPI pins.** The GPIO matrix allows any pin for SPI — just pick conflict-free pins and declare them explicitly.

```cpp
#include <SPI.h>

#define PIN_SCK   6
#define PIN_MISO  2
#define PIN_MOSI  7
#define PIN_CS    10

void setup() {
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
}
```

**Maximum SPI frequency:** 80 MHz via IO MUX path; ~40 MHz recommended via GPIO matrix path.

---

## 13. I²C

### I²C Controller

One hardware I²C controller, assignable to any pins via the GPIO matrix.

| Mode | Speed |
|------|-------|
| Standard | 100 kbit/s |
| Fast | 400 kbit/s |
| Fast-Plus | 1 Mbit/s |

### Default vs Recommended Pins

The "default" I²C pins in most board definitions are GPIO8 (SDA) and GPIO9 (SCL) — but these are the LED and BOOT button pins and both are strapping pins. External pull-up resistors are fine (they pull UP), but avoid using these pins for I²C devices that might glitch LOW during power-on.

**Recommended safe I²C pins:**

| Role | GPIO | Why safe |
|------|------|----------|
| SDA | 0, 1, 3, or 10 | No strapping conflicts |
| SCL | 0, 1, 3, or 10 | No strapping conflicts |

```cpp
#include <Wire.h>

#define SDA_PIN 0
#define SCL_PIN 1

void setup() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);  // 400 kHz
}
```

---

## 14. PWM

### LEDC Controller

| Property | Value |
|----------|-------|
| Channels | 6 independent channels |
| Resolution | Up to 14-bit (0–16383) |
| Typical resolution | 8-bit (0–255) or 10-bit (0–1023) |
| Frequency range | ~1 Hz to ~40 MHz (resolution-dependent) |
| PWM-capable pins | All 13 exposed GPIOs |

```cpp
// Simple — analogWrite() (8-bit)
analogWrite(3, 128);  // ~50% duty on GPIO3

// Full control — ledcWrite()
#define PWM_PIN     3
#define PWM_CHANNEL 0
#define PWM_FREQ    5000
#define PWM_RES     8

void setup() {
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(PWM_PIN, PWM_CHANNEL);
}

void loop() {
  for (int duty = 0; duty <= 255; duty++) {
    ledcWrite(PWM_CHANNEL, duty);
    delay(10);
  }
}
```

---

## 15. ADC

### Specifications

| Property | Value |
|----------|-------|
| ADC units | ADC1 (GPIO0–GPIO4), ADC2 (GPIO5) |
| Resolution | 12-bit (0–4095) |
| Input range | 0 – 3.3V |
| Calibration | ADC1 (GPIO0–4) factory-calibrated |
| ADC2 | **Broken by SoC errata — do not use** |

### Channel Map

| GPIO | ADC | Safe with WiFi? | Notes |
|------|-----|-----------------|-------|
| GPIO0 | ADC1_CH0 | ✅ | Factory calibrated |
| GPIO1 | ADC1_CH1 | ✅ | Factory calibrated |
| GPIO2 | ADC1_CH2 | ✅ | Factory calibrated; strapping pin |
| GPIO3 | ADC1_CH3 | ✅ | Factory calibrated |
| GPIO4 | ADC1_CH4 | ✅ | Factory calibrated; JTAG TMS |
| GPIO5 | ADC2_CH0 | ❌ | **BROKEN — SoC errata, all silicon revisions — digital use only** |

### ADC2 Errata

Espressif's errata document explicitly states: "The digital controller of SAR ADC2 does not work correctly in any revision of the ESP32-C3 (v0.0 through v1.1). Use SAR ADC1." — Treat GPIO5 as digital-only.

### Known ADC Limitations

1. **Voltage ceiling:** Readings plateau at ~2870 mV even when input is 3.3V (hardware nonlinearity).
2. **Noise:** More noise than dedicated ADC ICs; for precision use an external ADC (e.g., ADS1115).
3. **WiFi interference:** Even ADC1 can show noise when WiFi is transmitting — read between transmissions for best accuracy.
4. **No DAC:** The ESP32-C3 has no built-in DAC (unlike some other ESP32 variants).

```cpp
void setup() {
  Serial.begin(115200);
  analogReadResolution(12);  // 12-bit; this is the default
}

void loop() {
  int raw = analogRead(0);  // GPIO0 = ADC1_CH0
  float voltage = raw * 3.3f / 4095.0f;
  Serial.printf("Raw: %d  Voltage: %.3f V\n", raw, voltage);
  delay(500);
}
```

---

## 16. Programming

### Arduino IDE Setup

**Step 1 — Add board package URL:**

Go to **File → Preferences → Additional boards manager URLs**, add:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

**Step 2 — Install board support:**

**Tools → Board → Boards Manager** → search "esp32" → install **"esp32 by Espressif Systems"** (v3.x or later).

**Step 3 — Select board:**

| Board Name | When to Use |
|------------|-------------|
| **Nologo ESP32C3 Super Mini** | Best choice — correct `LED_BUILTIN`=8; USB CDC on Boot enabled by default (available in esp32 core v3.x+) |
| **ESP32C3 Dev Module** | Fallback if above not available; hardcode `#define LED_BUILTIN 8` |

**Step 4 — Tools menu settings:**

| Setting | Recommended Value |
|---------|------------------|
| USB CDC On Boot | **Enabled** — required for `Serial` on USB port |
| Upload Speed | 921600 |
| Flash Mode | QIO |
| Flash Size | 4MB (32Mb) |
| Partition Scheme | Default 4MB with spiffs |
| CPU Frequency | 160 MHz |

### PlatformIO Setup

```ini
[env:esp32-c3-super-mini]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
board_build.mcu = esp32c3
board_build.f_cpu = 160000000L
board_build.flash_size = 4MB
board_build.flash_mode = qio
board_upload.flash_size = 4MB
build_flags =
    -D ARDUINO_USB_MODE=1
    -D ARDUINO_USB_CDC_ON_BOOT=1
    -D LED_BUILTIN=8
```

### esptool Manual Flashing

```bash
# Erase flash
esptool.py --chip esp32c3 --port /dev/ttyACM0 erase_flash

# Flash compiled binary
esptool.py \
  --chip esp32c3 \
  --port /dev/ttyACM0 \
  --baud 921600 \
  write_flash \
  --flash_mode qio \
  --flash_size 4MB \
  0x0     bootloader.bin \
  0x8000  partitions.bin \
  0xe000  boot_app0.bin \
  0x10000 firmware.bin
```

---

## 17. Blynk Edgent Specifics

### Board Definition

The ESP32-C3 Super Mini is not a predefined board in Blynk Edgent's `Settings.h`. Use the custom board section (leave all `#define USE_xxx_BOARD` lines commented out).

### Settings.h Configuration

```cpp
// Leave all USE_xxx_BOARD defines commented out, then add:
#define BOARD_BUTTON_PIN        9     // GPIO9 = BOOT button
#define BOARD_BUTTON_ACTIVE_LOW true  // Button pulls GPIO9 LOW

#define BOARD_LED_PIN           8     // GPIO8 = onboard blue LED
#define BOARD_LED_INVERSE       true  // LED is active-LOW (inverted)
// Do NOT define BOARD_LED_PIN_R/G/B — no RGB LED on this board
```

### Main Sketch Header

```cpp
#define BLYNK_TEMPLATE_ID    "TMPLxxxxxxxxxx"  // From Blynk Console
#define BLYNK_TEMPLATE_NAME  "My Device"
#define BLYNK_FIRMWARE_VERSION "0.1.0"

#define BLYNK_PRINT Serial
// Do NOT define BLYNK_AUTH_TOKEN — Edgent manages it automatically

#include "BlynkEdgent.h"

void setup() {
  Serial.begin(115200);
  delay(100);
  BlynkEdgent.begin();
}

void loop() {
  BlynkEdgent.run();
}
```

### Edgent LED Behavior (with `BOARD_LED_INVERSE = true`)

| LED Pattern | Meaning |
|-------------|---------|
| Slow blink | Connecting to WiFi |
| Fast blink | SmartConfig provisioning mode (waiting for app) |
| Solid ON | Connected to Blynk Cloud |

### Factory Reset via BOOT Button

Hold GPIO9 (BOOT button) for **5 seconds** while the sketch is running:
- LED starts flashing at ~3 s to warn
- At 5 s: stored WiFi credentials and auth token are erased
- Board restarts in provisioning mode

> The factory reset only works when the **sketch is already running**. If the button is held during reset/power-on, it enters download mode instead — a completely different behavior.

### Edgent File Structure

When using Blynk Edgent, the sketch folder must contain all the Edgent support files (total ~10 files). The correct workflow:

1. Open **File → Examples → Blynk → Blynk.Edgent → Edgent_ESP32** in Arduino IDE
2. Copy that entire sketch folder to your project location
3. Replace the `.ino` file with your own code
4. Keep all the support files (`BlynkEdgent.h`, `OTA.h`, `ConfigMode.h`, etc.) alongside your `.ino`

---

## 18. Known Quirks and Gotchas

### 1. LED Inverted Logic
`digitalWrite(8, LOW)` = ON. `digitalWrite(8, HIGH)` = OFF. This is opposite to virtually all other Arduino boards.

### 2. `LED_BUILTIN` Not Defined
Always add `#define LED_BUILTIN 8` at the top of any sketch.

### 3. ADC2 / GPIO5 is Broken (All Silicon Revisions)
Espressif's errata confirms GPIO5 (ADC2_CH0) is non-functional for analog reads in every ESP32-C3 revision. Use only GPIO0–GPIO4 for `analogRead()`.

### 4. ADC Voltage Ceiling at ~2870 mV
Even on working ADC1 pins, readings plateau well below 3.3V. Apply calibration offsets if accurate voltage measurement is needed.

### 5. WiFi TX Power May Exceed LDO Capacity
Early-batch boards have a 250 mA LDO but WiFi at full power draws ~276 mA, causing resets. Fix:
```cpp
WiFi.setTxPower(WIFI_POWER_8_5dBm);
```

### 6. Antenna Clearance Issue (2024 Batches)
Crystal placed 0.3 mm from antenna instead of required ≥1.0 mm. Causes WiFi degradation. Same fix: reduce TX power, or replace the board.

### 7. Strapping Pin Conflicts with External Circuits
Any external circuit that can pull GPIO2, GPIO8, or GPIO9 LOW during power-on will interfere with boot. Design external circuits around these pins carefully.

### 8. SPI Default Pin Confusion
Community documentation is inconsistent. **Always use `SPI.begin(SCK, MISO, MOSI, CS)`** — never rely on `SPI.begin()` defaults.

### 9. I²C Defaults on Strapping Pins
`Wire.begin()` without arguments maps to GPIO8/GPIO9. Always use `Wire.begin(SDA_PIN, SCL_PIN)` with safe pins (GPIO0, GPIO1, GPIO3, or GPIO10).

### 10. Deep Sleep Current Higher Than Datasheet
The 43 µA figure requires removing the power LED. With it present, expect ~600 µA.

### 11. USB-C Cable Must Be Data-Capable
Charging-only cables (no data lines) will not allow programming or serial communication.

### 12. `nologo_esp32c3_super_mini` Board Requires esp32 Core v3.x+
If it doesn't appear in your Boards Manager, update the esp32 package.

### 13. `ArduinoBLE` Library Incompatible
The `ArduinoBLE` library (Arduino's official BLE library) causes a core panic on this chip. Use Espressif's bundled `BLEDevice.h` or the **NimBLE** library instead.

### 14. No PSRAM, No DAC, No Classic Bluetooth
Common surprises for developers migrating from other ESP32 variants.

### 15. GPIO4–GPIO7 Claimed by JTAG During Debugging
When JTAG debugging is active via USB, GPIO4/5/6/7 are not available as GPIO.

### 16. Boot Messages on GPIO20/GPIO21
The bootloader sends serial data on UART0 (GPIO20/GPIO21) before your sketch starts. Devices connected to these pins may behave unexpectedly during startup.

### 17. "USB CDC on Boot" Is the #1 Support Question
If `Serial.print()` produces no output, check **Tools → USB CDC on Boot → Enabled** in Arduino IDE.

### 18. Using GPIO6 and GPIO7 for Relay Control
GPIO6 and GPIO7 are the JTAG TCK and TDO pins respectively. They work fine as regular GPIO for relay control during normal sketch execution — JTAG is only active when you have a debugger connected. No conflict in production use.

---

## 19. Sample Arduino Sketches

### 19.1 Blink (LED on GPIO8 — Inverted Logic)

```cpp
#define LED_BUILTIN 8

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-C3 Super Mini Blink");
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // OFF
}

void loop() {
  digitalWrite(LED_BUILTIN, LOW);   // ON
  delay(500);
  digitalWrite(LED_BUILTIN, HIGH);  // OFF
  delay(500);
}
```

### 19.2 Button Read with Debounce

```cpp
#define BUTTON_PIN  9   // BOOT button, active-LOW
#define LED_PIN     8   // Active-LOW

unsigned long lastDebounceTime = 0;
bool lastButtonState   = HIGH;
bool currentButtonState = HIGH;
bool ledState = HIGH;

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, ledState);
}

void loop() {
  int reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonState)
    lastDebounceTime = millis();

  if ((millis() - lastDebounceTime) > 50) {
    if (reading != currentButtonState) {
      currentButtonState = reading;
      if (currentButtonState == LOW) {
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState);
        Serial.println("Button pressed — LED toggled");
      }
    }
  }
  lastButtonState = reading;
}
```

### 19.3 Analog Read (ADC1 only — GPIO0 through GPIO4)

```cpp
// IMPORTANT: Only GPIO0–GPIO4 (ADC1). GPIO5 (ADC2) is broken by SoC errata.

void setup() {
  Serial.begin(115200);
  delay(1000);
  analogReadResolution(12);
}

void loop() {
  int raw = analogRead(0);  // GPIO0 = ADC1_CH0
  float voltage = raw * 3.3f / 4095.0f;
  Serial.printf("Raw: %d  Voltage: %.3f V\n", raw, voltage);
  delay(500);
}
```

### 19.4 WiFi Station Mode

```cpp
#include <WiFi.h>

const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Uncomment if you have an early-batch board with weak LDO:
  // WiFi.setTxPower(WIFI_POWER_8_5dBm);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Reconnecting...");
    WiFi.reconnect();
    delay(5000);
  }
}
```

### 19.5 BLE Server (use BLEDevice.h — NOT ArduinoBLE)

```cpp
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

class MyCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s)    { Serial.println("Client connected"); }
  void onDisconnect(BLEServer* s) {
    Serial.println("Client disconnected");
    BLEDevice::getAdvertising()->start();
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);

  BLEDevice::init("ESP32-C3-SuperMini");
  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic* pChar = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
  );
  pChar->setValue("Hello");
  pService->start();

  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(SERVICE_UUID);
  pAdv->setScanResponse(true);
  pAdv->start();

  Serial.println("BLE advertising. Scan with nRF Connect app.");
}

void loop() {
  delay(2000);
}
```

### 19.6 BLE Scanner

```cpp
#include <BLEDevice.h>
#include <BLEScan.h>

BLEScan* pBLEScan;

class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice device) {
    Serial.printf("  %s | RSSI: %d dBm\n",
      device.toString().c_str(), device.getRSSI());
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new ScanCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
}

void loop() {
  Serial.println("Scanning...");
  BLEScanResults* results = pBLEScan->start(5, false);
  Serial.printf("Found %d device(s).\n", results->getCount());
  pBLEScan->clearResults();
  delay(2000);
}
```

### 19.7 Deep Sleep — Timer Wake-Up

```cpp
#define uS_TO_S   1000000ULL
#define SLEEP_S   30
#define LED_PIN   8

RTC_DATA_ATTR int bootCount = 0;  // Persists across deep sleep

void setup() {
  Serial.begin(115200);
  delay(500);
  bootCount++;
  Serial.printf("Boot #%d  Wake: %d\n", bootCount, esp_sleep_get_wakeup_cause());

  // Flash LED to signal wakeup
  pinMode(LED_PIN, OUTPUT);
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, LOW);  delay(200);
    digitalWrite(LED_PIN, HIGH); delay(200);
  }

  // ... do work here ...

  Serial.printf("Sleeping %d s\n", SLEEP_S);
  Serial.flush();
  esp_sleep_enable_timer_wakeup(SLEEP_S * uS_TO_S);
  esp_deep_sleep_start();
}

void loop() {}
```

### 19.8 Deep Sleep — GPIO Wake-Up

```cpp
// IMPORTANT: On ESP32-C3, use esp_deep_sleep_enable_gpio_wakeup()
// NOT esp_sleep_enable_ext0_wakeup() — that is NOT supported on ESP32-C3
// Only GPIO0–GPIO5 (RTC domain) can wake from deep sleep

#include "esp_sleep.h"

#define WAKEUP_PIN 3   // Must be GPIO0–GPIO5
#define LED_PIN    8

void setup() {
  Serial.begin(115200);
  delay(500);

  esp_deep_sleep_enable_gpio_wakeup(
    (1ULL << WAKEUP_PIN),
    ESP_GPIO_WAKEUP_GPIO_LOW  // Wake when pin goes LOW
  );

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  delay(500);
  digitalWrite(LED_PIN, HIGH);

  Serial.println("Sleeping. Bring GPIO3 LOW to wake.");
  Serial.flush();
  esp_deep_sleep_start();
}

void loop() {}
```

### 19.9 UART Communication

```cpp
void setup() {
  Serial.begin(115200);   // USB CDC
  delay(1000);
  Serial.println("USB CDC ready");

  // Hardware UART0 on GPIO20(RX) / GPIO21(TX)
  Serial0.begin(9600, SERIAL_8N1, 20, 21);
  Serial.println("UART0 ready at 9600 baud on GPIO20/21");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    Serial0.print(c);
  }
  if (Serial0.available()) {
    Serial.print((char)Serial0.read());
  }
}
```

### 19.10 Relay Control on GPIO6 and GPIO7

```cpp
// Simple relay control on the two relay GPIOs used by this project.
// HIGH = relay ON, LOW = relay OFF.
// GPIO6 and GPIO7 are JTAG TCK/TDO but work as normal GPIO when
// no JTAG debugger is connected.

#define RELAY_0_PIN  6
#define RELAY_1_PIN  7

void setup() {
  pinMode(RELAY_0_PIN, OUTPUT);
  pinMode(RELAY_1_PIN, OUTPUT);
  digitalWrite(RELAY_0_PIN, LOW);  // Ensure relays start OFF
  digitalWrite(RELAY_1_PIN, LOW);
}

void loop() {
  digitalWrite(RELAY_0_PIN, HIGH); delay(2000);  // ON 2 s
  digitalWrite(RELAY_0_PIN, LOW);  delay(1000);  // OFF 1 s
  digitalWrite(RELAY_1_PIN, HIGH); delay(2000);
  digitalWrite(RELAY_1_PIN, LOW);  delay(1000);
}
```

---

## 20. Quick Reference

### Critical Pin Summary

| Function | GPIO | Notes |
|----------|------|-------|
| Onboard blue LED | **8** | Active LOW: `LOW`=on, `HIGH`=off |
| BOOT button | **9** | Active LOW; internal+external pull-up |
| Hardware UART RX | **20** | UART0; boot messages during startup |
| Hardware UART TX | **21** | UART0; boot messages during startup |
| USB D− | 18 | Not on header; internal only |
| USB D+ | 19 | Not on header; internal only |
| ADC safe pins | **0, 1, 2, 3, 4** | ADC1; factory calibrated |
| ADC broken | **5** | ADC2; SoC errata — digital use only |
| RTC wake-capable | **0–5** | Only these can wake from deep sleep |
| JTAG TMS | 4 | Claimed by JTAG when debugging |
| JTAG TDI | 5 | Claimed by JTAG when debugging |
| JTAG TCK | 6 | Claimed by JTAG when debugging |
| JTAG TDO | 7 | Claimed by JTAG when debugging |
| Strapping pins | **2, 8, 9** | Must not be pulled LOW at boot |
| Fully safe GPIOs | **0, 1, 3, 10** | No boot/JTAG/USB concerns |
| Relay A (this project) | **6** | HIGH=on, LOW=off |
| Relay B (this project) | **7** | HIGH=on, LOW=off |

### Arduino IDE Quick Setup Checklist

```
1. Add board URL: https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
2. Install: esp32 by Espressif Systems (v3.x+)
3. Board: Nologo ESP32C3 Super Mini  (or ESP32C3 Dev Module)
4. USB CDC on Boot: Enabled
5. Flash Mode: QIO
6. Flash Size: 4MB
7. Upload Speed: 921600
8. Always #define LED_BUILTIN 8 in your sketch
```

---

## Sources

- [Random Nerd Tutorials — Getting Started with ESP32-C3 Super Mini](https://randomnerdtutorials.com/getting-started-esp32-c3-super-mini/)
- [Last Minute Engineers — ESP32-C3 Super Mini Pinout Reference](https://lastminuteengineers.com/esp32-c3-super-mini-pinout-reference/)
- [Mischianti — ESP32-C3 Super Mini High-Resolution Pinout](https://mischianti.org/esp32-c3-supermini-plus-v2-high-resolution-pinout-datasheet-and-specs/)
- [espboards.dev — ESP32 C3 Super Mini](https://www.espboards.dev/esp32/esp32-c3-super-mini/)
- [done.land — ESP32-C3 Super Mini](https://done.land/components/microcontroller/families/esp/esp32/developmentboards/esp32-c3/c3supermini/)
- [sigmdel.ca — First Look at the Super Mini ESP32-C3](https://sigmdel.ca/michel/ha/esp8266/super_mini_esp32c3_en.html)
- [Studio Pieters — Ultimate Guide to the ESP32-C3 Super Mini Pinout](https://www.studiopieters.nl/esp32-c3-super-mini-pinout/)
- [GitHub — sidharthmohannair/Tutorial-ESP32-C3-Super-Mini](https://github.com/sidharthmohannair/Tutorial-ESP32-C3-Super-Mini)
- [Espressif — ESP32-C3 Series Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf)
- [Espressif — ESP32-C3 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf)
- [Espressif IDF — GPIO & RTC GPIO (ESP32-C3)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/peripherals/gpio.html)
- [Espressif IDF — ADC (ESP32-C3)](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32c3/api-reference/peripherals/adc.html)
- [Espressif IDF — Sleep Modes (ESP32-C3)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/system/sleep_modes.html)
- [Espressif esptool — Boot Mode Selection (ESP32-C3)](https://docs.espressif.com/projects/esptool/en/latest/esp32c3/advanced-topics/boot-mode-selection.html)
- [Blynk — WiFi Provisioning (Edgent)](https://docs.blynk.io/en/getting-started/activating-devices/blynk-edgent-wifi-provisioning)
- [Blynk — OTA Firmware Updates (Blynk.Air)](https://docs.blynk.io/en/blynk.edgent/updating-devices-firmwares-ota)
- [GitHub — Blynk Edgent ESP32 Source](https://github.com/Blynk-Technologies/blynk-library/blob/master/examples/Blynk.Edgent/Edgent_ESP32/BlynkEdgent.h)

*Compiled June 2026. Covers ESP32-C3 silicon through v1.1, ESP32 Arduino core v3.x, ESP-IDF v5.x.*
