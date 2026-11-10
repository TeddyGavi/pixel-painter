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
