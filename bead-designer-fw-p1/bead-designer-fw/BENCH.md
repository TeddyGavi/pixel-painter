# Bench test on the Freenove FNK0114A (no breadboard)

## Stage 1: keyboard only (nothing wired)

The firmware reads single keys from the USB serial console (`CONFIG_BEAD_SERIAL_KEYS`,
on by default), so the whole UI can be driven from the laptop keyboard:

| Key | Action |
|---|---|
| `d` / `a` | left knob CW / CCW: next / previous cell (picker: next / previous colour) |
| `s` / `w` | right knob CW / CCW (picker: next / previous colour) |
| space or `p` | place / confirm colour (right push) |
| `x` | erase / cancel the picker (left push) |
| `m` | MENU: colour picker on the strip under the grid; `m` again confirms |
| `o` | SIDE: settings |
| `t` | colour test bars for 3 s |
| `?` | print this key list to the log |

The BOOT key still works as "place".

```sh
ls /dev/ttyUSB* /dev/ttyACM*            # the board's CH340C shows up as /dev/ttyUSB0
sudo usermod -aG uucp $USER             # Arch: serial-port access. Log out and back in once.
. ~/esp/esp-idf/export.sh
cd bead-designer-fw-p1/bead-designer-fw
idf.py -p /dev/ttyUSB0 flash monitor    # type keys here; Ctrl-] quits
```

Checks:
1. The boot screen matches `sim/golden/boot.png`, and the log shows the `keys:` help line.
2. `t` shows red, green, blue, white and black bars from top to bottom (`sim/golden/bars.png`).
   For fixes if they don't, see check 6 under "Bench checks" below.
3. `m`, `d` `d` `d` `d`, `p` selects Magenta (the strip frames it and shows "Magenta 0"),
   and `p` then places it (count 1). Compare with `sim/golden/keys.png` and `picker.png`.
4. Autosave: place a bead, wait more than 2 s, press `d`, then press the board's reset key
   (EN). The bead comes back.

Disable the keys for production with `idf.py menuconfig` → Bead designer.

## Stage 2: wiring the encoder

One PEC11R encoder is soldered "dead-bug" style onto two of the kit's 4-pin pigtail
cables. The onboard BOOT key is the "place" button.

## Connector pinout (Freenove `2.4inch_ESP32-32E_Display_Schematic.pdf`)

| Connector | Pin 1 | Pin 2 | Pin 3 | Pin 4 |
|---|---|---|---|---|
| **P4** "IO port" | **3V3** | **IO35** (encoder A) | not connected | **GND** |
| **P3** "SPI port" | SPI_MOSI (IO23) | SPI_MISO (IO19) | SPI_CLK (IO18) | **SPI_CS = IO27** (encoder B) |

- P4 supplies the harness with 3V3, GND and encoder A.
- From P3, only pin 4 is used. **Pins 1–3 are the SD-card bus, so cut them back and insulate them.**
- Neither connector carries 5V.
- GPIO35 is input-only and has no internal pull-up. The external 10k below is required, or the pin floats.

## Finding the right pigtail wires

The wire colours aren't documented, so identify them by measurement:

1. Plug a pigtail into **P4** and power the board over USB.
2. With a multimeter, find the two wires that read 3.3 V across them. The wire on the
   black probe is **GND** (pin 4) and the one on the red probe is **3V3** (pin 1). The wire
   next to 3V3 is **IO35** (pin 2). The remaining one is pin 3 (unused).
3. The P3 pigtail is the same cable type, so the wire in the GND position on P4 is the
   **IO27** position (pin 4) on P3. Check visually that the cables are wired the same way
   (same colour order at the same housing position).
4. Unplug USB before soldering.

## Circuit

The filter is the same one the control PCB uses. Build it once for A and once for B:

```
3V3 ──[10k pull-up]──┬── encoder A (raw)
                     └──[10k series]──┬── P4 pin 2 (IO35)
                                      └──[10 nF]── GND

3V3 ──[10k pull-up]──┬── encoder B (raw)
                     └──[10k series]──┬── P3 pin 4 (IO27)
                                      └──[10 nF]── GND

encoder C (middle pin of the 3-pin row) ── GND
```

Parts: 4 × 10k and 2 × 10 nF.

**PEC11R pins:** the 3-pin row is A, C, B, with **C (common) in the middle**. The 2-pin row
on the other side is the push switch. Leave it unconnected, since there's no free GPIO for it
and BOOT does "place". The two mounting lugs stay unconnected too.

## Dead-bug build (soldered)

1. Tin the encoder pins. Solder a short GND wire to **C**.
2. Solder one leg of a 10k pull-up to **A** and one to **B**. Twist the other two legs together
   with a short **3V3** wire and solder them.
3. Solder a 10k series resistor to **A** (and one to **B**). Their free ends are the signal nodes.
4. At each signal node, solder one leg of a 10 nF cap. Join the other cap legs into the
   **GND** wire.
5. Solder the pigtails on:
   - P4 pin 2 (IO35) to the A signal node.
   - P3 pin 4 (IO27) to the B signal node.
   - P4 pin 1 (3V3) to the 3V3 wire.
   - P4 pin 4 (GND) to the GND wire.
6. Cover every joint with heat-shrink or tape. **Before plugging in**, use the meter's
   continuity mode to confirm that 3V3 and GND are *not* shorted.

**No-solder fallback:** encoder C to GND, A to IO35 with **one** 10k pull-up from A to 3V3,
and B straight to IO27 (the pulse-counter driver enables IO27's internal pull-up). Use twisted
leads or jumper wires. The firmware's PCNT glitch filter handles some bounce, but expect the
odd skipped or doubled detent.

## Flash and monitor

```sh
ls /dev/ttyUSB* /dev/ttyACM*            # CH340C USB-serial; usually /dev/ttyUSB0
sudo usermod -aG uucp $USER             # Arch: serial-port access. Log out and back in once.
. ~/esp/esp-idf/export.sh
cd bead-designer-fw-p1/bead-designer-fw
idf.py set-target esp32                 # only if build/ isn't an esp32 build already
idf.py -p /dev/ttyUSB0 flash monitor    # Ctrl-] quits the monitor
```

The board has auto-download through DTR/RTS, so no buttons are needed to flash. Don't hold
BOOT while the board powers up or resets, because GPIO0 is a strapping pin.

Each input prints one line, showing the cursor position *before* the event:
```
I (2006) ui: ev=ENC_L_CW mode=0 cur=4,4
I (2499) ui: ev=PUSH_R mode=0 cur=7,4
```

## Bench checks

1. **Boot screen** matches `sim/golden/boot.png`: a dark 9x9 grid, with a white-framed centre cell.
2. **Quiet line.** With the knob untouched, no `ev=` lines appear.
3. **One detent CW** gives exactly one `ev=ENC_L_CW`, and the cursor moves one cell right
   (wrapping row by row). If CW goes left, swap the A and B wires (or the two
   signal nodes).
4. **BOOT** gives `ev=PUSH_R`, and a white bead appears.
5. **Autosave.** Place a bead, wait more than 2 s, turn the knob one detent, then press the
   reset key (EN). The bead comes back after reboot.
6. **Colours.** The grid should be dark with a white bead. If it's light (inverted), change
   `esp_lcd_panel_invert_color(s_panel, true)` to `false` in `main/board/board_freenove.c`.
   If colours other than white or black look wrong (red and blue swapped), change
   `LCD_RGB_ELEMENT_ORDER_RGB` to `_BGR`. Mirror either change in the simulator's
   `~/esp32sim/esp32/src/board/freenove.rs` (`PANEL_NEEDS_INVON` / `PANEL_IS_BGR`).

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Constant `ev=` spam with the knob still | IO35 floating: missing or broken pull-up on A |
| No `ev=` at all when turning | wrong pigtail wire, C not on GND, or no 3V3 on the pull-ups |
| Moves only every other detent, or one direction only | one of A/B not connected |
| Two moves per detent, or random skips | bounce: fit the 10 nF caps, shorten the wires |
| Board resets or won't boot while the harness is connected | short between 3V3 and GND; unplug and recheck |
