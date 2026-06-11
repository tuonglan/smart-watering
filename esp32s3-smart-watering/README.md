# Smart Watering — ESP32-S3 N16R8

Blynk Edgent firmware for a 2-relay watering controller. WiFi credentials and the
auth token are provisioned at runtime via the Blynk app (no hardcoded secrets).

- **Board:** UICPAL ESP32-S3-CAM N16R8 (ESP32-S3-WROOM-1, 16 MB flash / 8 MB OPI PSRAM — see `ESP32S3.md`)
- **Relays:** GPIO38 (relay 0), GPIO39 (relay 1) — HIGH = on
- **Status LED:** onboard WS2812 RGB on GPIO48 (requires the **Adafruit NeoPixel** library)
- **Template:** `Smart Watering` (`TMPL6yGZY2olP`)
- **Firmware version:** bump `BLYNK_FIRMWARE_VERSION` before every OTA ship

> Ported from the ESP32-C3 Super Mini build. The application logic is identical;
> only the board pins (`Settings.h`), relay GPIOs, and the LED driver changed.

## Virtual pin layout

| Pin | Name           | Type            | Notes                                  |
|-----|----------------|-----------------|----------------------------------------|
| V0  | Switch Relay 0 | button 0/1      | → GPIO38                               |
| V1  | Running Time 0 | slider 0–300 s  |                                        |
| V2  | Time On 0      | display (int)   | seconds elapsed, read-only             |
| V3  | Run Max 0      | toggle          | run to `MAX_RUNNING_TIME_S` (300 s)    |
| V4  | Switch Relay 1 | button 0/1      | → GPIO39                               |
| V5  | Running Time 1 | slider 0–300 s  |                                        |
| V6  | Time On 1      | display (int)   | seconds elapsed, read-only             |
| V7  | Run Max 1      | toggle          | run to `MAX_RUNNING_TIME_S` (300 s)    |
| V8  | Uptime         | **String**      | `2d 03h 14m` format, read-only         |

> **V8 must be a String datastream** in the Blynk console (not Integer), or the
> formatted uptime text is dropped.

## Hardware pin choices (why GPIO38 / GPIO39)

On the **N16R8** chip the "R8" means 8 MB of **octal (OPI) PSRAM**, whose data
lines are hard-wired to **GPIO35–GPIO37** — those pins are unusable. GPIO38 and
GPIO39 sit just past them, are **not** strapping pins, and on this board are only
labelled `SD_CMD` / `SD_CLK` for the unused on-board microSD slot, so they are
free for relay control. (GPIO39 doubles as JTAG `MTCK`, which only matters if you
attach a hardware debugger — irrelevant in production.)

The **BOOT button is GPIO0** (active-low) and the onboard **WS2812 RGB LED is
GPIO48** — neither collides with the relays.

## Required library

The status LED is an addressable WS2812, so `Indicator.h` pulls in
**Adafruit NeoPixel**. Install it once via **Tools → Manage Libraries → "Adafruit
NeoPixel"**, or it won't compile.

## Safety design

The pin is **never** HIGH longer than `MAX_RUNNING_TIME_S` (300 s). Three layers:
1. `BlynkTimer` countdown (`_cb_stop`) — normal stop.
2. Software backstop in `run()` — catches a missed timer.
3. Hardware `esp_timer` failsafe (priority-22 task) — forces the pin LOW even if
   `loop()` is blocked during a WiFi/Blynk reconnect.

## Arduino IDE settings (ESP32-S3 N16R8)

Select board **"ESP32S3 Dev Module"**, then in **Tools**:

| Setting              | Value                                          |
|----------------------|------------------------------------------------|
| Board                | **ESP32S3 Dev Module** (see note below)        |
| USB CDC On Boot      | **Enabled** — required for `Serial` over USB   |
| Flash Size           | **16MB (128Mb)**                               |
| Flash Mode           | QIO 80MHz                                       |
| PSRAM                | **Disabled** — this sketch doesn't use PSRAM   |
| Partition Scheme     | **16M Flash (3MB APP/9.9MB FATFS)** — has OTA   |
| Upload Mode          | UART0 / Hardware CDC                            |
| Upload Speed         | 921600                                          |
| CPU Frequency        | 240 MHz (160 MHz is also fine)                 |

> **PSRAM:** the 8 MB OPI PSRAM is unused by this firmware, so **Disabled** is the
> correct, safe choice. Pick **OPI PSRAM** only if you later write code that needs
> the extra RAM.

> **⚠️ If "OPI PSRAM" is missing or the CPU maxes out at 160 MHz, the wrong board
> is selected** — you're still on a C-series profile (e.g. the old *ESP32C3 Dev
> Module*). The C3 has no PSRAM and tops out at 160 MHz. Re-select **Tools → Board
> → esp32 → ESP32S3 Dev Module** and both will appear (240 MHz, OPI PSRAM).

> Use the board's **USB Type-C port** with a **data-capable** cable. The S3 has
> native USB; if uploads fail, hold **BOOT**, tap **RST**, release **BOOT** to
> force download mode, flash once, then it behaves normally.

## OTA firmware update (no factory reset needed)

OTA updates the **app partition only**. Your WiFi credentials and Blynk auth token
live in NVS and are left untouched — **no re-provisioning, no factory reset.**

### Step 1 — Bump the firmware version

In the sketch, increment `BLYNK_FIRMWARE_VERSION` (e.g. `"1.0.1"` → `"1.0.2"`).
The version is compiled into the binary; Blynk reads it from the uploaded `.bin`
and **refuses to ship a version that matches what's already running**.

### Step 2 — Use an OTA-capable partition scheme (one-time check)

**Tools → Partition Scheme** must reserve two OTA app slots:

- ✅ *16M Flash (3MB APP/9.9MB FATFS)* — dual 3 MB app slots, recommended here
- ✅ *16M Flash (2MB APP/12.5MB FATFS)*
- ✅ *Minimal SPIFFS (1.9MB APP with OTA)* — works but wastes most of the 16 MB
- ❌ *Huge APP (3MB No OTA)* — **OTA silently never works**

If you've been flashing with a No-OTA scheme, switch now and do **one last USB
flash** with an OTA scheme. Every subsequent update can then go over the air.

### Step 3 — Export the compiled binary

1. **Sketch → Export Compiled Binary** (compiles and writes the `.bin`).
2. **Sketch → Show Sketch Folder** → open
   `build/esp32.esp32.esp32s3/`.
3. Grab the **plain application binary** — this is the only file OTA wants:

   ```
   esp32s3-smart-watering/build/esp32.esp32.esp32s3/esp32s3-smart-watering.ino.bin
   ```

**The build folder has many `.bin` files — only `esp32s3-smart-watering.ino.bin`
(~1.2 MB) is for OTA.** The rest are for USB flashing or are build artifacts:

| File                          | Size   | What it is                              | OTA? |
|-------------------------------|--------|-----------------------------------------|:----:|
| **`...ino.bin`**              | ~1.2 MB | **The app image — upload this**         | ✅   |
| `...ino.merged.bin`           | ~4 MB  | full-flash image (bootloader+parts+app) | ❌   |
| `...ino.bootloader.bin`       | ~22 KB | 2nd-stage bootloader                    | ❌   |
| `...ino.partitions.bin`       | ~3 KB  | partition table                         | ❌   |
| `boot_app0.bin`               | ~8 KB  | OTA slot-selector stub                  | ❌   |

> **Sanity check:** the file ends in exactly `.ino.bin` (no
> `merged`/`bootloader`/`partitions`), is ~1.2 MB, and its timestamp is from your
> **latest** export — *after* the version bump. A stale file ships old firmware.
> After upload, confirm Blynk reads the expected `BLYNK_FIRMWARE_VERSION`.

### Step 4 — Ship it from blynk.cloud

1. Log in to **blynk.cloud** (web console).
2. Open the device (**Devices** list / Search).
3. Top-right **⋮ (More) → Update Firmware**.
4. **Upload** `esp32s3-smart-watering.ino.bin`. Blynk parses it and shows the
   detected **version**, board type, and size — confirm these are correct.
5. Select the target device and **Start / Ship**.

The device (if **online**) is notified, downloads to the spare app slot, flashes,
and reboots into the new firmware. If the new image fails to connect, Blynk
**auto-rolls-back** to the previous version.

> **Note:** OTA only works between builds **for the same board type**. You cannot
> OTA from the old ESP32-C3 firmware to this ESP32-S3 build — the very first S3
> flash must be over USB.

### If OTA does nothing

- **Version unchanged** → bump `BLYNK_FIRMWARE_VERSION`.
- **No-OTA partition scheme** → re-flash once via USB with an OTA scheme.
- **Device offline** → the update queues until it reconnects; confirm it's online.
- **Wrong `.bin`** → use the plain `*.ino.bin`, not merged/bootloader/partitions.

---

## Blynk message budget (free-tier planning)

A **message** = one data command between device and cloud. Counted: every
`Blynk.virtualWrite`, every inbound app write, each pin in `syncVirtual`,
`logEvent`, property updates. **Not** counted: the keep-alive heartbeat — an idle
connected device costs nothing on its own.

**Quota: 200,000 messages per _month_.**

### Per-source cost

| Source                          | Trigger                          | Cost                       |
|---------------------------------|----------------------------------|----------------------------|
| **Uptime (V8)**                 | every 10 min, always on          | **144 / day**              |
| **Time On (V2/V6)**             | every 1 s *while a relay runs*   | ≈ run-seconds, ≤300/run    |
| Shutdown resets (switch/run_max/time_on) | each relay stop         | 1–3 msgs                   |
| Conflict-reject writes          | conflicting command sent         | 1 msg (rare)               |
| Reconnect `syncVirtual(V0–V7)`  | each Blynk reconnect             | ~8–16 msgs                 |
| Inbound app commands            | each button/slider from phone    | 1 msg each                 |

> ⚠️ **The uptime timer was the budget-killer.** At a 1 s interval it would send
> **86,400 msgs/day** (~2.6M/month) — it alone would blow the monthly quota in
> under 3 days. It is throttled to **10 min = 144/day** (~4,300/month).

### Realistic per-device total

| Component                                   | Est. msgs/day | /month  |
|---------------------------------------------|---------------|---------|
| Uptime baseline (10 min, always on)         | 144           | ~4,300  |
| Watering (e.g. 6 runs/day × ~120 s)         | ~750          | ~22,500 |
| App interaction (checking, taps)            | ~150          | ~4,500  |
| Reconnect syncs (stable WiFi)               | ~100          | ~3,000  |
| **Typical total**                           | **~1,150/day** | **~35k** |
| Conservative budget (with headroom)         | ~1,500/day    | ~45k    |

The dominant cost is **watering runtime** (`time_on` at 1 msg/s during runs), not
uptime. If you ever approach the cap with many devices, throttle `time_on` to every
5 s during runs (5× fewer) before touching anything else.
