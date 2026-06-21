# nano-watchdog

An external **hardware reset guardian** for the two co-located ESP32-S3 smart-watering
devices, running on a classic **Arduino Nano (ATmega328P, 5 V)**.

## What problem it solves

After a long power outage the ESP32-S3 boards sometimes fail their cold boot on the slow
power-rail ramp (GPIO0 sampled wrong → stuck in UART download mode, firmware never runs).
The only fix is pressing reset once the rails are stable. This Nano watches each device's
heartbeat and, if a device goes silent, **pulses its EN/reset pin** — automating that
button press so an unattended device recovers by itself.

The Nano is used precisely because its own power-on reset is bulletproof (simple POR +
brown-out detect, no strapping pins, no boot-mode trap): it reliably survives the very
cold-start that trips the ESP32, so the guardian never gets stuck itself.

## How it works

- Each ESP32-S3 toggles **GPIO21** at ~2 Hz while its `loop()` runs (the heartbeat).
- The Nano counts edges. Every **60 s** it checks each device; **3** consecutive silent
  windows (~180 s) ⇒ it pulses that device's reset.
- Reset is **open-drain**: the Nano only pulls EN **down to GND**, then releases to
  high-Z. It never drives 5 V onto the 3.3 V ESP32 — same action as the reset button.

**Fail-safe:** every reset pin idles as `INPUT` (high-Z), so a dead/unplugged Nano can
never hold an ESP32 in reset. Worst case it does nothing and you're back to manual reset.

## Pin map

| Signal              | Nano pin | Direction        | Connects to                          |
|---------------------|----------|------------------|--------------------------------------|
| Heartbeat, device 0 | **D2**   | input            | ESP32 #0 **GPIO21** (via 1 kΩ)       |
| Heartbeat, device 1 | **D3**   | input            | ESP32 #1 **GPIO21** (via 1 kΩ)       |
| Reset, device 0     | **D4**   | open-drain out   | ESP32 #0 **EN / CHIP_PU** node       |
| Reset, device 1     | **D5**   | open-drain out   | ESP32 #1 **EN / CHIP_PU** node       |
| Status LED          | **D13**  | output           | onboard LED (blinks each check)      |
| Serial debug        | D0/D1    | —                | USB (115200) — kept free for logs    |

## Wiring detail

### Common ground (do this first — nothing works without it)
Tie **Nano GND ↔ ESP32 #0 GND ↔ ESP32 #1 GND ↔ supply GND** together. All three boards
must share one ground reference.

### Heartbeat lines (ESP32 → Nano), per device
```
  ESP32 GPIO21 ──[ 1 kΩ ]──┬── Nano D2 (dev0) / D3 (dev1)
                           │
                        [ 100 kΩ ]
                           │
                          GND
```
- The **100 kΩ pull-down is the important one**: when the ESP32 is off/silent its line
  is high-Z, and the pull-down holds the Nano input at a steady LOW so noise can't be
  misread as "alive." The 1 kΩ series resistor is just protection.
- Do **not** enable the Nano's internal pull-up on D2/D3 (the sketch uses plain `INPUT`).
  3.3 V from the ESP32 reads as a solid HIGH on the 5 V Nano; the ESP32 is never exposed
  to 5 V.

### Reset lines (Nano → ESP32 EN), per device
```
  Nano D4 (dev0) / D5 (dev1) ─────────── ESP32 EN / CHIP_PU
                                         (the non-GND pad of the reset button)
```
- Connect to the **same node the reset button shorts to GND** — i.e. the EN side of the
  button, *not* the ground side. If EN is broken out on a header, use that pin.
- Direct connection is correct (it mirrors the button, which is a hard short). The Nano
  only ever pulls this LOW or releases it — never drives 5 V — so no level shifter is
  needed. (A 100 Ω series resistor is fine if you want extra insurance.)

### Powering the Nano
Power it from the **same supply** as the ESP32 boards so it cycles with them:
- **Preferred:** feed regulated **5 V → Nano `5V` pin** (bypasses the Nano's regulator).
- **Or:** feed **12 V → Nano `VIN`** (onboard regulator handles 7–12 V; runs a bit warm).

## Timing you'll observe

- **After a long outage:** both boards power up together. If an ESP32 fails its cold boot,
  the Nano sees no heartbeat for 3 windows and resets it at roughly **T+180 s** — by then
  the rails are stable, so it boots cleanly. (Tune `MAX_STRIKES` / `CHECK_INTERVAL_MS` in
  the sketch if you want faster recovery.)
- **Healthy device:** ~240 edges per window, strikes stay at 0, never reset.

## Build / upload

Standard Arduino Nano target (ATmega328P). No external libraries. Open the serial monitor
at **115200** to watch the per-window edge counts and any reset events.

> Per project policy, flashing is left to you — this directory only contains the source.
