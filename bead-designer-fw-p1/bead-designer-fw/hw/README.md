# Hardware tests: Waveshare ESP32-S3-Touch-AMOLED-2.41 Rev 2.0

These tests run the firmware on the real board over its USB Serial/JTAG console
(`/dev/ttyACM0`). They drive the UI with the serial test keys and capture the screen
with the `F` framebuffer dump, so results compare pixel-for-pixel with a golden PNG.
There is no battery sense and no touch yet; those come later.

```sh
hw/run.sh flash                    # build (build-s3/, sdkconfig.s3) and flash
hw/run.sh boot                     # reset; print and check the boot log
hw/run.sh shot hw/out/now.png      # dump whatever is on screen right now
hw/run.sh run                      # every scenario; compare with hw/golden
hw/run.sh run menu draw            # selected scenarios
hw/run.sh run --update-golden ...  # accept the current screens as golden
hw/run.sh list                     # list scenarios
```

`hw/run.sh` sources ESP-IDF and runs under `newgrp uucp`, because the port is `root:uucp`.

## What the tests check

- **Boot check:** the log must contain the board banner, `TCA9554 ok at 0x20` and
  `RM690B0 ready`, and must have no `E (` error lines and no crash signatures.
- **Scenarios:** `sim/scripts/*.txt` (shared with the simulator) and `hw/scripts/*.txt`.
  - Each scenario starts with a factory reset (`!Z`: NVS erased, reboot), so saved
    patterns and settings can't leak between tests.
  - The steps are replayed with the same relative timing. The screen is dumped at
    `stop` and compared with `hw/golden/<name>.png`.
  - A scenario fails on a crash, an error at boot, a corrupt dump, or any pixel
    differing from the golden. Results go to `hw/out/<name>.{png,log}`.
- **Translation from simulator scripts:**
  - `uart0 <k>` becomes key `k`.
  - `knob cw|ccw n` becomes `l`/`j` × n.
  - `press sw` / `press menu` / `press boot` become `p` / `m` / `o`.
  - Scripts that need simulator-only inputs (the Freenove ADC D-pad ladder, register
    pokes) are skipped.
  - Hardware-only scripts can also use `key <string>`.
  - `browse` (encoder held while turning, with detent acceleration) is simulator-only.
- **Screen-saver scenarios** (`saver`, `saver_cycle`) animate in real time, so they
  can't match a golden pixel for pixel. They pass if the saver is up instead: at
  least 30% of the screen black and at least 200 lit pixels.
- **Marquee:** the name line scrolls in real time. The screen above it must match the golden exactly; the line itself only has to show text.
- **Boot check:** with the fast `-O2` boot, the first log lines go out while USB re-enumerates. So `boot` also sends `?`, and the board reprints a status line (`board: Waveshare 2.41 Rev 2.0; TCA9554 ok at 0x20; RM690B0 ready ...`), which the check reads.
- **Battery and power scenarios** (`hw/scripts/`): `batt_menu`, `batt_low`, `batt_alert`, `batt_alert_dismiss` and `sleep_wake`. Shut Down and the critical level put the board into soft off (the USB port goes away), so they are checked by hand: press PWR to wake.
- **Current result** (2026-11-17, Rev 2.0 board): 40 pass (37 exact, 1 marquee, 2 saver checks) and 4 are skipped as simulator-only (2026-11-18, after battery management).

## Firmware test hooks (CONFIG_BEAD_SERIAL_KEYS, bench builds only)

- `F`: framebuffer dump. The format is `FBDUMP w h ZLIB`, then `FB:<base64>` lines
  (zlib stream of big-endian RGB565, made by the ROM's deflate), then
  `FBEND <bytes> <crc32>`.
- `!` then `Z` within 1 s: factory reset (erase NVS, restart).
- `b`: step through simulated battery levels:
  - off (real reading);
  - 3950 mV (~72%);
  - 3640 mV (~18%, low: power saving);
  - 3560 mV (~8%, alert);
  - 3420 mV (critical: save and switch off).
- `z` / `q`: PWR short press (sleep / wake) / PWR held (shut down).
- `G`: edge ruler. `B`: every task's backtrace.
- Production builds must turn `CONFIG_BEAD_SERIAL_KEYS` off.

## Board bring-up notes (2026-11-17)

- **Display:**
  - RM690B0 over QSPI at 80 MHz (CS 9, CLK 10, D0–D3 11–14). Reset goes through TCA9554 EXIO0.
  - **Pixel doubling:** the UI is laid out at 225x300 logical pixels and each one is shown as 2x2 on the 450x600 panel. That gives the same physical pixel size as the 2.4" CYD, with crisp pixels. It also makes every panel window even-aligned, which the controller requires. The `F` dump and the goldens are 225x300.
  - **Mounted upside down** in the case, so the flush rotates by 180° (`DISPLAY_ROT180`).
  - **Column offset:** the panel's visible columns start at RAM column 16 (`LCD_X_GAP`). This was measured with the `G` edge ruler, which shows 8 px coloured stripes at every edge.
- **Frame pipeline:**
  - Draws go into a logical shadow framebuffer in PSRAM.
  - `present()`, called once per UI loop pass, copies the changed area into one of two snapshots. A display task on core 1 expands and sends it, one window per frame with ping-pong DMA bands.
  - Each screen change appears at once (no flicker). A full frame takes ~16 ms and runs in parallel with drawing the next.
- **Screen saver (2.41"):**
  - Square dots on a 4 px grid: double the Freenove density, up to ~4,200 dots.
  - Physics runs in 40 ms ticks, split into 3 staggered groups and across both cores. Frames come every 15 ms on a fixed cadence, with each dot blended between ticks.
  - Dots are packed to 26 bytes in internal RAM chunks.
  - It draws into a **two-colour 1-bit bitmap** (8.4 KB), which the display task expands to the two colours. The RGB565 framebuffer is only rebuilt for a test dump (`board_fb_sync`).
  - The build is `-O2` with a 64 KB / 64 B data cache.
  - Measured 63–66 fps across all four motion types.
- **Hang fixed 2026-11-17:** with the saver running, the UI froze. The cause was that the SPI bus and its interrupt were set up on core 0, but transfers were issued from the display task on core 1, and the bus lock eventually stopped being released.
  - Now *all* SPI work (setup, frames, brightness) runs on the display task.
  - The UI task is also subscribed to the task watchdog.
  - The bench key `B` prints every task's backtrace.
  - Console output deliberately does not go through the USB Serial/JTAG driver; the driver is only used to read keys.
- **TCA9554 at 0x20** (I2C GPIO47/48):
  - MENU = EXIO5, SIDE = EXIO6, polled every 5 ms by `input.c`.
  - EXIO7 = USB_PRESENT (control PCB D revB4), not read yet.
- **Battery and power (2026-11-18):** see `main/battery.c`, plus the power section of `main/ui.c`.
  - **Reading:** BAT/3 on GPIO17 (ADC2, curve-fit calibration, 8-sample average), polled every 10 s and smoothed. A LiPo curve gives the percentage. With no cell, the charger holds BAT at ~4.2 V, so it shows 100%.
  - **Battery icon:** always in menu titles; on the name line at ≤20%.
  - **Low battery:**
    - ≤20%: brightness 60%, and a black screen instead of the saver;
    - ≤10%: one "Battery low / Plug in soon" notice;
    - <3.45 V for 3 readings: "Battery empty", save, switch off.
  - **PWR key:** a short press sleeps (saved, panel sleep-in) and any button wakes. Holding 1.5 s is Shut Down (saved, latch dropped).
    - On USB, Shut Down becomes a soft off (deep sleep, PWR wakes as a fresh boot). In deep sleep the USB port disappears.
  - **Idle:** sleep after 10 min with no input; power off after 1 h asleep.
  - **Real LiPo test (2026-11-18):**
    - USB with no cell: 4.20 V. Charging: 4.09–4.11 V. On battery with the screen on: 3.96–3.98 V (~74%).
    - On battery, holding PWR 1.5 s gives "Shutting down", and the board goes fully off on release. A 0.5 s PWR hold switches it back on.
    - Bench key `h` prints the last 64 readings, so a run without USB can be read back afterwards.
    - Not checked against a multimeter yet.
  - **Not yet:** charge sense (needs control PCB D revB4 EXIO7; without the board EXIO7 floats high), true light sleep (the CPU stays awake with the panel off).
- **Not yet:** touch (FT6336U), RTC, IMU.
