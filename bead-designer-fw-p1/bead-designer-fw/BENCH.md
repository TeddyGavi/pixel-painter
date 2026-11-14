# Bench test on the Freenove FNK0114A (no breadboard)

## Stage 1: keyboard only (nothing wired)

The firmware reads single keys from the USB serial console (`CONFIG_BEAD_SERIAL_KEYS`,
on by default), so the whole UI can be driven from the laptop keyboard:

Keys stand in for the final controls (D-pad + push-encoder + MENU/SIDE):

| Key | Final control | Action |
|---|---|---|
| `w` `a` `s` `d` | D-pad | move the cursor (while browsing: `a`/`d` = previous/next colour family) |
| `x` | D-pad centre | erase |
| `X` | D-pad centre, long-press | eyedropper: colour under the cursor -> selected slot |
| `l` / `j` | encoder turn | next / previous shortlist slot (5 slots on the strip) |
| `k`, `p` or space | encoder press | place a bead in the selected slot's colour |
| `L` / `J` | encoder press + turn | browse the whole catalogue for the selected slot; any other key (or 1.5 s) = release |
| `m` | MENU | menu: Brand, Edit shortlist (browse list or dial a code), Colours used, Export. In menus: `w`/`s` or `j`/`l` move, `k` selects, `x` goes back, `m` closes |
| `o` | SIDE | colours used in the pattern, with counts (`k` on a row loads it into the selected slot) |
| `t` | - | colour test bars for 3 s |
| `v` | - | log the D-pad ladder voltage on every change (breadboard check) |
| `?` | - | print this key list to the log |

With the Stage 2 breadboard wired, the real controls work alongside these keys; until
then the onboard BOOT key is SIDE.


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
3. `l` `l` `l`, `p` places S27 Yellow; `L` `L` `d` browses into the next colour family
   (the strip turns bright, the cursor dims) and `p` ends the browse and places it.
   Compare with `sim/golden/keys.png`, `browse.png`, `family.png`, `eyedrop.png`.
4. Autosave: place a bead and stop typing for ~2 s, then press the board's reset key (EN).
   The bead comes back.

Disable the keys for production with `idf.py menuconfig` → Bead designer.


## Stage 2: all controls on a breadboard

Every final control, wired on an 830-point breadboard to the Freenove's two expansion
connectors. The serial keys from Stage 1 keep working alongside.

### Pin plan

Only five GPIOs reach the Freenove's connectors, so the 5-way D-pad shares one analog pin
through a resistor ladder (each direction gives its own voltage), and SIDE uses the
onboard BOOT key.

| Control | GPIO | Connector pin | Notes |
|---|---|---|---|
| Encoder A | IO35 | P4-2 | input-only: **external 10k pull-up required** |
| Encoder B | IO23 | P3-1 | |
| Encoder push | IO19 | P3-2 | |
| MENU (B3F-1000) | IO18 | P3-3 | |
| D-pad (5-way), all directions | IO27 | P3-4 | resistor ladder, read by the ADC |
| SIDE | IO0 | onboard **BOOT** key | nothing to wire |
| 3V3 / GND | – | P4-1 / P4-4 | the only power pins on P3/P4 |

- **Take the microSD card out.** P3 is the SD card's bus (MOSI/MISO/CLK).
- The D-pad pin (IO27) is on ADC2, which Wi-Fi takes over: **the D-pad does not respond
  while the Wi-Fi export is running** (Export > press). Use MENU to close it. The QR
  export does not use Wi-Fi, so the D-pad keeps working.
- Neither connector carries 5V. Never connect 5V to any of these pins.

Connector pinout, from Freenove's `2.4inch_ESP32-32E_Display_Schematic.pdf`:

| Connector | Pin 1 | Pin 2 | Pin 3 | Pin 4 |
|---|---|---|---|---|
| **P4** "IO port" | 3V3 | IO35 | not connected | GND |
| **P3** "SPI port" | IO23 (SPI_MOSI) | IO19 (SPI_MISO) | IO18 (SPI_CLK) | IO27 (SPI_CS) |

### Parts (from stock)

| Part | Qty | Use |
|---|---|---|
| 10k resistor | 17 | ladder 11, encoder 4, switch pull-ups 2 |
| 10 nF capacitor | 5 | encoder A/B, encoder push, MENU, ladder node |
| PEC11R encoder | 1 | colour encoder with push |
| Adafruit 504 5-way switch | 1 | D-pad |
| B3F-1000 6 mm tactile switch | 1 | MENU |
| Jumper wires | ~20 | |
| The kit's two 4-pin pigtails | 2 | P3 and P4 to the breadboard |

### 1. Get the pigtails onto the breadboard

The wire colours aren't documented, so find pin 1 by measurement:

1. Plug a pigtail into **P4**, power the board over USB, and find the two wires that read
   3.3 V across them. The one on the red probe is **pin 1 (3V3)**, the one on the black
   probe **pin 4 (GND)**. The wire next to 3V3 is **pin 2 (IO35)**; pin 3 is unused.
2. Unplug USB. The P3 pigtail is the same cable, so its wire in P4's pin-1 position is
   **P3 pin 1 (IO23)**, then IO19, IO18, and the pin-4 position is **IO27**. Check that
   both cables have the same colour order.
3. Bare wire ends: tin them, or solder each to a header pin, so they push firmly into the
   breadboard.
4. Wire P4-1 (3V3) to the breadboard's **+ rail**, P4-4 (GND) to the **- rail**. Bridge the
   rails on both sides if you need power on both.

### 2. Encoder (PEC11R)

The 3-pin row is **A, C, B** with **C (common) in the middle**; the 2-pin row on the other
side is the push switch. The two mounting lugs keep it out of a breadboard: bend them
flat or snip them (they are only mechanical).

```
            3V3 ──[10k]──┬── encoder A
                         └──[10k]──┬── IO35 (P4-2)
                                   └──[10 nF]── GND
            3V3 ──[10k]──┬── encoder B
                         └──[10k]──┬── IO23 (P3-1)
                                   └──[10 nF]── GND
            encoder C ── GND
            push pin 1 ── GND
            push pin 2 ──┬── IO19 (P3-2)
            3V3 ──[10k]──┤
                         └──[10 nF]── GND
```

### 3. MENU button (B3F-1000)

Seat it across the breadboard's centre channel. Its four legs are two internally joined
pairs; use two **diagonally opposite** legs so you're sure to go through the switch.

```
            leg 1 ── GND
            leg 2 ──┬── IO18 (P3-3)
            3V3 ──[10k]──┤
                         └──[10 nF]── GND
```

### 4. D-pad (Adafruit 504 5-way) as a resistor ladder

The 5-way presses into the breadboard with a little bending of the legs. It has six pins:
a **common** plus one per direction (the control-board notes give COM = pin 4, centre =
pin 6, but the physical direction of each is unverified). Each direction only ever
connects **to the common**, never to another direction's pin.

Identify the pins with **volts**, not ohms (many meters, e.g. the Fluke 323, top out at
4 kOhm and can't measure these resistors at all; continuity mode works but needs firm
presses while holding both probes):

1. Seat the 5-way across rows of its own, so each pin has its own row.
2. Put a 10k from the **+ rail** to one pin's row ("test row"), and a jumper from another
   pin's row to the **- rail** (your guess at the common).
3. Power up and measure the test row against GND: **3.3 V** idle, **0 V** while you press
   the direction that connects that pin to the common.
4. Press each of the 5 directions. If one of them drops the test row to 0 V, you have the
   common right and have found that pin's direction. If none does, move the jumper to the
   next pin and repeat. Then move the 10k along to the other pins.

Once the ladder is built, the board can do the measuring for you: in `idf.py monitor`
press **`v`**, and every change in the D-pad voltage is logged, e.g.
`dpad: GPIO27 = 1098 mV -> DOWN`. Press `v` again to stop.

All five directions feed one node, which goes to IO27. A 20k pull-up holds the node at
3.3 V; each press pulls it towards GND through a different resistance:

```
            3V3 ──[10k]──[10k]──┬──────────── IO27 (P3-4)      pull-up = 20k
                                ├──[10 nF]── GND
                                ├──────────────────── centre ┐
                                ├──[10k ∥ 10k]─────── up     │  (5k: two 10k side by side)
                                ├──[10k]───────────── down   ├─ switch ── common ── GND
                                ├──[10k]─[10k]─────── left   │  (20k: two in series)
                                └──[10k]─[10k]─[10k]─[10k]── right ┘  (40k: four in series)
```

| Press | Resistance to GND | Voltage at IO27 | Firmware reads it as |
|---|---|---|---|
| nothing | – | 3.30 V | above 2.75 V |
| centre | 0 Ω (jumper) | 0.00 V | below 0.33 V |
| up | 5k | 0.66 V | 0.33–0.88 V |
| down | 10k | 1.10 V | 0.88–1.37 V |
| left | 20k | 1.65 V | 1.37–1.92 V |
| right | 40k | 2.20 V | 1.92–2.75 V |

Each level is at least 0.44 V from its neighbours, so normal resistor tolerance (5%) and
ADC error are no problem. If a direction comes out wrong, swap that direction's resistor
chain rather than touching the firmware.

### 5. Check before powering up

1. With USB unplugged, use continuity mode to confirm **3V3 and GND are not shorted** (no
   beep).
2. With USB plugged in and nothing pressed, measure IO27 against GND: about **3.3 V**. Then
   press each direction and compare with the table above (±0.1 V), or press `v` in the
   monitor and read the voltages off the log instead.
3. Encoder and switch circuits: check with **volts**, powered. Each pull-up node (row
   where the 10k from 3V3 meets the encoder/switch pin) reads **3.3 V** idle, and
   **0 V** while that contact is closed (switch pressed, or the encoder sitting mid-click).

### 6. Flash and test

```sh
ls /dev/ttyUSB*                          # CH340C, usually /dev/ttyUSB0
. ~/esp/esp-idf/export.sh
cd bead-designer-fw-p1/bead-designer-fw
idf.py -p /dev/ttyUSB0 flash monitor     # Ctrl-] quits
```

Each input prints one log line (`ui: ev=...`). Checklist:

1. The boot log shows `input: D-pad ladder on GPIO27 (calibrated)`.
2. **Nothing pressed: no `ev=` lines.** (Lines repeating on their own = a floating pin; see
   Troubleshooting.)
3. **D-pad:** each direction gives one `ev=UP/DOWN/LEFT/RIGHT` and moves the cursor; holding
   one auto-repeats. Centre tap = `ev=CENTER` (erase); centre held 0.6 s =
   `ev=CENTER_LONG` (eyedropper).
4. **Encoder:** one detent = one `ev=ENC_CW`/`ENC_CCW` (the strip frame moves one slot;
   if clockwise goes backwards, swap the A and B wires). Press = `ev=ENC_PRESS` (place).
   Hold the push and turn = `ev=BROWSE_CW/CCW` (the strip shows the catalogue), release =
   `ev=BROWSE_END`. While holding, the D-pad left/right jump colour families.
5. **MENU** opens and closes the menu; **BOOT** (SIDE) opens "Colours used".
6. Autosave: place a bead, wait ~2 s, press the board's reset key (EN): the bead and your
   5 slots come back.

The simulator drives these same pins: `sim/scripts/hwpad.txt` steps the ladder voltages
(`adc 27 <volts>`), the push (`press sw`), MENU (`press menu`) and SIDE (`press boot`).

### Troubleshooting

| Symptom | Likely cause |
|---|---|
| `ev=CENTER_LONG` / `CENTER` repeating with nothing pressed | IO27 pulled low: ladder pull-up missing, or the centre wire is shorted to GND |
| A D-pad press shows as a different direction | wrong resistor chain on that direction, or the common pin isn't the one on GND |
| D-pad dead, everything else fine | P3-4 wire not on the node; or Wi-Fi export is running (ADC2 busy) |
| `ev=ENC_CW/CCW` spam with the knob still | IO35 floating: the A pull-up is missing |
| Encoder moves only one way, or every other detent | A or B not connected; C not on GND |
| Two steps per detent, or skips | contact bounce: fit the 10 nF caps, keep wires short |
| Encoder press does nothing / places when turning | push wired to the wrong pins (use the 2-pin side) |
| Board won't start after reset while BOOT is held | expected: BOOT held at reset = download mode. Let go and reset |
| Board resets or won't boot with the breadboard connected | 3V3 shorted to GND somewhere |
| Nothing on P3 works at all | microSD card still inserted, or the P3 pigtail is in the wrong connector |
