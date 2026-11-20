# Bead pattern designer: handheld enclosure, Rev B

Parametric OpenSCAD source for the 3D-printed case. Dated 2026-11-08; **Rev B.1 2026-11-09** follows the control PCB in `kicad-control-J/` and `kicad-control-D/` (MENU B3F-1000, SIDE Y -23.0 / tip X 43.20, IDC cable H3DDS-3406G).

> **Link rev B5 (2026-11-19, replaces the soldered-wire plan from earlier the same day):** the Waveshare J8 is a **female** 2x17 socket.
> - **Adapter:** the new **J8 adapter PCB** (`kicad-control-revB/kicad-adapter-J8/`) plugs into J8 with a 2x17 male header; you solder the header.
> - **Cable:** a **30 mm 16-way 0.5 mm same-side FFC** (GCT 05-16-A-0030-A-4-06-4-T) links the adapter's J2 to J1 on the control PCB rev B5. Both J2 and J1 are Hirose FH12-16S-0.5SH(55), assembled by JLC.
> - **Cable path:** the cable Z-folds between the two facing slots (adapter bottom edge Y -4.6, control top edge Y -7.0).
> - **USB sense:** R1 2.2k / R2 4.7k moved to the adapter.
> - **Case:** depth 21.5, top screws M2 x 12, ribs in the back shell hold the adapter.
Coordinates follow `layout-revB.md`. Waveshare data comes from `ws241-mechanical.md`. Control parts come from `controls-eval/parts.md`. The key plate, USB duct, SIDE counterbore and insert practice are carried over from Rev A.1.

| File | What it is |
|---|---|
| `bead_case.scad` | The model: all parameters, derived values, 38 `assert()` design-rule checks and every part. Variant set by `ctrl = "J"` (thumbstick) or `"D"` (5-way + cross cap). |
| `validate_case.py` | Static checker. It parses the .scad, checks every identifier, then for **both** variants evaluates the parameters, reruns the asserts and prints a clearance table. Python 3 only. |
| `validate.log` | Output of the last validator run |

> **Not yet rendered.** No OpenSCAD was available in the authoring environment, so the file has only been checked by `validate_case.py`. The first real render happens on your machine. Use the commands in section 2 and check the console for `WARNING`/`ERROR`.

## 1. What changed from Rev A.1

| | Rev A.1 | Rev B |
|---|---|---|
| Front | raised screen boss, knob deck stepped 8.5 back, 1.2 lip over the glass | **flat 2.0 panel (Z 0..2)**, glass **flush** in a 0.9 deep pocket (0.15 clearance per side) |
| Waveshare | on standoffs to a full-size carrier | **front-loaded** into the pocket, stopped from behind by 4 front-shell bosses (faces at Z 6.0 = glass front + 6.0) |
| Controls | 2 encoders on the carrier | **control PCB** (top face Z 8.5) behind the lower panel: knob + J stick or D 5-way + MENU + SIDE; held by the encoder nut and 3 M2 screws |
| Link | J8 = 2x17 **female** socket | J8 adapter PCB (male header into J8) + 30 mm 16-way 0.5 mm same-side FFC to J1 (FH12-16S) on the control PCB |
| Size | 56 x 110.3 x 35.1 | **54 x 110.3 x 22.95** |
| Walls | 2.0 | 1.2 sides/bottom/back, 2.0 top (key-plate rail), 2.0 panel |

## 2. Export (one STL per part)

| `show =` | Part | Qty |
|---|---|---|
| `"front"` | Front shell: panel, glass pocket + ledge window, cursor opening (J round / D cross), encoder + MENU holes, SIDE hole + counterbore, USB-C duct, key-plate slot, 4 Waveshare bosses with pins, 3 control-PCB insert bosses, 2 top-corner insert bosses | 1 |
| `"back"` | Back shell: locating lip, 2 screw columns (H1/H2), 2 top countersunk columns, battery top fence | 1 |
| `"key_plate"` | Slide-in top-wall section holding the 3 key pins | 1 |
| `"key_cap"` | Key pin for PWR/BOOT/RST | 3 (+2) |
| `"menu_cap"` | MENU cap Ø9 (hole Ø9.4), flange Ø12 x 1.0 with a flat towards H3, Ø3 nub offset 1.5 onto the B3F-1000 plunger | 1 (+1) |
| `"side_cap"` | SIDE cap Ø3.6, 0.8 flange | 1 (+2) |
| `"dpad_cap"` | **ctrl D only**: cross cap 23.2 x 7.2, flange ring, square 3.3 socket onto the 5-way stem | 1 (+1) |
| `"print_plate"` | Every printed part of the selected variant laid flat | overview |
| `"assembly"` | Shells + ghosts (Waveshare + glass, control PCB with the chosen cursor part, knob, battery, J8 plug + wire bundle). F5. | n/a |

```sh
for c in J D; do
  for p in front back key_plate menu_cap side_cap key_cap dpad_cap print_plate assembly; do
    openscad -o bead_${c}_$p.stl -D "show=\"$p\"" -D "ctrl=\"$c\"" bead_case.scad 2>&1 | tee bead_${c}_$p.log
  done
done        # (dpad_cap renders only for D; assembly is preview-only: use F5 or export a PNG)
python3 validate_case.py bead_case.scad      # same checks without OpenSCAD, both variants
```

Only the **front shell** differs between J and D (round Ø14.15 opening or cross opening). The back shell and the small parts are shared. The D variant adds `dpad_cap`.

## 3. Print settings (JLCPCB 3D printing)

| Part | Process / material | Notes |
|---|---|---|
| Front + back shell, key plate | **MJF PA12**, grey or dyed black | Thinnest walls are 1.0: key-plate rail ledge, USB frame/duct, top insert skin and wall, back-screw columns, right wall behind the SIDE counterbore. The 1.1 ledge behind the glass is the glass seat; ask for no bead-blasting on the ledge face. Upload each STL separately. |
| D-pad cap, MENU cap | MJF PA12 or SLA | D-pad: printed top-face down. Its square socket (3.3 x 3.3 x 3.4 deep, 0.8 skin in front) is a press fit + glue onto the 3.2 square stem. |
| SIDE cap, key pins | **SLA** (e.g. JLC 9000R) | SIDE flange 0.8 thick (MJF minimum, SLA preferred for the Ø3.6 stem); Ø1.6 key pins are below reliable MJF feature size. |
| First fit test | SLA resin | Checks glass flush, stick/d-pad opening, knob stack, wire routing. Use glue-in or self-tapping instead of heat-set inserts in resin. |

## 4. Hardware

| Qty | Item | Use |
|---|---|---|
| 5 | **M2 heat-set insert 3.2 x 4** (ruthex RX-M2x4). Hole Ø3.2 x 4 + Ø1.8 x 1 pilot | Front shell: 2 top corners (face Z 13.0), 3 control-PCB bosses (face Z 8.5) |
| 3 | M2 x 4 pan head (ISO 7045) | Waveshare standoffs at (0,0), (0,51), (36.5,51) through the front-shell bosses |
| 2 | M2 x **12** countersunk (ISO 10642), head flush | Top corners, back shell → front-shell inserts (3.5 engagement) |
| 2 | M2 x 14 socket head (ISO 4762) | H1 (-4, -20) and H2 (40, -44.5): back shell + control PCB → inserts (3.5 engagement, head 0.5 below the back face) |
| 1 | M2 x 6 socket head | H3 (18.25, -25): control PCB bottom → insert, fitted before the back shell |
| 1 | PEC11R washer Ø12 x 0.5 (TBV) + nut 10 A/F x 2.0 | Clamp the control PCB to the panel through the bushing |
| 1 | Kilo OEJNI-75-4-7 knob | 0.5 above the face; top 16.4 proud; 12.5 mm shaft engagement |
| - | **0.2 mm PSA gasket strip** (e.g. 3M 9448A/tesa 4972, frame die-cut or 1.5 mm strips) | On the pocket ledge. It bonds and seals the glass (TBV-G). |
| - | 0.3 mm double-sided foam + Kapton | Cell onto the Waveshare rear |
| 1 | J8 adapter PCB rev A (JLC, J2 + R1/R2 assembled) + 2x17 male header (hand-soldered) + 30 mm 16-way 0.5 mm same-side FFC (GCT 05-16-A-0030-A-4-06-4-T) | Adapter into J8 (pin 1 at the keys end); FFC J2 → control J1. Insert the FFC into J1 first, then J2 (FH12: ~20 mating cycles). |

Total shell screws: 4 (2 top countersunk + 2 through the control PCB at H1/H2), all into heat-set inserts.

## 5. Assembly order

1. Press the 5 inserts into the front shell (iron at 220–240 °C for PA12).
2. From inside the front shell, fit the MENU cap (flange inside, flat towards the H3 boss) and the SIDE cap (flange into the right-wall counterbore). **D:** place the cross cap through the cross opening, flange inside.
3. **Control PCB** from the back. The encoder bushing goes through the panel and the PCB rests on the 3 bosses. **D:** the 5-way stem enters the cap socket (press + a drop of glue). Fit the M2 x 6 at H3. Fit the washer and nut on the front (≤ 10 kgf·cm).
4. Prepare the Waveshare: re-terminate the battery lead to J7 (Y 12.8 or 37.5, TBV). Tape the cell (Kapton + 0.3 foam) to the board rear at X 4..24, Y 0..50. Solder the wires to the male header's trimmed tails (sleeve J8-31 USB 5 V on its own; heat-shrink/Kapton + hot-melt over all tails), plug it into J8 **with pin 1 to pin 1** (it fits either way round), and lay the bundle down −Y beside the cell, not behind it. Before first power: J8-16 to GND must read > 1 kΩ.
5. Lay the 0.2 PSA gasket on the pocket ledge, liner still on top. Dry-fit the module, then remove the liner.
6. **Front-load the Waveshare**: feed the wire bundle through the window first. Lower the module **left (USB) edge first with the right side raised about 12°**. The left-edge part next to the USB-C (X −4.9, TBV) stands 0.79 past the glass and only clears the pocket wall when tilted. Then lower the right side. The 4 boss pins enter the Waveshare holes and the glass seats flush on the gasket. Press for 30 s.
7. Solder the wires into the control PCB's J1 holes, pin n to pin n (J1 not fitted).
8. Put the 3 key pins into the key plate and slide the plate into the top-wall slot from the back.
9. Close with the back shell: 2 x M2 x 12 countersunk at the top, 2 x M2 x 14 SHCS at H1/H2.
10. Fit the knob 0.5 above the face. Tighten the set screw on the flat (3.8 from the shaft tip; flat length TBV).

## 6. Design decisions and deviations from the brief

- **Waveshare retention (2026-11-19): front-loaded, then screwed from the back into its own M2 standoffs.**
  - **Standoffs:** the module has 4 female M2 standoffs on its rear, 4.0 mm tall (user caliper).
  - **Bosses:** front-shell bosses on bridges to the side walls sit behind the standoff tips at Z 10.0–11.5, so the standoff tips set the glass exactly flush (10.0 − 4.0 − 6.0 = 0).
  - **Screws:** 3 × M2 × 4 pan head (ISO 7045) go through Ø2.4 holes into the standoffs, giving 2.5 mm of thread.
  - **Hole (36.5, 0) is not screwed:** the FFC Z-fold passes there.
  - **Clearances:** the screw heads end at Z 13.1, 0.4 below the J8 adapter.
  - **Glass:** it still sits on the 0.2 gasket on the ledge; a light adhesive is optional.
  - **Assembly:** front-load the module, fit the 3 screws from the back, then tape on the battery, fit the adapter, the FFC, and close.
- **Front loading needs a ledge window**: PCB outline + 0.3 per side (X −3.4..39.9, Y −3.65..55.1, which includes the keys at Y 54.8). The window is notched at the USB-C and at the neighbour part. The glass then rests on 0.71 (left/right) to 1.6 (bottom) of ledge, and on no ledge along the two notches. The validator warns about this (adhesive land < 1.0).
- **Depth 21.5** = cell back 19.8 + 0.5 swell allowance + back wall 1.2. Nothing runs behind the cell: the J8 plug and wire bend top out at Z 16.6 (3.7 in front of the back wall), and the wires along the control PCB at Z ~11.4. (Before 2026-11-19 the IDC ribbon gave 22.95, and it could not mate with the female J8 anyway.) The two top screws are now **M2 x 12** (were 14).
- **Control-PCB holes:** H1 moved from the preferred (−3.5, −44.5) to (−4.0, −20.0). The preferred spot sits on the corner of the THB001P body envelope (17 x 21, TBV). The two outer holes double as shell screws because there is no room for a separate column beside the control PCB (0.8–1.05 clearance to the walls). Positions are written to `layout-revB.md`.
- **SIDE cap (B.1):** the KiCad board puts the SW2 actuator tip at X 43.20 (PCB edge + 0.20) and its centre at Y -23.0. With 0.25 pre-travel the cap face is at 43.45, which leaves exactly the full **0.8 flange** with 1.0 of right wall behind the counterbore. The flange is still derived (`side_flange_t`); a tip further out thins it again.
- **MENU cap (B.1):** the B3F-1000 sits at (18.25, -18.1), only 6.9 from the H3 boss (Ø5.6 at (18.25, -25.0)). A Ø9 cap centred on the switch would put its panel hole and flange on that boss. The cap axis is therefore moved +Y to **(18.25, -16.6)**. The axis is derived (`menu_cap_xy`) so the flange flat (5.3 from the axis, 0.6 beyond the hole) is 0.3 from the boss, and the boss also keys the cap against rotation. A Ø3.0 nub, offset 1.5 to -Y, lands on the centre of the Ø3.5 plunger (top Z 4.2 = PCB top 8.5 − 4.3, Omron B3F table) with a 0.1 gap, so the nub is 1.1 long. The flange rear (Z 3.0) is 2.0 in front of the switch body. The 1.5 offset puts a small moment on the cap. If it feels rocky, shorten the proud height or drop to a Ø8 cap (offset 1.0).
- **J opening Ø14.15 (fallback Ø16).** The THB001P drawing is an image the tools could not read. The datasheet text gives 26° max tilt and a 7.75 plastic shaft. The 19.45 height and the Ø13.15 and Ø4 callouts come from the earlier parts survey and are interpreted as stick top, dome Ø and shaft Ø (TBV). The dome Ø13.15 + 2 x 0.5 sets the opening. At 26° the Ø4 shaft sweeps only r 2.96 across the panel band (pivot assumed 8.0 above the PCB, TBV). The stick top stands 10.95 above the face without a cap. The tilted tip clears the knob by 9.4. If the dome turns out to be larger, set `thb_hole_d` directly or use the 16 fallback.
- **D cross cap (updated 2026-11-15 to the Alps SKQUCAA010 drawing, confirmed with calipers on the Adafruit 504).** Stem top 10.0 above the PCB (Z −1.5, so the stem stands 1.5 in front of the panel face), square stem 3.2 from the collar top at 5.8, housing 3.15, pivot 1.23 above the PCB, 0.4 travel measured 2.9 below the top, so actuation is atan(0.4/5.87) = 3.9°; centre push 0.2. The cap covers the stem with 0.8 skin, so the cross stands **2.3** proud. Set `skqu_stem_trim` (mm cut off the stem) to lower it; the socket must keep ≥ 2.0 of grip. The socket grips 3.4 of the stem; the cap rear stops 0.8 in front of the collar with a Ø8.0 relief (collar Ø7.0 TBV, not dimensioned). Side shift at the face is 0.49 at actuation against `dp_clr` 0.7. The span and arm were reduced to 23.2 x 7.2 so the panel opening stays 24.6 x 8.6. The flange gap is 1.06 and it stops the tilt 0.15 after actuation. Cap length 6.2. A pressed arm tip stays 1.5 in front of the face.
- **Battery:** held by tape on the Waveshare rear plus a top-end fence on the back shell. The wire bundle runs down its right side, so there are no full bay ribs.

## 7. TBV parameters (update after measuring, then rerun the validator)

| ID | Parameter(s) | Value | What to measure / risk |
|---|---|---|---|
| W | `ws_pcb_t`, front side of the 4 holes | 1.2, n/a | Is there free space between the PCB front and the display at the holes? If yes, screws are possible (section 6) |
| G | `lens_gasket_t`, `lens_clr` | 0.2, 0.15 | Gasket product and glass fit. Flush is set by the bosses (6.0 dimensioned) and the ledge (0.7 + gasket) |
| P | `nb_conn_x0`, `nb_conn_Z0`, `ins_tilt` | −4.9, 6.0, 12° | The left-edge part beyond the glass: is it real, and how deep? Is the tilt insertion OK? |
| S | `side_act_len`, `side_act_h` | 0.20 (KiCad, tip X 43.20), 3.5 | SW2 tip on the soldered board (push the switch fully home). Beyond 43.20 the flange thins below 0.8 |
| J1 | `thb_h_top`, `thb_dome_d`, `thb_shaft_d`, `thb_pivot_h`, `thb_frame_h`, `thb_frame_w/l` | 19.45, 13.15, 4.0, 8.0, 6.0, 17/21 | THB001P drawing. Frame must stay ≤ 6.3 above the PCB |
| D1 | `skqu_collar_d` | 7.0 | Collar Ø (the only SKQU value not on the Alps drawing). Only sizes the relief in the cap rear (Ø8.0). The other SKQU values were confirmed on 2026-11-15 |
| E | `enc_body_h`, `enc_bush_len`, `enc_washer_t`, `enc_nut_af` | 6.5, 5.0, 0.5, 10.0 | PEC11R. `enc_body_h` sets the whole control-PCB depth |
| K | `knob_cb_h`, `knob_setscrew_from_top`, `enc_flat_len` | 4.0, 7.2, 7.0 | Kilo OEJ-75 drawing (OEJ-90 values used) |
| M | `menu_plunger_h`, `menu_plunger_d`, `menu_sw_body_h` | 4.3 (Omron table), 3.5, 3.5 (TBV) | B3F-1000 (Rev B.1). Height 4.3 sets the nub length 1.1; body height sets the 2.0 flange travel room |
| H | `cp_hdr_xc/yc/len/w/h` | 18.25/−10.5/50.8/8.9/2.6 (J1 not fitted: stubs + wire) | Position **confirmed** in KiCad (J1, bottom, centre (18.25, −10.5)). The KiCad F.Fab outline is 50.95 x 9.12 (X −7.28..43.67), still 0.27 inside the cavity. Mated height (19.1) sets the clearance to the back wall |
| I | `idc_w`, `idc_len`, `idc_h`, `idc_fold_Z`, `hdr_h` | 5.1, 43.2, 4.5, 16.6, 5.0 | Male 2x17 plug in the female J8 (top Z 11.0, user caliper), tails + solder 2.0 TBV. 3.7 of margin to the back wall |
| R | `ribbon_t`, `ribbon_layers` | 0.65, 0 | 30 AWG wire OD (TBV); nothing behind the cell, so it doesn't set the depth |
| B | `batt_t`, `batt_gap`, `batt_clr_z` | 10.0, 0.3, 0.15 | Measured cell incl. PCM and swelling |
| U | `usb_Z`, `usb_face_x`, `key_tip_y`, `key_Z`, `j7_y` | 7.8, −4.1, 54.8, 6.9, 12.8 | As Rev A.1 (scaled ±0.3) |
| V | `ws_pcb_w/h`, `va_top_margin` | 42.7/57.7, 6.4 | Scaled. They set the ledge window |

## 8. Validation status (Rev B.1, 2026-11-09)

`python3 validate_case.py` (log in `validate.log`):
- The file parses and brackets balance. 0 undefined identifiers or unknown modules/arguments. 13 warnings, all for unused data parameters kept for documentation.
- **ctrl J: 35/35 asserts, 0 clearance fails, 1 warning** (glass ledge 0.71 < 1.0).
- **ctrl D: 35/35 asserts, 0 clearance fails, 2 warnings** (glass ledge, plus the cross side shift 0.85 > 0.3 clearance).
- The B.1 changes clear the old SIDE flange warning (now 0.80).
- Key numbers:
  - Depth 22.95. Glass flush 0.000 from both the bosses and the ledge.
  - MENU: the hole is 6.47 from the glass pocket, 7.65 from the knob skirt, 13.47 from the encoder hole, 10.10 from the J stick opening and 5.60 from the D cross opening. Flange flat to the H3 boss 0.30, hole cut to the boss 0.90, nub 1.10, flange to the switch body 2.00.
  - SIDE: flange 0.80, right wall behind it 1.00, flange 0.20 from the panel/PCB.
  - Mount-hole bosses clear the parts by 0.74 / 1.64 / 1.10 (J) and 1.08 / 1.64 / 1.00 (D). The header clears the cavity walls by 0.40.
  - Knob: protrusion 16.4, engagement 12.5 / 13. USB duct: 1.4 to the panel, 0.8 to the parting plane, 0.5 to the neighbour part.
  - Cell 0.3 behind the Waveshare parts, 7.2 from the J8 plug. The wire bend is 3.7 in front of the back wall.
