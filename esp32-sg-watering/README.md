# Home SG Watering — ESP32-C3 Super Mini

Blynk Edgent firmware for a 2-relay watering controller. WiFi credentials and the
auth token are provisioned at runtime via the Blynk app (no hardcoded secrets).

- **Board:** ESP32-C3 Super Mini (see `ESP32C3.md`)
- **Relays:** GPIO6 (relay 0), GPIO7 (relay 1) — HIGH = on
- **Template:** `Home SG Watering` (`TMPL6Ho-4zn87`)
- **Firmware version:** bump `BLYNK_FIRMWARE_VERSION` before every OTA ship

## Virtual pin layout

| Pin | Name           | Type            | Notes                                  |
|-----|----------------|-----------------|----------------------------------------|
| V0  | Switch Relay 0 | button 0/1      | → GPIO6                                |
| V1  | Running Time 0 | slider 0–300 s  |                                        |
| V2  | Time On 0      | display (int)   | seconds elapsed, read-only             |
| V3  | Run Max 0      | toggle          | run to `MAX_RUNNING_TIME_S` (300 s)    |
| V4  | Switch Relay 1 | button 0/1      | → GPIO7                                |
| V5  | Running Time 1 | slider 0–300 s  |                                        |
| V6  | Time On 1      | display (int)   | seconds elapsed, read-only             |
| V7  | Run Max 1      | toggle          | run to `MAX_RUNNING_TIME_S` (300 s)    |
| V8  | Uptime         | **String**      | `2d 03h 14m` format, read-only         |

> **V8 must be a String datastream** in the Blynk console (not Integer), or the
> formatted uptime text is dropped.

## Safety design

The pin is **never** HIGH longer than `MAX_RUNNING_TIME_S` (300 s). Three layers:
1. `BlynkTimer` countdown (`_cb_stop`) — normal stop.
2. Software backstop in `run()` — catches a missed timer.
3. Hardware `esp_timer` failsafe (priority-22 task) — forces the pin LOW even if
   `loop()` is blocked during a WiFi/Blynk reconnect.

## OTA firmware update (no factory reset needed)

OTA updates the **app partition only**. Your WiFi credentials and Blynk auth token
live in NVS and are left untouched — **no re-provisioning, no factory reset.**

### Step 1 — Bump the firmware version

In the sketch, increment `BLYNK_FIRMWARE_VERSION` (e.g. `"1.0.1"` → `"1.0.2"`).
The version is compiled into the binary; Blynk reads it from the uploaded `.bin`
and **refuses to ship a version that matches what's already running**.

### Step 2 — Use an OTA-capable partition scheme (one-time check)

**Tools → Partition Scheme** must reserve OTA app slots:

- ✅ *Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)*
- ✅ *Minimal SPIFFS (1.9MB APP with OTA)*
- ❌ *Huge APP (3MB No OTA)* — **OTA silently never works**

If you've been flashing with a No-OTA scheme, switch now and do **one last USB
flash** with an OTA scheme. Every subsequent update can then go over the air.

### Step 3 — Export the compiled binary

1. **Sketch → Export Compiled Binary** (compiles and writes the `.bin`).
2. **Sketch → Show Sketch Folder** → open
   `build/esp32.esp32.esp32c3/`.
3. Grab the **plain application binary** — this is the only file OTA wants:

   ```
   esp32-sg-watering/build/esp32.esp32.esp32c3/esp32-sg-watering.ino.bin
   ```

**The build folder has many `.bin` files — only `esp32-sg-watering.ino.bin`
(~1.2 MB) is for OTA.** The rest are for USB flashing or are build artifacts:

| File                          | Size   | What it is                              | OTA? |
|-------------------------------|--------|-----------------------------------------|:----:|
| **`...ino.bin`**              | ~1.2 MB | **The app image — upload this**         | ✅   |
| `...ino.merged.bin`           | ~4 MB  | full-flash image (bootloader+parts+app) | ❌   |
| `...ino_flashed.bin`          | ~1.2 MB | post-USB-flash artifact                 | ❌   |
| `...ino.bootloader.bin`       | ~19 KB | 2nd-stage bootloader                    | ❌   |
| `...ino.partitions.bin`       | ~3 KB  | partition table                         | ❌   |
| `boot_app0.bin`               | ~8 KB  | OTA slot-selector stub                  | ❌   |
| `*_flashed.bin`               | —      | duplicates from the last USB flash      | ❌   |

> **Sanity check:** the file ends in exactly `.ino.bin` (no
> `merged`/`bootloader`/`partitions`/`flashed`), is ~1.2 MB, and its timestamp is
> from your **latest** export — *after* the version bump. A stale file ships old
> firmware. After upload, confirm Blynk reads the expected `BLYNK_FIRMWARE_VERSION`.

### Step 4 — Ship it from blynk.cloud

1. Log in to **blynk.cloud** (web console).
2. Open the device (**Devices** list / Search).
3. Top-right **⋮ (More) → Update Firmware**.
4. **Upload** `esp32-sg-watering.ino.bin`. Blynk parses it and shows the detected
   **version**, board type, and size — confirm these are correct.
5. Select the target device and **Start / Ship**.

The device (if **online**) is notified, downloads to the spare app slot, flashes,
and reboots into the new firmware. If the new image fails to connect, Blynk
**auto-rolls-back** to the previous version.

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

> ⚠️ **The uptime timer was the budget-killer.** At the original 1 s interval it
> sent **86,400 msgs/day** (~2.6M/month) — it alone would blow the monthly quota in
> under 3 days. Now throttled to **10 min = 144/day** (~4,300/month).

### Realistic per-device total

| Component                                   | Est. msgs/day | /month  |
|---------------------------------------------|---------------|---------|
| Uptime baseline (10 min, always on)         | 144           | ~4,300  |
| Watering (e.g. 6 runs/day × ~120 s)         | ~750          | ~22,500 |
| App interaction (checking, taps)            | ~150          | ~4,500  |
| Reconnect syncs (stable WiFi)               | ~100          | ~3,000  |
| **Typical total**                           | **~1,150/day** | **~35k** |
| Conservative budget (with headroom)         | ~1,500/day    | ~45k    |

### Affordability vs the 200,000 / month quota

| Devices | ~msgs/month (conservative) | Verdict              |
|---------|----------------------------|----------------------|
| 1       | ~35–45k                    | ✅ plenty of room     |
| 2       | ~70–90k                    | ✅ comfortable        |
| 3       | ~105–135k                  | ✅ comfortable        |
| 4       | ~140–180k                  | ✅ still fits         |

The dominant cost is **watering runtime** (`time_on` at 1 msg/s during runs), not
uptime. If you ever approach the cap with many devices, throttle `time_on` to every
5 s during runs (5× fewer) before touching anything else.

### Other limits to verify

The Blynk **free plan also caps the number of devices/templates** independent of
messages. Confirm your plan actually permits a 2nd/3rd device before wiring one up
— the message budget may not be the binding constraint.
