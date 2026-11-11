# bead-designer firmware

Plain C on ESP-IDF (v5.3+). One codebase, two boards:

| Board | Target | Role |
|---|---|---|
| Freenove ESP32 CYD 2.4" (interim) | `esp32` | Firmware development until the product board arrives |
| Waveshare ESP32-S3-Touch-AMOLED-2.41 **V2** | `esp32s3` | Product prototype |

Only `main/board/board_*.c` differs between them. Everything else (UI state
machine, palettes, storage, Wi-Fi export) is shared.

## Status

P0 scaffold, authored before hardware arrived. It has NOT been compiled yet —
first build happens in P1 with the Freenove board on the bench. Expect to fix
small compile errors then; the structure is the deliverable here.

TBV markers:
- `board_freenove.c` pin numbers (pending the Freenove pinout research /
  board arrival).
- Display init sequences in both board files (ported from vendor examples in P1/P2).
- QR code on the Export screen: vendor `qrcodegen.c/.h` (Nayuki, MIT) into
  `main/` in P1; until then the export screen shows the SSID/URL as text.

## Build

```sh
# Freenove (interim board)
idf.py set-target esp32
idf.py build flash monitor

# Waveshare 2.41 V2 (product)
idf.py set-target esp32s3
idf.py build flash monitor
```

The board file is selected automatically by target (`#if CONFIG_IDF_TARGET_*`).

## Control mapping (from spec section 7)

| Mode | L turn | R turn | L push | R push | MENU | SIDE |
|---|---|---|---|---|---|---|
| Draw | X (wraps) | Y (wraps) | erase | place | palette | settings |
| Palette | X | Y | close | choose | close | settings |
| Settings | move | move | back/close | select | - | close |
| Export | - | - | stop AP, back | - | - | stop AP |

## Bench test on hardware

See [BENCH.md](BENCH.md): wiring one encoder to the Freenove's P3/P4 pigtails without a
breadboard, flashing, and a checklist.

## Simulator (esp32sim)

The Freenove build runs unmodified on [esp32sim](https://github.com/mikroverk/esp32sim),
an instruction-level ESP32 emulator, so UI work can be tested without hardware.

**Prerequisites**
- ESP-IDF v5.5 at `~/esp/esp-idf` (`./install.sh esp32,esp32s3`). It also provides the
  classic-ESP32 ROM `esp32_rev300_rom.elf`, which the emulator finds automatically.
- esp32sim at `~/esp32sim`, local branch `freenove-fnk0114` (adds the
  `freenove-fnk0114` board model, classic-ESP32 PCNT, and `--flash-out`):
  `cargo build --release -p esp32sim`.
- `sdkconfig.defaults.esp32` sets rev3 minimum silicon (the emulator models the ECO3 ROM)
  and 4 MB flash.

**Run**
```sh
sim/run.sh --build --web 8080          # build, then open http://127.0.0.1:8080/
sim/run.sh --script sim/scripts/draw.txt --tft-png out.png --max-seconds 10
sim/check.sh                           # all scenarios, compared with sim/golden/*.png
sim/check.sh --update-golden           # accept intentional screen changes
```
In the web UI the ccw/cw buttons (or arrow keys) turn the knob and "push / BOOT" places a bead.

**Script commands** (`<seconds> <cmd> [args]`, `#` comments):
`knob cw|ccw N` (detents on GPIO35/27, 4 PCNT counts each), `press boot MS` (GPIO0),
`adc 34 1.95` (battery pin volts), `poke 3ff48000 80000000` (software reset; flash/NVS kept),
`stop`.

**Persisting NVS between runs:** add `--flash-out state.bin`, and start the next run with
`--flash-image state.bin`.

**Scenarios** (`sim/scripts/`): `boot` (empty grid), `place` (BOOT places a bead),
`draw` (encoder walk and two beads), `autosave` (pattern restored from NVS after a reset),
`keys` (pick a colour on the strip, place and erase via serial keys, `uart0 <key>`),
`picker` (screen mid-pick), `bars` (colour test pattern).

**Limitations**
- Wi-Fi export can't be tested: the emulator supports station mode only, not SoftAP.
- The panel's colour order (RGB vs BGR) and inversion in the board model are assumed and
  must be checked against the real panel.
- The PCNT glitch filter is not modelled (it passes everything through), and there is no
  contact bounce, so button and encoder debounce are not exercised.
