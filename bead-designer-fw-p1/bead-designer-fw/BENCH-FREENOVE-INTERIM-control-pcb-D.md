# INTERIM BENCH TEST ONLY: control PCB Rev B (variant D) on the Freenove FNK0114A

> **This is a temporary bench setup, not the product build.**
> - It lets you test an assembled control PCB D on the Freenove while the Waveshare 2.41 is on its way.
> - **R8–R12 are left OFF** for this test, and you must fit them before the Waveshare build (see "Before the Waveshare build").
> - **Never plug the 34-way ribbon into the Freenove.** Everything here uses single jumper wires.
> - The case does not fit the Freenove; this is a loose-board test.
>
> Written 2026-11-15. The breadboard-only setup is in [BENCH.md](BENCH.md) (Stage 2). This file reuses its D-pad resistor ladder and pin plan.

## Why this setup

- **Too few pins.** The Freenove brings only five GPIOs to its connectors (P3 and P4) plus the onboard BOOT key. The control PCB has 10 signals.
- **Encoder and MENU:** these wire straight across, using the pull-ups and RC filters already on the PCB.
- **SIDE:** stays on the Freenove's **BOOT** key, as in BENCH.md.
- **D-pad:** goes through the same resistor ladder as BENCH.md, built on a breadboard, into one ADC pin (IO27).
- **Why R8–R12 must be off:** they are the PCB's five D-pad pull-ups, and they would shift the ladder voltages. With them fitted, UP reads about 1.84 V, which the firmware decodes as LEFT. The 10 nF caps C6–C10 can stay; they don't affect the ladder.

**Firmware:** the normal Freenove build, no changes. It already expects the ladder on IO27, the encoder on IO35/IO23/IO19, MENU on IO18 and SIDE on the BOOT key.

## 1. Assemble the PCB with R8–R12 left off

Build the board per `kicad-control-revB/kicad-control-README.md`, with these exceptions:

| Ref | Part | Fit now? | Role |
|---|---|---|---|
| R8 | 10k | **NO** | NAV_UP pull-up |
| R9 | 10k | **NO** | NAV_DOWN pull-up |
| R10 | 10k | **NO** | pull-up on net NAV_LEFT (SW3 pin 5 = physical **right**) |
| R11 | 10k | **NO** | pull-up on net NAV_RIGHT (SW3 pin 2 = physical **left**) |
| R12 | 10k | **NO** | NAV_C (centre) pull-up |
| everything else | | yes | including C6–C10 |

Put a strip of tape on the board marked **"R8–R12 NOT FITTED: Freenove bench"** so it can't be mistaken for a finished board.

## 2. Getting wires onto J1

J1 is the 2x17 shrouded header on the **bottom** of the PCB. Pin 1 is the **square pad**, marked "1" on the bottom silkscreen. Odd pins (1, 3 … 33) are the row nearest the board edge; even pins (2, 4 … 34) are the inner row. Pin numbers count along the header away from pin 1.

Two ways to connect:
- **(a) Single female Dupont jumpers** straight onto the J1 pins. Single housings fit side by side inside the shroud.
- **(b) The H3DDS ribbon cable on J1**, with male Dupont pins pushed into the free socket at the other end. Pin 1 is the red stripe.

## 3. Wiring table

The Freenove connectors are P4 (3V3 / IO35 / NC / GND) and P3 (IO23 / IO19 / IO18 / IO27). Find the pigtail pin 1 the same way as in BENCH.md step 1.

| J1 pin | PCB net | Goes to | Notes |
|---|---|---|---|
| **16** | +3V3 | **P4-1** (3V3) | 30 and 32 are also 3V3; one wire is enough |
| **1** | GND | **P4-4** (GND) | 18 and 34 are also GND. **Not 4 or 33**: they are unconnected on this board |
| 13 | ENC_A | **P4-2** (IO35) | IO35 is input-only. The PCB's R1 + R2 give the required external pull-up |
| 15 | ENC_B | **P3-1** (IO23) | |
| 24 | ENC_SW | **P3-2** (IO19) | |
| 6 | BTN_MENU | **P3-3** (IO18) | |
| 8 | BTN_SIDE | **leave unconnected** | SIDE = the Freenove BOOT key |
| 3 | NAV_C | ladder: **centre** branch (0 Ω) | |
| 19 | NAV_UP | ladder: **up** branch (5k) | |
| 21 | NAV_DOWN | ladder: **down** branch (10k) | |
| **25** | NAV_RIGHT *(net name)* | ladder: **left** branch (20k) | SW3 pin 2 = physical **left**. The net name is crossed; see the README |
| **23** | NAV_LEFT *(net name)* | ladder: **right** branch (40k) | SW3 pin 5 = physical **right** |
| ladder node | – | **P3-4** (IO27) | |

## 4. The ladder (on the breadboard)

This is the BENCH.md ladder, except each branch now ends at a J1 pin instead of at the loose switch. SW3 on the PCB connects that J1 pin to GND when the direction is pressed, because pin 4 is COM to GND on the board.

```
3V3 ──[10k]──[10k]──┬──────────────────────── IO27 (P3-4)       pull-up = 20k
                    ├──[10 nF]── GND
                    ├─────────────────────── J1-3   (centre)     0 Ω
                    ├──[10k ∥ 10k]────────── J1-19  (up)         5k
                    ├──[10k]──────────────── J1-21  (down)       10k
                    ├──[10k]─[10k]────────── J1-25  (left)       20k
                    └──[10k]─[10k]─[10k]─[10k]── J1-23 (right)   40k
```

Use the breadboard's 3V3 and GND rails, fed from P4-1 and P4-4 as in BENCH.md. The PCB's GND (J1-1) must share that GND.

Expected voltages at IO27 (same as BENCH.md):

| Press | Volts | Firmware band |
|---|---|---|
| nothing | 3.30 | > 2.75 |
| centre | 0.00 | < 0.33 |
| up | 0.66 | 0.33–0.88 |
| down | 1.10 | 0.88–1.37 |
| left | 1.65 | 1.37–1.92 |
| right | 2.20 | 1.92–2.75 |

## 5. Checks before power

1. **Unpowered:** use continuity between J1-16 (3V3) and J1-1 (GND). There must be **no beep**.
2. **Confirm R8–R12 are really empty:** J1-19, 21, 23, 25 and 3 must **not** read about 10k to J1-16.
3. **Powered, nothing pressed:**
   - IO27 reads about **3.3 V**.
   - J1-24 (ENC_SW), J1-6 (MENU) and J1-8 (SIDE, even though it isn't wired out) read about 3.3 V.
   - J1-13 and J1-15 read 3.3 V or 0 V depending on where the encoder rests. Turn half a click to see both.
4. **Press each D-pad direction** and compare with the table (±0.1 V), or press **`v`** in `idf.py monitor` to log the voltages.
   - Left and right are the ones to watch: they check the B = left / D = right reading of the Alps drawing.
   - If they come out swapped, the board is wired as drawn and the Alps reading is what's wrong. **Tell me before the Waveshare build**, because `board.h` would then need to swap back.

## 6. Flash and test

Same as BENCH.md step 6: `idf.py -p /dev/ttyUSB0 flash monitor`, then run through its checklist (D-pad events, encoder detents, MENU, SIDE on BOOT).

Same limits as the breadboard:
- No microSD card while P3 is in use.
- The D-pad is dead during the Wi-Fi export, because IO27 is on ADC2.

## Before the Waveshare build (undo the interim setup)

1. Remove all jumper wires and the breadboard ladder.
2. **Fit R8–R12 (10k each).** They are bottom-side axial parts: insert them from the bottom and solder on the top. Their top-side pads are clear of SW3, ENC1 and SW1, so they can be fitted after the rest of the board.
3. Remove the "NOT FITTED" tape.
4. With power applied through J1-16/J1-1 only, check that J1-3, 19, 21, 23 and 25 read **3.3 V** idle and **0 V** when the matching direction is pressed.
5. Then use the ribbon to the Waveshare J8. Pin 1 is the red stripe; check it by hand, because the Waveshare header is unshrouded.
