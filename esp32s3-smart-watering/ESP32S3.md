# UICPAL ESP32-S3-CAM N16R8 — Board Reference

> Scope: the specific board used by this project — a **UICPAL ESP32-S3-CAM N16R8
> RE1.3** (40-pin, dual USB-C, on-board camera connector + microSD slot), built
> around the **ESP32-S3-WROOM-1** module. GPIO assignments below are read directly
> from the board's printed pin-definition diagram and cross-checked against the
> Espressif ESP32-S3 datasheet. Where this board differs from a stock
> ESP32-S3-DevKitC-1, it is called out.

---

## 1. Board at a glance

| Property            | Value                                                        |
|---------------------|--------------------------------------------------------------|
| Module              | ESP32-S3-WROOM-1                                             |
| Marking             | `ESP32-S3-N16R8`                                             |
| Flash               | **16 MB** quad SPI (the "N16")                               |
| PSRAM               | **8 MB octal/OPI** (the "R8") — on GPIO35–37                  |
| CPU                 | Dual-core Xtensa LX7 @ up to 240 MHz, hardware FPU           |
| SRAM                | 512 KB on-chip                                               |
| WiFi                | 802.11 b/g/n, 2.4 GHz only                                  |
| Bluetooth           | BLE 5.0 (no Classic)                                         |
| USB                 | 2× USB-C: one native USB-OTG (GPIO19/20), one UART bridge    |
| Buttons             | **BOOT = GPIO0**, **RST** (reset, not a GPIO)                |
| Onboard RGB LED     | **WS2812 addressable, GPIO48**                              |
| Extras              | OV-series camera (DVP) connector, microSD slot              |
| Header              | 40 pins, soldered                                           |

> "N16R8" is the key string: **16 MB flash, 8 MB OPI PSRAM**. The R8 PSRAM
> dictates which GPIOs are off-limits (see §3).

---

## 2. Verified pinout (from the board diagram)

Right-hand header (top → bottom), with the silkscreen's alternate-function labels:

| GPIO  | Alt / label        | Notes                                            |
|-------|--------------------|--------------------------------------------------|
| GPIO43| U0TXD              | UART0 TX — boot log; "LED TX" activity            |
| GPIO44| U0RXD              | UART0 RX; "LED RX" activity                        |
| GPIO1 | ADC1_CH0           | safe GPIO / ADC                                   |
| GPIO2 | ADC1_CH1 / LED ON  | safe GPIO / ADC                                   |
| GPIO42| MTMS               | JTAG (free as GPIO when no debugger)             |
| GPIO41| MTDI               | JTAG                                             |
| GPIO40| MTDO / SD_DATA     | JTAG / SD slot                                   |
| GPIO39| **MTCK / SD_CLK**  | **← relay 1 in this project**                    |
| GPIO38| **SD_CMD**         | **← relay 0 in this project**                    |
| GPIO37| PSRAM              | **reserved — octal PSRAM, do not use**           |
| GPIO36| PSRAM              | **reserved — octal PSRAM, do not use**           |
| GPIO35| PSRAM              | **reserved — octal PSRAM, do not use**           |
| GPIO0 | **Boot**           | BOOT button, strapping, active-low               |
| GPIO45| VSPI               | strapping pin (see §4)                            |
| GPIO48| **WS2812**         | **onboard RGB status LED**                        |
| GPIO47|                    | safe GPIO                                         |
| GPIO21|                    | safe GPIO                                         |
| GPIO20| USB_D− / ADC2_CH9  | native USB; avoid as GPIO while USB active        |
| GPIO19| USB_D+ / ADC2_CH8  | native USB; avoid as GPIO while USB active        |

Left-hand header is dominated by the **camera (DVP) bus** (CAM_* labels) and the
ADC1/ADC2 analog channels (T1–T14 touch labels). Those pins are claimed by the
camera connector on this variant — only use them if you are not wiring the camera.

---

## 3. Octal PSRAM reserves GPIO35–37

The **R8** octal PSRAM is wired to **GPIO35, GPIO36, GPIO37** (plus the SPI flash
on GPIO26–32 internally). These three are **not available** for application use —
attempting to drive them crashes or corrupts PSRAM access.

This is the single most important pin constraint on the board, and the reason this
project's relays sit on **GPIO38 / GPIO39** — the first safe, non-strapping pins
immediately past the PSRAM block.

In the Arduino IDE, `PSRAM` selects whether the firmware *initialises* the 8 MB.
This sketch never uses PSRAM, so **`PSRAM: Disabled` is the correct, safe choice** —
it boots cleanly and frees you from matching the exact PSRAM type. Choose
**`OPI PSRAM`** only if you later add code that needs the extra RAM (and note that
setting OPI on a board without OPI PSRAM, or vice-versa, can hang the boot — which
is exactly why "Disabled" is the no-surprises default here).

> The **`OPI PSRAM` menu option only appears when an S3 board is selected.** If you
> don't see it, you're on the wrong board profile (see §8).

---

## 4. Strapping pins (do not pull LOW at boot)

| Pin    | Role                          | Caution                                  |
|--------|-------------------------------|------------------------------------------|
| GPIO0  | Boot mode select (BOOT btn)   | LOW at reset → download mode             |
| GPIO3  | JTAG source / boot config     | leave floating/default                   |
| GPIO45 | VDD_SPI voltage select        | external pull-down can break boot        |
| GPIO46 | Boot config / ROM log         | input-only on some packages             |

Relays on GPIO38/39 and the LED on GPIO48 avoid all of these. If you add more
peripherals, keep external circuitry off the strapping pins during power-on.

---

## 5. The two USB-C ports

This board has **two USB-C connectors**:

- **Native USB (USB-OTG)** — wired to the SoC's GPIO19 (D+) / GPIO20 (D−). This is
  the CDC serial + JTAG port. With **"USB CDC On Boot: Enabled"**, `Serial`
  appears here. This is the port Blynk OTA / normal use does not depend on, but
  it's the most convenient for flashing.
- **UART bridge** — an on-board USB-UART chip on UART0 (GPIO43/44). Always shows
  boot-ROM messages; works even if native USB is reconfigured.

Use a **data-capable** cable. If a sketch reconfigures GPIO19/20, native USB drops
and you recover via manual download mode: **hold BOOT → tap RST → release BOOT**.

---

## 6. Onboard WS2812 RGB LED (GPIO48)

A single addressable WS2812 (NeoPixel) on **GPIO48**. This project uses it as the
Blynk Edgent status indicator, so `Settings.h` defines:

```cpp
#define BOARD_LED_PIN_WS2812  48
```

`Indicator.h` then drives it via the **Adafruit NeoPixel** library (install it
through the Library Manager). Status colours:

| Pattern                 | Meaning                          |
|-------------------------|----------------------------------|
| Blue blink (slow/fast)  | waiting for / running config     |
| Green (Blynk) blink     | connecting to WiFi / cloud       |
| Green slow breathe      | connected & running              |
| Magenta fast blink      | OTA upgrade in progress          |
| Red pattern             | error / disconnected             |

There is **no plain single-colour user LED** to fall back on — the WS2812 is the
only controllable LED. (GPIO43/44 "LED TX/RX" are hard-wired UART activity LEDs.)

---

## 7. BOOT button (GPIO0) & factory reset

`Settings.h` maps the Edgent factory-reset button to **GPIO0** (active-low):

```cpp
#define BOARD_BUTTON_PIN         0
#define BOARD_BUTTON_ACTIVE_LOW  true
```

Hold **BOOT for 5 s while the sketch is running** to erase stored WiFi credentials
and the Blynk auth token; the LED warns at ~3 s. The board then reboots into
provisioning mode.

> The reset only works while the sketch runs. Holding BOOT **during** power-on/RST
> instead enters the ROM serial download mode — a different behaviour.

---

## 8. ESP32-S3 vs the old ESP32-C3 Super Mini (migration notes)

| Aspect            | ESP32-C3 Super Mini       | This ESP32-S3 N16R8 board          |
|-------------------|---------------------------|------------------------------------|
| Core              | 1× RISC-V                 | 2× Xtensa LX7 + FPU                |
| Flash / PSRAM     | 4 MB / none               | 16 MB / 8 MB OPI                   |
| Relay pins        | GPIO6, GPIO7              | **GPIO38, GPIO39**                 |
| BOOT button       | GPIO9                     | **GPIO0**                          |
| Status LED        | plain LED GPIO8 (active-LOW) | **WS2812 RGB GPIO48**           |
| LED driver        | `BOARD_LED_PIN` + inverse | `BOARD_LED_PIN_WS2812` (+ NeoPixel)|
| Reserved pins     | GPIO12–17 (flash)         | **GPIO35–37 (OPI PSRAM)** + flash  |
| Board (Arduino)   | Nologo/ESP32C3 Dev Module | **ESP32S3 Dev Module**             |
| Build path        | `esp32.esp32.esp32c3`     | `esp32.esp32.esp32s3`              |
| TX-power workaround | sometimes needed (weak LDO) | not needed                      |

The application code (`RelayController`, virtual-pin layout, 3-layer failsafe,
uptime, message-budget throttling) is **unchanged** — only the hardware mapping
in `Settings.h` and the relay GPIO array in the `.ino` differ.

---

## 9. Arduino IDE quick setup

```
1. Board:            ESP32S3 Dev Module   ← NOT a C3/C-series board (see below)
2. USB CDC On Boot:  Enabled
3. Flash Size:       16MB (128Mb)
4. Flash Mode:       QIO 80MHz
5. PSRAM:            Disabled            ← this sketch doesn't use PSRAM (OPI optional)
6. Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)   ← OTA-capable
7. CPU Frequency:    240 MHz             ← 160 MHz also fine
8. Upload Speed:     921600
9. Install library:  Adafruit NeoPixel  (for the GPIO48 WS2812)
10. Install library: PubSubClient       (Nick O'Leary — MQTT moisture push, §10)
```

**Wrong-board tell:** if the **CPU Frequency dropdown maxes at 160 MHz** or there is
**no `OPI PSRAM` option**, you still have a C-series board selected (the old
*ESP32C3 Dev Module*). The C3 caps at 160 MHz and has no PSRAM. Re-select
**ESP32S3 Dev Module** and both reappear (240 MHz, OPI PSRAM).

If `Serial.print()` is silent, the cause is almost always **USB CDC On Boot:
Disabled** or a charge-only cable. If upload won't start: **hold BOOT, tap RST,
release BOOT**, then upload once.

---

## 10. Soil-moisture monitoring (V11 + MQTT)

Up to **3 analog moisture sensors** are sampled on **ADC1** and pushed over MQTT to a
local broker (Raspberry Pi), where a `mqtt2prometheus` exporter turns them into
Prometheus metrics. The broker + exporter stack lives in the repo's `monitoring/`
directory; the firmware side is `Moisture.h` + `MqttPublisher.h`.

**Sensor pins** (positional — channel `sN` → fixed GPIO):

| Channel | GPIO  | ADC      | Header                                   |
|---------|-------|----------|------------------------------------------|
| s0      | GPIO4 | ADC1_CH3 | left-hand (camera/ADC) header            |
| s1      | GPIO5 | ADC1_CH4 | left-hand (camera/ADC) header            |
| s2      | GPIO6 | ADC1_CH5 | left-hand (camera/ADC) header            |

> **Why ADC1:** ADC2 is unusable while WiFi is active, and this node is always on
> WiFi. GPIO4/5/6 sit on the camera/ADC header (§2) — free here because this build
> does not wire the camera. Keep the camera unconnected if you use these pins.

**V11 config string** (set from the Blynk app; restored on every connect):

```
<pin1>[,<pin2>[,<pin3>]] ; <device_name> ; <mqtt_host>[:<port>] ; <interval_s>
```

- pin names: 1–3 labels; the **count** picks how many channels are sampled. The names
  are for humans only — the wire format is `s0/s1/s2` by channel, and friendly names
  are mapped in Grafana (one dynamic MQTT/Prom label is spent on the device name).
- `device_name`: charset `[A-Za-z0-9_-]`; becomes the topic segment + Prom `sensor` label.
- `mqtt_host[:port]`: broker IP/host, port optional (default **1883**).
- `interval_s`: publish period, optional (default **60**, clamped 10–3600).
- A malformed string is rejected and echoed back as **`INVALID FORMAT`** (sampling stops).

```
example:  tomato,basil,mint;garden-node1;192.168.1.50:1883;60
publishes: topic  watering/garden-node1/moisture
           payload {"s0":2731,"s1":2540,"s2":2600,"r0":0,"r1":1}   every 60 s
```

**Relay state** rides in the same message: `r0` (GPIO38) and `r1` (GPIO39), `1`=on
`0`=off, mapped to a `relay_on` gauge. Both relays are always included, regardless of
sensor count, and a relay change is published immediately (within ~1 s of the 1 Hz
tick) so a short pump run isn't missed between periodic publishes.

A retained **Last Will** is registered on `watering/<device>/status` (`online`/`offline`)
for future use; today liveness is handled by the exporter's `cache.timeout`.

> **Raw values only.** The device publishes raw 12-bit ADC (0–4095); raw→% calibration
> is done downstream (Grafana / Prometheus rules) so re-calibrating never means
> re-flashing.
