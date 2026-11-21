# Bench test on the Waveshare 2.41" (breadboard, before the control PCB)

This guide wires every final control to the **Waveshare ESP32-S3-Touch-AMOLED-2.41
Rev 2.0** on an 830-point breadboard, through its 2x17 header **J8**. J8 is a
**female** socket, so the male ends of jumper wires plug straight into it: no ribbon
needed (the H3DDS ribbon can't mate with it at all). Each control
gets its own pin and uses the same pull-ups and filters as control PCB D, so the
firmware runs exactly as it will with the real board, with no changes.

The Freenove bench is in [BENCH.md](BENCH.md); it needs a resistor ladder because that
board has only 5 free pins. The interim control-PCB-on-Freenove setup is in
[BENCH-FREENOVE-INTERIM-control-pcb-D.md](BENCH-FREENOVE-INTERIM-control-pcb-D.md).

## Pin plan

J8 numbering follows Waveshare's schematic: odd pins 1, 3 ... 33 in one row, even pins
2, 4 ... 34 in the other. **Find pin 1 on your board before wiring** (see step 1).

| Control | J8 pin | Signal | Circuit on the breadboard |
|---|---|---|---|
| Encoder A | **13** | GPIO7 | 10k pull-up, 10k series, 10 nF to GND (RC filter) |
| Encoder B | **15** | GPIO8 | same as A |
| Encoder push (place; hold + D-pad = paint brush) | **24** | GPIO42 | 10k pull-up, 10 nF to GND |
| D-pad up | **19** | GPIO38 | 10k pull-up, 10 nF to GND |
| D-pad down | **21** | GPIO39 | same |
| D-pad **left** | **25** | GPIO41 | same |
| D-pad **right** | **23** | GPIO40 | same |
| D-pad centre (erase; hold = eyedropper) | **3** | GPIO1 | same |
| MENU | **6** | EXIO5 (TCA9554) | 10k pull-up, 10 nF to GND |
| SIDE | **8** | EXIO6 (TCA9554) | same |
| 3V3 | **16** | | the breadboard **+ rail** |
| GND | **1** (or 18, 34) | | the breadboard **- rail** |

- **Every control closes to GND when pressed.**
- **Left is pin 25 and right is pin 23.** Control PCB D has those two net names
  crossed, and the firmware (`board.h`) is set for the real switch directions. On the
  breadboard, simply follow this table.
- **Never connect anything to pin 2 (BAT), pin 31 (USB 5 V), or any 5 V.** Leave those socket holes empty.
  Leave pins 4 and 33 unused, as on the PCB. They are GND on the Waveshare, but if a
  plug in J8 is ever fitted backwards they land on 5 V and BAT.
- **The ESP32 pull-ups are switched on in firmware,** and the expander has ~100k
  pull-ups of its own. The external 10k and 10 nF copy the PCB and make presses clean;
  don't skip them on the encoder.

## Parts (from the Freenove bench stock)

| Part | Qty | Use |
|---|---|---|
| PEC11R encoder with push | 1 | colour knob, place, paint brush |
| Adafruit 504 five-way switch (Alps SKQUCAA010) | 1 | D-pad |
| B3F-1000 6 mm tactile switch | 2 | MENU, SIDE |
| 10k resistor | 12 | 2 encoder pull-ups + 2 series, 8 switch pull-ups |
| 10 nF capacitor | 10 | encoder A/B, 8 switch lines |
| Male-to-male jumper wires | ~14 | J8 to the breadboard (female-to-male also works: male end in J8, a header pin in the female end) |

## 1. Find pin 1 and power the rails

1. **Find pin 1 on J8.** With USB unplugged, read the silkscreen beside J8: Waveshare
   labels every hole (GND, 3V3, 1, 7, 8 ...). Pins 1 and 2 are the pair labelled
   **GND / BAT**, at the end of J8 nearest the PWR / BOOT / RST keys; pin 1 is the GND. Cross-check against the table: J8-3 is labelled 1
   (GPIO1), J8-16 is 3V3.
2. **Check it with a meter.** Plug USB in and measure DC volts between hole 16 and
   hole 1 (a jumper's male pin in each hole, probes on the jumpers): about **3.3 V**
   means your numbering is right. Anything else: stop and recount.
3. Unplug USB. Plug the **male ends** of jumpers into J8: **hole 16 to the + rail**,
   **hole 1 to the - rail**, then one jumper per control from the table above. Bridge
   both sides of the breadboard if you need power on both. Label each jumper; the holes
   are easy to miscount.

**Don't push anything into holes 2 (BAT) or 31 (USB 5 V).** A jumper there touching
a signal line can damage the ESP32.

## 2. Encoder (PEC11R)

The 3-pin row is **A, C, B**, with the common **C in the middle**. The 2-pin row on the
other side is the push switch. Bend or snip the two mounting lugs so it sits in the
breadboard.

```
            3V3 ──[10k]──┬── encoder A
                         └──[10k]──┬── J8-13 (GPIO7)
                                   └──[10 nF]── GND
            3V3 ──[10k]──┬── encoder B
                         └──[10k]──┬── J8-15 (GPIO8)
                                   └──[10 nF]── GND
            encoder C ── GND
            push pin 1 ── GND
            push pin 2 ──┬── J8-24 (GPIO42)
            3V3 ──[10k]──┤
                         └──[10 nF]── GND
```

## 3. D-pad (Adafruit 504 five-way switch)

It presses into the breadboard with a little bending of the legs. Seat it across rows
of its own. Pinout per the Alps SKQUCAA010 drawing, looking at the top:
pin 1 = up, pin 2 = left, pin 3 = down, **pin 4 = common**, pin 5 = right,
pin 6 = centre. Each direction only connects to the common.

```
            switch pin 4 (common) ── GND

            each direction pin ──┬── its J8 pin      up: pin 1 -> J8-19
            3V3 ──[10k]──────────┤                   left: pin 2 -> J8-25
                                 └──[10 nF]── GND    down: pin 3 -> J8-21
                                                     right: pin 5 -> J8-23
                                                     centre: pin 6 -> J8-3
```

If a direction comes out wrong in the test (step 6), swap the two J8 wires. The
breadboard is the place to fix it; leave the firmware alone.

## 4. MENU and SIDE (B3F-1000)

Seat each switch across the breadboard's centre channel. Its four legs are two
internally joined pairs, so use two **diagonally opposite** legs to be sure you go
through the switch.

```
            leg 1 ── GND
            leg 2 ──┬── J8-6 (EXIO5, MENU)      second switch: J8-8 (EXIO6, SIDE)
            3V3 ──[10k]──┤
                         └──[10 nF]── GND
```

These two go to the board's I/O expander, which the firmware reads over I2C every
5 ms. They work exactly like the other buttons.

## 5. Check before powering up

1. **USB unplugged:** in continuity mode, check that **3V3 and GND are not shorted**
   (no beep). Check that **nothing touches J8 pins 2 or 31**.
2. **USB plugged in, nothing pressed:**
   - every switch line reads about **3.3 V** against GND;
   - J8-13 and J8-15 (encoder A/B) read 3.3 V or 0 V depending on where the knob
     rests; turn it half a click to see both.
3. **Press each control:** its line drops to **0 V**.

## 6. Flash and test

```sh
hw/run.sh flash                          # or: idf.py -B build-s3 -D SDKCONFIG=sdkconfig.s3 -p /dev/ttyACM0 flash
newgrp uucp                              # the port is root:uucp
. ~/esp/esp-idf/export.sh
idf.py -B build-s3 -D SDKCONFIG=sdkconfig.s3 -p /dev/ttyACM0 monitor     # Ctrl-] quits
```

Every input prints one line (`ui: ev=...`). The serial keys keep working alongside
the real controls (`?` lists them). Checklist:

1. **Nothing pressed: no `ev=` lines.** Lines repeating on their own mean a floating
   line; see Troubleshooting.
2. **D-pad:**
   - each direction gives one `ev=UP/DOWN/LEFT/RIGHT` and moves the cursor the matching
     way on screen;
   - holding a direction auto-repeats;
   - a centre tap gives `ev=CENTER` (erase);
   - holding centre for 0.6 s gives `ev=CENTER_LONG` (eyedropper).
3. **Encoder:**
   - one detent gives one `ev=ENC_CW` / `ENC_CCW` (next/previous shortlist colour);
   - a press and release gives `ev=ENC_PRESS` (places a bead);
   - turning while pressed gives `ev=BROWSE_CW/CCW`, then `BROWSE_END` on release
     (browse the catalogue).
4. **Paint brush:**
   - hold the encoder push and move with the D-pad: you see `ev=PAINT` after each move,
     the start cell and every cell you pass get the colour, and holding a direction
     paints a line;
   - releasing places nothing extra.
5. **MENU** gives `ev=MENU` (opens the menu). **SIDE** gives `ev=SIDE` (colours in the
   pattern).
6. **PWR on the board:**
   - a short press gives sleep (the screen goes dark), and any control wakes it;
   - holding 1.5 s shuts down (on USB a soft off: press PWR to wake).
7. **Screen saver:** after 1 minute idle (the default; it can be changed in Settings) the dot screen saver starts, and any control
   wakes it without acting.

## Troubleshooting

- **A direction or the encoder fires on its own:** the line is floating. Check its
  10k pull-up goes to the **+ rail** and the switch common or encoder C goes to the
  **- rail**.
- **The encoder skips or double-steps:** check the RC filter on A and B (10k series and
  10 nF to GND, at the J8 side).
- **Encoder direction reversed:** swap the A and B wires (J8-13 and J8-15).
- **D-pad direction wrong:** swap the two J8 wires for those directions.
- **MENU/SIDE do nothing:** the boot log must show `TCA9554 ok at 0x20` (or press `?`;
  the `board:` line says the same). Check the wires are on J8-6 and J8-8, not 5 and 7.
- **Nothing on J8 responds at all:** recheck pin 1 (step 1). The holes may be counted
  from the wrong end.
- **A jumper is loose in J8:** the socket is made for 0.64 mm square pins, and some
  jumper pins are thinner. Bend the pin slightly, or use jumpers with square pins.
