# Layout Rev B — flat front, Waveshare on the front panel, control PCB + ribbon

Decided 2026-11-08 by the buyer. Supersedes layout-revA.md (carrier PCB retired).
Units mm. TBV = verify on real parts. Waveshare data: ws241-mech/ws241-mechanical.md.

## Decisions
1. **Flat front panel**, no screen bump. Knobs/stick may protrude up to ~18 mm.
2. **Waveshare screwed to the front shell** (its 4 M2 holes onto front-shell bosses), **glass flush** with the front face (glass sits in a pocket, retained from behind by the board/bosses).
3. **Controls on a separate control PCB** behind the panel, below the screen; linked to the Waveshare's 2x17 2.54 mm header by the **J8 adapter PCB** (2x17 male header into the female J8; FH12-16S FPC connector on its back) and a **30 mm 16-way 0.5 mm same-side FFC** to J1 (FH12-16S, bottom side, top edge) on the control PCB rev B5. The IDC ribbon planned earlier can't mate with the female J8.
4. **Two control-PCB variants**, ordered together:
   - **J**: thumbstick with push (C&K THB001P) = cursor + place/erase; PEC11R colour knob (Kilo OEJNI-75-4-7, Ø19.05); MENU (B3F-1000, 6 mm; was B3F-4055) front; SIDE (C&K PTS645VL31-2 LFS right-angle) right edge.
   - **D**: 5-way switch (Adafruit 504 = Alps SKQUCAA010 footprint; part is end-of-life, stock limited — keep the footprint replaceable) + same knob, MENU, SIDE.
5. Waveshare rotated 180° (keys on the device TOP edge, USB-C LEFT side), as in Rev A.

## Frames
XY front view (X right, Y up), origin = bottom-left Waveshare hole after rotation. KiCad Y = -Y.
Z = depth from the FRONT FACE of the case (= glass front face, flush), positive towards the back.

## Waveshare (unchanged geometry from Rev A)
Holes (0,0) (36.5,0) (0,51) (36.5,51); lens X -4.11..40.61, Y -5.28..56.28; PCB rear face Z 6.0;
rear parts to Z 9.5; keys top edge X 9.75/18.25/26.75 at Z ≈ 6.9; USB-C left edge Y 25.5, Z ≈ 7.8 (shell 6.0–9.3).
J8 header (**female** socket, 2.54, 2x17) on the RIGHT: columns X 32.73 / 35.27, rows Y 5.18..45.82, socket Z 6.0–11.0 (user caliper 2026-11-19; drawing 11.5).
Link (rev B5): adapter header plastic Z 11.0..13.5, adapter PCB 13.5..15.1 (X 26.5..41.5, Y -4.6..47.5), J2 on its back 15.1..17.1, FFC Z-fold to J1 (control back face, Z 10.1..12.1, X 27.5..40.5, Y -7..-12.3). Case depth 21.5.

## Case targets
| Item | Value |
|---|---|
| Outer width | 54 (allowed 52–56); X -8.75 .. 45.25 |
| Outer height | ~110; Y -50 .. 60.3 |
| Front panel | flat, 2.0 thick: Z 0..2.0; lens pocket 44.72+0.3 x 61.56+0.3, depth 0.7 + gasket TBV |
| Depth | target ≤ 23 (back wall outer); battery behind the Waveshare |
| Battery 102050 | 10 x 20 x 50, behind the Waveshare beside the header column: X ≈ 4..24 (along Y, e.g. Y 0..50), Z 9.8..19.8 |

## Control PCB (both variants, same outline)
- Outline: X -6.5 .. 43.0, Y -48.0 .. -7.0 (TBV, agents may adjust ≤ 2 mm), R2.
- Mounting: PCB **top (component) side faces the front panel**. PEC11R mounting surface (body top) touches the panel inner face (Z 2.0) → PCB top Z = 2.0 + encoder body height 6.5 (TBV) = **8.5**, PCB bottom Z 10.1. The encoder nut on the front clamps the board via the bushing; plus 2–3 M2 screws into front-shell bosses.
- Controls (front view XY):
| Item | Centre | Notes |
|---|---|---|
| Cursor control (J: THB001P / D: 5-way) | (5.0, -34.0) | J: stick protrudes through a panel hole sized to its cap travel; D: printed cross cap on the 5-way actuator through a cross-shaped opening |
| Colour knob encoder | (31.5, -34.0) | knob Ø19.05 → X 21.97..41.03 |
| MENU B3F-1000 (6 x 6, H 4.3) | (18.25, -18.1) | Rev B.1 (was B3F-4055 at (18.25, -14.0)). Printed Ø9 cap on an axis at (18.25, -16.6), nub on the plunger |
| SIDE PTS645VL31 right-angle | right PCB edge, Y -23.0 (Rev B.1; was -24.0), actuator tip X 43.20 | actuator +X through the right wall via printed cap |
| 2x17 shrouded header | on the **bottom** side, near the top edge of the PCB, columns along X, ribbon runs up to the Waveshare header | pin-for-pin with J8 |
- Height budget on the top side: ≤ 6.0 everywhere except the encoder, MENU switch, stick/5-way and SIDE.
- 10 nF caps: KEMET C317C103K5R5TA, lead spacing 5.08 → footprint P5.00 (not 2.50).

## Pin use (J8 pin numbers; Waveshare V2)
| Signal | J (THB001P) | D (5-way) |
|---|---|---|
| Stick X / 5-way UP | GPIO7 (J8-13), ADC | GPIO38 (J8-19) |
| Stick Y / 5-way DOWN | GPIO8 (J8-15), ADC | GPIO39 (J8-21) |
| 5-way LEFT | — | GPIO40 (J8-23) |
| 5-way RIGHT | — | GPIO41 (J8-25) |
| Press / centre | GPIO1 (J8-3) | GPIO1 (J8-3) |
| Knob A / B | GPIO38 / GPIO39 (J8-19/21) | GPIO7 / GPIO8 (J8-13/15) |
| Knob push | GPIO40 (J8-23) | GPIO42 (J8-24) |
| MENU | GPIO41 (J8-25) | EXIO5 (J8-6), 10k pull-up to 3V3 |
| SIDE | GPIO42 (J8-24) | EXIO6 (J8-8), 10k pull-up to 3V3 |
| 3V3 / GND | J8-16/30/32 / J8-1/4/18/33/34 | same |
Matches firmware board.h (Rev B). Encoder A/B: 10k pull-up + 10k series + 10 nF. Switches: 10k pull-up + 10 nF. Stick wipers: 10 nF to GND at the ADC pin; pot ends to 3V3/GND.

## Control PCB mounting holes (from case)
Written by the case agent 2026-11-08 (case-revB/bead_case.scad `mount_holes`). Front view XY; KiCad Y = -Y.

| Hole | XY | Fastener | Why this spot |
|---|---|---|---|
| H1 | (-4.0, -20.0) | M2 x 14 SHCS from the back: back shell + PCB → front-shell insert (also a shell screw) | Preferred (-3.5, -44.5) collides with the THB001P body corner if the body is 17 (X) x 21 (Y) (TBV): the hole sits on that corner and its boss overlaps the body by 2.8 mm. (-4.0, -20.0) clears the THB envelope by 0.7 mm (boss Ø5.6) and the d-pad flange by 1.1 mm |
| H2 | (40.0, -44.5) | M2 x 14 SHCS from the back (also a shell screw) | as preferred |
| H3 | (18.25, -25.0) | M2 x 6 SHCS from the PCB bottom into a front-shell insert | as preferred |

- Holes Ø2.2 (M2 clearance). Top side: front-shell insert boss Ø5.6 bears on the PCB top face at each hole, so keep a **Ø6.2 copper/part keep-out on the top side**. Bottom side: H1/H2 get a back-shell column Ø6.2, so keep a **Ø6.6 keep-out on the bottom side**. H3 has an M2 head (Ø3.8) on the bottom side, so use Ø4.4.
- Related case inputs (not hole positions): the SIDE actuator tip at X 43.56 (edge + 0.56) leaves only a 0.44 mm cap flange. **Tip X ≤ 43.20 is preferred**, which allows a 0.8 flange with a 1.0 wall. The D-variant cross cap and its flange (cross 24 x 8 + 1.2 lip) sweep down to 1.5 mm above the PCB top. Keep that footprint free of parts taller than 1.5. The case assumes the bottom-side shrouded header is centred at (18.25, -10.5), 50.8 x 8.9, with the mated socket ending at Z 19.1.

## Change note Rev B.1 (2026-11-09, case sync to kicad-control-J/D)
- **MENU:** Omron B3F-1000 (6 x 6, plunger top 4.3 above the PCB = Z 4.2) at (18.25, -18.1). The H3 boss (Ø5.6 at (18.25, -25.0)) leaves no room for a Ø9 cap centred on the switch, so the cap axis sits at **(18.25, -16.6)**: cap Ø9.0 in a Ø9.4 panel hole, 1.5 proud, flange Ø12 x 1.0 with a flat 5.3 from the axis on the H3 side (0.3 from the boss, which also stops it rotating). A Ø3.0 x 1.1 nub, offset 1.5 to -Y, presses the plunger centre with 0.1 pre-gap. The hole is 7.65 from the knob skirt, 13.5 from the encoder hole, 10.1 from the J stick opening and 5.6 from the D cross opening.
- **SIDE:** cap and wall opening at Y -23.0. Tip X 43.20 gives the full **0.8 flange** with 1.0 of wall behind the counterbore.
- **Confirmed unchanged:** H1 (-4.0, -20.0), H2 (40.0, -44.5), H3 (18.25, -25.0); J1 shroud centred (18.25, -10.5) on the bottom, 50.8 x 8.9, overhanging the 49.5 board by 0.65 per side (0.40 to the cavity walls).
- **Ribbon:** H3DDS-3406G replaces H3DDH-3406G. It uses the same HHKR34 socket and AE34G ribbon (tin vs gold plating), so `idc_h` 9.0 and `ribbon_t` 0.9 stay TBV and the depth stays 22.95. Each +0.1 of ribbon thickness adds 0.2 to the depth (two layers), so the 23 target holds only up to 0.92.
