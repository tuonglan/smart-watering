# Fan Controller — ESP32-S3 N16R8

Blynk Edgent firmware for a PWM fan controller with 4-wire stepper-motor oscillation.
WiFi credentials and the auth token are provisioned at runtime via the Blynk app (no
hardcoded secrets).

- **Board:** UICPAL ESP32-S3-CAM N16R8 (ESP32-S3-WROOM-1, 16 MB flash / 8 MB OPI PSRAM)
- **Fan PWM:** GPIO15 — LEDC channel 4, 25 kHz (above audible range)
- **Stepper motor:** GPIO4 / GPIO5 / GPIO6 / GPIO7 — half-step sequence (8 steps)
- **Status LED:** onboard WS2812 RGB on GPIO48 (requires the **Adafruit NeoPixel** library)
- **Template:** `Fan Controller` (fill `BLYNK_TEMPLATE_ID` from your Blynk console)
- **Firmware version:** bump `BLYNK_FIRMWARE_VERSION` before every OTA ship

## Virtual pin layout

### Fan controller (V0–V6)

| Pin | Name             | Direction    | Widget / type        | Notes                                         |
|-----|------------------|--------------|----------------------|-----------------------------------------------|
| V0  | Fan ON/OFF       | app → device | Button 0/1           | turns the fan on or off                       |
| V1  | Fan speed        | app → device | Slider 32–255        | duty cycle (32 = minimum, 255 = full speed)   |
| V2  | Timer ON/OFF     | app → device | Button 0/1           | enables the countdown timer                   |
| V3  | Timer length     | app → device | Slider (minutes)     | how long to run before auto-shutoff           |
| V4  | Timer countdown  | device → app | Display (Integer)    | minutes remaining; read-only                  |
| V5  | Firmware version | device → app | Display (**String**) | written on connect; read-only                 |
| V6  | Uptime           | device → app | Display (**String**) | `2d 03h 14m` format, every 5 min; read-only   |

### Stepper motor oscillation (V10–V15)

| Pin | Name          | Direction      | Widget / type     | Notes                                                 |
|-----|---------------|----------------|-------------------|-------------------------------------------------------|
| V10 | Rotation ON/OFF | app → device | Button 0/1        | starts / stops oscillation                            |
| V11 | Rotation speed  | app → device | Slider (steps/s)  | max ~300 steps/s; hard floor at ~333 steps/s          |
| V12 | Max angle       | app → device | Slider (steps)    | oscillation half-sweep in half-steps                  |
| V13 | Current angle   | device → app | Display (Integer) | updated every 1 s while running; read-only            |
| V14 | Reset angle     | app → device | Button (momentary)| snaps position back to 0 and updates V13              |
| V15 | Angle update    | app → device | Switch 0/1        | 1 = push V13 every 1 s while running, 0 = suppress (saves messages) |

> **V5 and V6 must be String datastreams** in the Blynk console. V0–V4 and V10–V15 are
> Integer datastreams. Using the wrong type causes Blynk to silently drop the value.

> The firmware's `BLYNK_WRITE_DEFAULT()` handler dispatches on pin number: pins 0–6 go
> to `FanController`, pins 10–15 go to `StepperMotor`.

---

## Wiring

### Power

The ESP32-S3 board is powered via its USB-C port (5 V). The fan and stepper motor
typically need a **separate 12 V supply** (or 5 V depending on your motor). Their GND
must be tied to the board's GND.

```
12 V PSU (+) ──── Fan + / Stepper VCC
12 V PSU (−) ──┬─ Fan − / Stepper GND
               └─ ESP32-S3 GND (any GND pin)
```

### Fan (PWM speed control)

The PWM signal on GPIO15 is 3.3 V logic at 25 kHz. Most brushless PC fans (4-wire)
accept 3.3 V PWM directly. For a 2- or 3-wire fan driven through an N-channel MOSFET:

#### 4-wire PWM fan (direct)

```
4-wire Fan
  Pin 1  (GND / Black)   ──── GND
  Pin 2  (12 V / Yellow) ──── 12 V PSU (+)
  Pin 3  (Tach / Green)  ──── (leave unconnected — not used)
  Pin 4  (PWM / Blue)    ──── GPIO15
```

#### 2- or 3-wire fan via MOSFET (e.g. IRLZ44N / 2N7000)

```
                     12 V
                       │
                    Fan (+)
                    Fan (−)
                       │
                    Drain (D)
    GPIO15 ──[100Ω]── Gate (G)
                       │
    GND ────[10kΩ]──── Source (S) ──── GND
```

The 100 Ω series resistor on the gate limits inrush; the 10 kΩ pull-down holds the
gate LOW while the ESP32-S3 is booting (GPIO15 is not a strapping pin and comes up
low-Z, but the pull-down is good practice).

### Stepper motor (28BYJ-48 + ULN2003 driver board)

The 28BYJ-48 is a 5 V unipolar stepper; its ULN2003 driver board takes 4 logic inputs
and handles the current. Wire the driver's IN1–IN4 to GPIO4–7 in the order below — the
half-step sequence in the firmware is written for this mapping.

```
ULN2003 board          ESP32-S3 board
  VCC (5 V)  ────────── 5 V  (USB VBUS pin or separate 5 V)
  GND        ────────── GND
  IN1        ────────── GPIO4   (SM_PIN0)
  IN2        ────────── GPIO5   (SM_PIN1)
  IN3        ────────── GPIO6   (SM_PIN2)
  IN4        ────────── GPIO7   (SM_PIN3)

5-pin connector on the 28BYJ-48 plugs directly into the ULN2003 board's socket.
```

> ⚠️ **Do not drive the 28BYJ-48 directly from the ESP32-S3 GPIO pins.** Each coil draws
> ~200 mA; GPIO pins source at most 40 mA. Always use the ULN2003 (or equivalent) driver
> board between the ESP32 and the motor.

### Nano watchdog (GPIO21)

The firmware sends a `HB\n` heartbeat to an external Arduino Nano guardian every 2 s
(see `nano-watchdog/` in the repo). This is TX-only; wire GPIO21 → Nano RX at 9600 baud.
If you do not have the Nano watchdog, the heartbeat is still emitted harmlessly on GPIO21
(the pin is just toggling TX with nothing listening).

### Complete wiring summary

| GPIO | Function         | Connects to                          |
|------|------------------|--------------------------------------|
| 0    | BOOT button      | onboard button (built-in)            |
| 4    | Stepper IN0      | ULN2003 IN1                          |
| 5    | Stepper IN1      | ULN2003 IN2                          |
| 6    | Stepper IN2      | ULN2003 IN3                          |
| 7    | Stepper IN3      | ULN2003 IN4                          |
| 15   | Fan PWM          | Fan PWM pin / MOSFET gate            |
| 19   | USB D−           | USB-C port (native USB, built-in)    |
| 20   | USB D+           | USB-C port (native USB, built-in)    |
| 21   | Watchdog TX      | Nano guardian RX (9600 baud)         |
| 48   | WS2812 LED       | onboard RGB LED (built-in)           |

---

## Hardware pin choices

### GPIO4–7 for stepper

GPIO4–7 are consecutive general-purpose pins on the 40-pin header, making it easy to
route 4 stepper wires next to each other. They are not strapping pins, not PSRAM lines
(those are GPIO35–37), and not USB (GPIO19/20) — fully free. Moisture ADC uses GPIO4/5/6
in the smart-watering project, but this is a separate firmware on a separate device.

### GPIO15 for PWM

GPIO15 is a clean general-purpose pin: not a strapping pin, not in the PSRAM block, and
physically adjacent to the stepper group (pins 4–7 and 15 are all on the same side of the
module). LEDC channel 4 is used so it doesn't collide with channels 1–3, which
`Indicator.h` reserves for the RGB LED.

### Why LEDC instead of analogWrite

On ESP32, `analogWrite()` is not a standard function. The ESP32 Arduino core provides
`ledcWrite()` instead, backed by the hardware LEDC (LED Control) peripheral. The firmware
uses the core 3.x API (`ledcAttach(pin, freq, bits)` then `ledcWrite(pin, duty)`) which
addresses the pin directly rather than going through a channel number — simpler and
forward-compatible.

---

## Required libraries

Install via **Tools → Manage Libraries** (or `arduino-cli lib install`):

- **Adafruit NeoPixel** — the status LED is an addressable WS2812; `Indicator.h` pulls it in.
- **Blynk** — the Blynk client library (includes `BlynkSimpleEsp32_SSL.h` and the
  Edgent headers).

```bash
arduino-cli lib install "Adafruit NeoPixel" "Blynk"
```

Without both libraries, the sketch will not compile.

---

## First-time provisioning (adding the device in the Blynk app)

This firmware uses **Blynk Edgent**: WiFi credentials and the auth token are stored in
NVS flash at runtime — there are no hardcoded secrets in the source.

**Before flashing**, fill in your template details in the `.ino`:

```cpp
#define BLYNK_TEMPLATE_ID   "TMPLxxxxxxxx"   // from blynk.cloud → Templates
#define BLYNK_TEMPLATE_NAME "Fan Controller"
```

**First boot sequence:**

1. Flash the firmware over USB (see *Build & upload* below).
2. The onboard LED **blinks blue slowly** — the device is in provisioning (AP) mode.
3. On your phone, open the **Blynk app → + Add new device → Find devices nearby**.
4. The device appears as `Blynk-XXXXXX`. Tap it and follow the prompts to enter your
   WiFi SSID / password and link it to your template.
5. On success the LED switches to a **teal wave** (MODE_RUNNING) and the device appears
   online in your Blynk dashboard.

Credentials are saved to NVS and survive reboots. See *Changing WiFi* below if you need
to move the device to a different network later.

---

## LED status indicator

The onboard WS2812 (GPIO48) shows the Edgent connection state at a glance:

| LED pattern              | State                                  |
|--------------------------|----------------------------------------|
| Blue, slow pulse         | Waiting for provisioning (AP mode)     |
| Blue, fast blink         | Actively being configured              |
| Teal, slow pulse         | Connecting to WiFi                     |
| Teal, fast blink         | Connecting to Blynk cloud              |
| Teal, slow wave (breathe)| Running normally                       |
| Magenta, fast blink      | OTA firmware update in progress        |
| Red, irregular blink     | Error state                            |
| White, slow wave → fast blink | BOOT button held (3 s → 5 s) — factory-reset warning |

---

## Arduino IDE settings (ESP32-S3 N16R8)

Select board **"ESP32S3 Dev Module"**, then in **Tools**:

| Setting          | Value                                           |
|------------------|-------------------------------------------------|
| Board            | **ESP32S3 Dev Module**                          |
| USB CDC On Boot  | **Enabled** — required for `Serial` over USB    |
| Flash Size       | **16MB (128Mb)**                                |
| Flash Mode       | QIO 80MHz                                       |
| PSRAM            | **Disabled** — this sketch doesn't use PSRAM    |
| Partition Scheme | **16M Flash (3MB APP/9.9MB FATFS)** — has OTA   |
| Upload Mode      | UART0 / Hardware CDC                            |
| Upload Speed     | 921600                                          |
| CPU Frequency    | 240 MHz (160 MHz is also fine)                  |

> **PSRAM:** the 8 MB OPI PSRAM is present on this board but unused; **Disabled** is the
> correct safe choice.

> **⚠️ If "OPI PSRAM" is missing or CPU maxes at 160 MHz,** the wrong board is selected —
> you are likely on the old *ESP32C3 Dev Module* profile. Re-select **Tools → Board →
> esp32 → ESP32S3 Dev Module**.

> Use the board's **USB Type-C port** with a **data-capable** cable. If uploads fail, hold
> **BOOT**, tap **RST**, release **BOOT** to force download mode, flash once; it behaves
> normally from then on.

---

## Build & upload from the command line (arduino-cli)

Verified with **arduino-cli 1.5.x** and the **esp32:esp32 3.3.x** core. One-time setup:

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install "Adafruit NeoPixel" "Blynk"
```

The FQBN below mirrors the IDE settings table above (OTA-capable partition scheme,
PSRAM disabled):

```bash
FQBN="esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,UploadSpeed=921600"

# compile only (verify)
arduino-cli compile --fqbn "$FQBN" esp32s3-fan-controller

# compile and write artifacts into build/ (produces the .bin for OTA)
arduino-cli compile --fqbn "$FQBN" --export-binaries esp32s3-fan-controller

# find the port, then flash over USB
arduino-cli board list
arduino-cli upload --fqbn "$FQBN" -p /dev/ttyACM0 esp32s3-fan-controller
```

`--export-binaries` writes `esp32s3-fan-controller.ino.bin` into
`esp32s3-fan-controller/build/esp32.esp32.esp32s3/` — exactly the file OTA ships.

> **Upload trouble?** Hold **BOOT**, tap **RST**, release **BOOT**, then re-run upload.

---

## OTA firmware update (no factory reset needed)

OTA updates the **app partition only**. WiFi credentials and the Blynk auth token live
in NVS and are left untouched — **no re-provisioning needed**.

### Step 1 — Bump the firmware version

In the sketch, increment `BLYNK_FIRMWARE_VERSION` (e.g. `"0.1.0"` → `"0.1.1"`).
Blynk reads this from the uploaded `.bin` and **refuses to ship a version that matches
what is already running**.

### Step 2 — Check the partition scheme (one-time)

**Tools → Partition Scheme** must reserve two OTA app slots:

- ✅ *16M Flash (3MB APP/9.9MB FATFS)* — recommended here
- ✅ *16M Flash (2MB APP/12.5MB FATFS)*
- ❌ *Huge APP (3MB No OTA)* — OTA silently never works

If you have been using a No-OTA scheme, switch now and do **one last USB flash** — then
all future updates can go over the air.

### Step 3 — Export the compiled binary

1. **Sketch → Export Compiled Binary**, or from CLI:
   ```bash
   arduino-cli compile --fqbn "$FQBN" --export-binaries esp32s3-fan-controller
   ```
2. Open `esp32s3-fan-controller/build/esp32.esp32.esp32s3/`.
3. Grab the **plain application binary** — the only file OTA wants:
   ```
   esp32s3-fan-controller.ino.bin
   ```

The build folder contains several `.bin` files — only the one ending in `.ino.bin`
(~1 MB) goes to OTA:

| File                               | Size   | What it is                             | OTA? |
|------------------------------------|--------|----------------------------------------|:----:|
| **`...ino.bin`**                   | ~1 MB  | **The app image — upload this**        | ✅   |
| `...ino.merged.bin`                | ~4 MB  | full-flash image (bootloader+parts+app)| ❌   |
| `...ino.bootloader.bin`            | ~22 KB | 2nd-stage bootloader                   | ❌   |
| `...ino.partitions.bin`            | ~3 KB  | partition table                        | ❌   |
| `boot_app0.bin`                    | ~8 KB  | OTA slot-selector stub                 | ❌   |

> **Sanity check:** the file ends in exactly `.ino.bin` (no `merged`/`bootloader`/
> `partitions`), is ~1 MB, and its timestamp is from your **latest** export — *after*
> the version bump.

### Step 4 — Ship it from blynk.cloud

1. Log in to **blynk.cloud** → **Devices** list → open your device.
2. Top-right **⋮ (More) → Update Firmware**.
3. Upload `esp32s3-fan-controller.ino.bin`. Blynk shows the detected version, board
   type, and size — confirm they are correct.
4. Select the target device and **Start / Ship**.

The device downloads to the spare app slot, reboots into the new firmware, and
auto-rolls back if the new image fails to connect.

> OTA only works between builds for the same board type. The very first ESP32-S3 flash
> must be over USB.

### If OTA does nothing

- **Version unchanged** → bump `BLYNK_FIRMWARE_VERSION`.
- **No-OTA partition scheme** → re-flash once via USB with an OTA scheme.
- **Device offline** → update queues until it reconnects.
- **Wrong `.bin`** → use the plain `*.ino.bin`, not merged/bootloader/partitions.

---

## USB firmware update without losing config

A normal sketch upload writes only the **app partition** — the same partition OTA
touches. WiFi credentials and the Blynk auth token live in the **NVS partition**
(`Preferences`, namespace `"blynk"`) and are never touched by a plain upload.

- **Arduino IDE:** make sure **Tools → Erase All Flash Before Sketch Upload** is
  **Disabled** (the default). Then upload as normal.
- **arduino-cli:** the default `upload` command does not erase NVS. Do **not** add an
  `esptool.py erase_flash` step — that nukes NVS and forces re-provisioning.

> **Keep the partition scheme identical between flashes.** If you switch schemes, the
> new partition table can move NVS and your saved config becomes unreadable. Stay on
> `app3M_fat9M_16MB`.

---

## Changing WiFi — re-provisioning (BOOT-button config reset)

When you need to move the device to a different WiFi network, wipe the stored credentials
and auth token, then re-run provisioning. **No re-flash needed** — same firmware.

**How:** with the device powered and running, **press and hold the BOOT button (GPIO0)
for ~10 seconds, then release**. The reset fires on release.

- After ~3 s the LED starts a slow **white wave**; at ~5 s it switches to a fast **white
  blink**. Once you see the blink you have held long enough — release to reset.
- On release the device clears WiFi + auth token from NVS, reboots, and enters
  provisioning (AP) mode. Follow the *First-time provisioning* steps above to re-pair.

> The action threshold is `BUTTON_HOLD_TIME_ACTION` (5 s, set in `Settings.h`). A brief
> accidental tap (< 5 s) never triggers a reset.

> **Not the same as download mode.** The flashing combo — hold **BOOT**, tap **RST**,
> release **BOOT** — holds BOOT across a reset to force the bootloader; it does **not**
> touch your config.

---

## Blynk message budget (free-tier planning)

**Quota: 200,000 messages per month.**

A message = one data command between device and cloud. Counted: every
`Blynk.virtualWrite`, every inbound app write, each pin in `syncVirtual`. The keep-alive
heartbeat is free.

### Per-source cost

| Source                           | Trigger                           | Cost                        |
|----------------------------------|-----------------------------------|-----------------------------|
| **Uptime (V6)**                  | every 5 min, always on            | **288 / day**               |
| **Stepper angle (V13)**          | every 1 s *while stepper runs AND V15 = 1* | 3,600 / hour of rotation |
| **Timer countdown (V4)**         | every ~1 min while timer active   | negligible (≤60/run)        |
| Reconnect `syncVirtual` (~12 pins)| each Blynk reconnect             | ~12 msgs                    |
| Inbound app commands             | each button/slider from phone     | 1 msg each                  |

> ⚠️ **The stepper angle (V13) is the dominant cost** when enabled. It fires every second
> while the motor is oscillating and **V15 = 1**. At 4 hours of daily oscillation that is
> **14,400 msgs/day ≈ 432,000/month** — well over the free-tier limit on its own. Flip
> **V15 to 0** from the app to suppress V13 updates entirely while still running the motor.
> The V13 display simply freezes at the last reported angle; the oscillation is unaffected.

> **Uptime** is throttled to 5 minutes (288/day ≈ 8,640/month) to avoid the quota burn
> a shorter interval would cause (at 1 s it would be 86,400/day — 2.6 M/month alone).

### Realistic per-device total

| Component                                      | Est. msgs/day | /month   |
|------------------------------------------------|---------------|----------|
| Uptime baseline (5 min, always on)             | 288           | ~8,600   |
| Stepper oscillation (e.g. 2 h/day, V15 = 1)    | 7,200         | ~216,000 |
| App interaction (buttons, sliders)             | ~100          | ~3,000   |
| Reconnect syncs (stable WiFi)                  | ~50           | ~1,500   |
| **Typical total (2 h oscillation/day)**        | **~7,640/day**| **~229k**|

This is already close to the free-tier limit if the stepper runs 2 h/day with V15 on. With
V15 = 0 (angle updates suppressed), the stepper contributes **0 messages** and the daily
total drops to ~450/day ≈ 13k/month regardless of how long the motor runs.
