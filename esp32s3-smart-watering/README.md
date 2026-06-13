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
| V9  | Device time    | **String**      | `YYYY-MM-DD HH:MM:SS +07` local, read-only, every 5 min |
| V10 | Schedule       | **String**      | start-time config (see Scheduling), app→device |
| V11 | Moisture cfg   | **String**      | sensor + MQTT config (see Soil-moisture), app→device |

> **V8, V9, V10, and V11 must be String datastreams** in the Blynk console (not
> Integer), or the text is dropped.

> The `.ino` defines readable `#define VPIN_*` aliases for every pin above (e.g.
> `VPIN_MOISTURE_CFG` → V11); the table here is the canonical reference.

## Hardware pin choices (why GPIO38 / GPIO39)

On the **N16R8** chip the "R8" means 8 MB of **octal (OPI) PSRAM**, whose data
lines are hard-wired to **GPIO35–GPIO37** — those pins are unusable. GPIO38 and
GPIO39 sit just past them, are **not** strapping pins, and on this board are only
labelled `SD_CMD` / `SD_CLK` for the unused on-board microSD slot, so they are
free for relay control. (GPIO39 doubles as JTAG `MTCK`, which only matters if you
attach a hardware debugger — irrelevant in production.)

The **BOOT button is GPIO0** (active-low) and the onboard **WS2812 RGB LED is
GPIO48** — neither collides with the relays.

## Required libraries

Install both via **Tools → Manage Libraries** (or `arduino-cli lib install`, below):

- **Adafruit NeoPixel** — the status LED is an addressable WS2812, so `Indicator.h`
  pulls it in.
- **PubSubClient** (Nick O'Leary) — MQTT client used by `MqttPublisher.h` for the
  soil-moisture push.

Without either, the sketch won't compile.

## Safety design

The pin is **never** HIGH longer than `MAX_RUNNING_TIME_S` (300 s). Three layers:
1. `BlynkTimer` countdown (`_cb_stop`) — normal stop.
2. Software backstop in `run()` — catches a missed timer.
3. Hardware `esp_timer` failsafe (priority-22 task) — forces the pin LOW even if
   `loop()` is blocked during a WiFi/Blynk reconnect.

## Scheduling (timed automatic watering)

The device keeps real wall-clock time via **NTP** (`configTzTime`, resynced hourly)
and can start a relay at configured local times. Timezone is compiled in — **Vietnam,
UTC+7, no DST** (`SCHED_TZ "<+07>-7"` in the `.ino`; the POSIX sign is inverted on
purpose).

- **V9** shows the device's current local time, so you can confirm it isn't drifting.
  It reads `SYNCING...` until the first NTP sync succeeds.
- **V10** holds the schedule config string, editable from the Blynk web/iOS app.

### Config string format (V10)

```
<relay0_sched>|<relay1_sched>           (at most one '|')
```

- No `|` → relay 0 only.  Leading `|` → relay 1 only.  **Empty string → no schedule.**
- Each relay: `<day>[,<day>]*;<time>[,<time>]*`
  - **day:** `Mon Tue Wed Thu Fri Sat Sun` (case-insensitive) or `*` = every day.
  - **time:** `HH:MM:SS` or `HH:MM` (seconds default `0`). Range-checked.

Examples:

| V10 string | Effect |
|------------|--------|
| `Mon,Tue,Wed,Fri;10:30:00,16:30:30` | relay 0 fires those 2 times on those 4 days |
| `*;06:00`                           | relay 0 every day at 06:00:00 |
| `*;06:00\|*;06:05`                  | relay 0 at 06:00, relay 1 at 06:05 |
| `\|Sat,Sun;07:30`                   | relay 1 only, weekends at 07:30 |
| *(empty)*                           | no scheduled watering |

A scheduled start uses that relay's **Running Time slider (V1/V5)** for duration and
is bounded by the same 300 s ceiling + 3-layer failsafe as a manual start. A start is
**skipped if the relay is already running**.

### Invalid input

Each side of the `|` is validated **independently**. A malformed side disables *only
that relay* and its text is rewritten to `INVALID FORMAT`, leaving the good side
running. E.g. `*;06:00|Tue;99:99` becomes `*;06:00|INVALID FORMAT`.

### Offline / power behavior

- Schedules **survive a Blynk/internet outage**: the clock keeps ticking from the
  last NTP sync, so watering continues on time.
- Schedules **do not survive a power loss without internet on reboot** — the S3 has
  no battery-backed RTC, so on cold boot it can't know the time until NTP syncs.
  Until the clock is valid, nothing fires (V9 shows `SYNCING...`). With reliable
  power+internet this window is only a few seconds at boot. (Add a DS3231 RTC if you
  ever need true cold-boot-offline scheduling.)
- A start more than **10 min late** (`MAX_LATENESS_S`, e.g. `loop()` blocked through a
  long reconnect) is treated as missed and skipped — it won't fire at a surprising
  time. The schedule is restored after reboot via `syncVirtual(V10)`.

## Soil-moisture monitoring (MQTT → Prometheus)

Up to **3 analog soil sensors** on **ADC1 (GPIO4 / GPIO5 / GPIO6)** are sampled and
pushed over **MQTT** to a local broker every *interval* seconds, where a
`mqtt2prometheus` exporter turns them into Prometheus metrics for Grafana. The broker
+ exporter run on a Raspberry Pi via Docker — see the repo's
[`monitoring/`](../monitoring/) stack. Firmware side: `Moisture.h` (config parsing +
ADC sampling) and `MqttPublisher.h` (PubSubClient wrapper).

- Readings are **raw 12-bit ADC** (0–4095); raw→% calibration is done downstream
  (Grafana / Prometheus rules) so re-calibrating never means re-flashing.
- **Both relay states** (`r0`/`r1`, 1=on) ride in the same message → a `relay_on`
  gauge in Prometheus. They're published every interval **and** immediately on any
  relay change, so a short pump run (default 10 s) isn't missed by the periodic grid.
- Publishing goes to **your own broker, not Blynk** — it costs **zero** Blynk messages.
- **ADC1 is required** (ADC2 is unusable while WiFi is on). GPIO4/5/6 sit on the
  camera/ADC header — free here because this build doesn't wire the camera
  (see `ESP32S3.md` §10).
- ⚠️ **Only list as many pins as you physically wire.** An unconnected ADC pin floats
  and publishes drifting noise, **not** `0`.

### Config string format (V11)

```
<pin1>[,<pin2>[,<pin3>]] ; <device_name> ; <mqtt_host>[:<port>] ; <interval_s>
```

- **pins:** 1–3 comma-separated labels. The *count* selects how many channels are
  sampled (positional: name 1 → GPIO4/`s0`, 2 → GPIO5/`s1`, 3 → GPIO6/`s2`). The names
  are for humans only — the wire format is `s0/s1/s2` by channel, and friendly names
  are mapped in **Grafana** (the one dynamic MQTT/Prometheus label is spent on the
  device name).
- **device_name:** charset `[A-Za-z0-9_-]`; becomes the MQTT topic segment + the
  Prometheus `sensor` label.
- **mqtt_host[:port]:** broker IP/hostname; port optional (default **1883**).
- **interval_s:** publish period in seconds, optional (default **60**, clamped **10–3600**).

Examples:

| V11 string | Effect |
|------------|--------|
| `tomato,basil,mint;garden-node1;192.168.1.50:1883;60` | 3 sensors → `watering/garden-node1/moisture` every 60 s |
| `tomato;balcony;mqtt.local` | 1 sensor, default port (1883) + interval (60 s) |
| *(empty / malformed)* | rejected → `INVALID FORMAT`, sampling stops |

Publishes e.g. `{"s0":2731,"s1":2540,"s2":2600,"r0":0,"r1":1}`. A retained Last-Will on
`watering/<device>/status` (`online`/`offline`) is registered for the future
command/Grafana-annotation path.

### Invalid input

A malformed string is rejected wholesale, its text rewritten to `INVALID FORMAT`, and
sampling stops until a valid string is set (same pattern as V10).

### Broker offline behavior

If the broker is unreachable, each publish attempt is **capped at ~2 s**
(`MqttPublisher::begin()` sets a 2 s TCP-connect timeout + 2 s MQTT CONNACK timeout)
and retried on the next interval — it recovers automatically when the broker returns.
PubSubClient does not reconnect on its own; the per-interval attempt is what heals it.
The brief stall never affects pump safety: the hardware `esp_timer` failsafe runs on
its own task, independent of `loop()`.

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

## Build & upload from the command line (arduino-cli)

Same toolchain as the IDE, scripted. Verified with **arduino-cli 1.5.x** and the
**esp32:esp32 3.3.x** core. One-time setup:

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install "Adafruit NeoPixel" "PubSubClient" "Blynk"
```

The FQBN options below mirror the **Tools** menu in the IDE table above (notably the
OTA-capable `app3M_fat9M_16MB` partition scheme and `PSRAM=disabled`). Stash it in a
variable to keep commands readable:

```bash
FQBN="esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,UploadSpeed=921600"

# compile only
arduino-cli compile --fqbn "$FQBN" esp32s3-smart-watering

# compile AND write artifacts into build/ (produces the .bin used for OTA, below)
arduino-cli compile --fqbn "$FQBN" --export-binaries esp32s3-smart-watering

# find the port, then flash over USB
arduino-cli board list
arduino-cli upload --fqbn "$FQBN" -p /dev/ttyACM0 esp32s3-smart-watering
```

`--export-binaries` writes `esp32s3-smart-watering.ino.bin` into
`esp32s3-smart-watering/build/esp32.esp32.esp32s3/` — exactly the file the OTA section
ships. Without the flag, arduino-cli compiles in a temp dir and keeps nothing.

> **Upload trouble?** If the port doesn't appear or upload won't start, hold **BOOT**,
> tap **RST**, release **BOOT**, then re-run the upload once (see the USB note above).

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

1. **Sketch → Export Compiled Binary** (compiles and writes the `.bin`) — or from the
   CLI, `arduino-cli compile --fqbn "$FQBN" --export-binaries esp32s3-smart-watering`.
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
| **Device time (V9)**            | every 5 min, always on           | **288 / day**              |
| **Time On (V2/V6)**             | every 1 s *while a relay runs*   | ≈ run-seconds, ≤300/run    |
| Scheduled start                 | each scheduled relay fire        | 1 msg (V0/V4 → 1)          |
| Shutdown resets (switch/run_max/time_on) | each relay stop         | 1–3 msgs                   |
| Conflict-reject writes          | conflicting command sent         | 1 msg (rare)               |
| Reconnect `syncVirtual` (V0–V7, V10, V11) | each Blynk reconnect   | ~10 msgs                   |
| Inbound app commands            | each button/slider from phone    | 1 msg each                 |

> ⚠️ **The uptime timer was the budget-killer.** At a 1 s interval it would send
> **86,400 msgs/day** (~2.6M/month) — it alone would blow the monthly quota in
> under 3 days. It is throttled to **10 min = 144/day** (~4,300/month).

### Realistic per-device total

| Component                                   | Est. msgs/day | /month  |
|---------------------------------------------|---------------|---------|
| Uptime baseline (10 min, always on)         | 144           | ~4,300  |
| Device time (5 min, always on)              | 288           | ~8,600  |
| Watering (e.g. 6 runs/day × ~120 s)         | ~750          | ~22,500 |
| App interaction (checking, taps)            | ~150          | ~4,500  |
| Reconnect syncs (stable WiFi)               | ~100          | ~3,000  |
| **Typical total**                           | **~1,440/day** | **~43k** |
| Conservative budget (with headroom)         | ~1,800/day    | ~54k    |

The dominant cost is **watering runtime** (`time_on` at 1 msg/s during runs), not
uptime. If you ever approach the cap with many devices, throttle `time_on` to every
5 s during runs (5× fewer) before touching anything else.
