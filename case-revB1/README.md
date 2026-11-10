# Bead pattern designer: handheld enclosure, Rev B

Parametric OpenSCAD source for the 3D-printed case. Dated 2026-11-08; **Rev B.1 2026-11-09** follows the control PCB in `kicad-control-J/` and `kicad-control-D/` (MENU B3F-1000, SIDE Y -23.0 / tip X 43.20, IDC cable H3DDS-3406G).
Coordinates follow `layout-revB.md`. Waveshare data comes from `ws241-mechanical.md`. Control parts come from `controls-eval/parts.md`. The key plate, USB duct, SIDE counterbore and insert practice are carried over from Rev A.1.

| File | What it is |
|---|---|
| `bead_case.scad` | The model: all parameters, derived values, 35 `assert()` design-rule checks and every part. Variant set by `ctrl = "J"` (thumbstick) or `"D"` (5-way + cross cap). |
| `validate_case.py` | Static checker. It parses the .scad, checks every identifier, then for **both** variants evaluates the parameters, reruns the asserts and prints a clearance table. Python 3 only. |
| `validate.log` | Output of the last validator run |

> **Not yet rendered.** No OpenSCAD was available in the authoring environment, so the file has only been checked by `validate_case.py`. The first real render happens on your machine. Use the commands in section 2 and check the console for `WARNING`/`ERROR`.

## 1. What changed from Rev A.1

| | Rev A.1 | Rev B |
|---|---|---|
| Front | raised screen boss, knob deck stepped 8.5 back, 1.2 lip over the glass | **flat 2.0 panel (Z 0..2)**, glass **flush** in a 0.9 deep pocket (0.15 clearance per side) |
| Waveshare | on standoffs to a full-size carrier | **front-loaded** into the pocket, stopped from behind by 4 front-shell bosses (faces at Z 6.0 = glass front + 6.0) |
| Controls | 2 encoders on the carrier | **control PCB** (top face Z 8.5) behind the lower panel: knob + J stick or D 5-way + MENU + SIDE; held by the encoder nut and 3 M2 screws |
| Link | 2x17 socket | 34-way IDC ribbon, J8 to the bottom-side shrouded header |
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
| `"dpad_cap"` | **ctrl D only**: cross cap 24 x 8, flange ring, socket onto the 5-way stem | 1 (+1) |
| `"print_plate"` | Every printed part of the selected variant laid flat | overview |
| `"assembly"` | Shells + ghosts (Waveshare + glass, control PCB with the chosen cursor part, knob, battery, IDC + ribbon). F5. | n/a |

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
| D-pad cap, MENU cap | MJF PA12 or SLA | D-pad: printed top-face down. Its socket (Ø3.1 x 1.4) is sized for a press-fit + glue onto the 5-way stem (stem shape TBV). |
| SIDE cap, key pins | **SLA** (e.g. JLC 9000R) | SIDE flange 0.8 thick (MJF minimum, SLA preferred for the Ø3.6 stem); Ø1.6 key pins are below reliable MJF feature size. |
| First fit test | SLA resin | Checks glass flush, stick/d-pad opening, knob stack, ribbon fold. Use glue-in or self-tapping instead of heat-set inserts in resin. |

## 4. Hardware

| Qty | Item | Use |
|---|---|---|
| 5 | **M2 heat-set insert 3.2 x 4** (ruthex RX-M2x4). Hole Ø3.2 x 4 + Ø1.8 x 1 pilot | Front shell: 2 top corners (face Z 13.0), 3 control-PCB bosses (face Z 8.5) |
| 2 | M2 x 14 countersunk (ISO 10642), head flush | Top corners, back shell → front-shell inserts (4.05 engagement) |
| 2 | M2 x 14 socket head (ISO 4762) | H1 (-4, -20) and H2 (40, -44.5): back shell + control PCB → inserts (3.5 engagement, head 1.95 below the back face) |
| 1 | M2 x 6 socket head | H3 (18.25, -25): control PCB bottom → insert, fitted before the back shell |
| 1 | PEC11R washer Ø12 x 0.5 (TBV) + nut 10 A/F x 2.0 | Clamp the control PCB to the panel through the bushing |
| 1 | Kilo OEJNI-75-4-7 knob | 0.5 above the face; top 16.4 proud; 12.5 mm shaft engagement |
| - | **0.2 mm PSA gasket strip** (e.g. 3M 9448A/tesa 4972, frame die-cut or 1.5 mm strips) | On the pocket ledge. It bonds and seals the glass (TBV-G). |
| - | 0.3 mm double-sided foam + Kapton | Cell onto the Waveshare rear |
| 1 | Assmann H3DDS-3406G 34-way IDC cable, 152 mm (HHKR34S/AE34G/HHKR34S; replaces the discontinued H3DDH-3406G, same socket and ribbon, tin plating) | J8 → control header. A 45° crease is needed (section 6). |

Total shell screws: 4 (2 top countersunk + 2 through the control PCB at H1/H2), all into heat-set inserts.

## 5. Assembly order

1. Press the 5 inserts into the front shell (iron at 220–240 °C for PA12).
2. From inside the front shell, fit the MENU cap (flange inside, flat towards the H3 boss) and the SIDE cap (flange into the right-wall counterbore). **D:** place the cross cap through the cross opening, flange inside.
3. **Control PCB** from the back. The encoder bushing goes through the panel and the PCB rests on the 3 bosses. **D:** the 5-way stem enters the cap socket (press + a drop of glue). Fit the M2 x 6 at H3. Fit the washer and nut on the front (≤ 10 kgf·cm).
4. Prepare the Waveshare: re-terminate the battery lead to J7 (Y 12.8 or 37.5, TBV). Tape the cell (Kapton + 0.3 foam) to the board rear at X 4..24, Y 0..50. Seat the IDC on J8 (pin 1 TBV). Fold the ribbon flat behind the cell: towards −X, then a 45° crease to run down (−Y).
5. Lay the 0.2 PSA gasket on the pocket ledge, liner still on top. Dry-fit the module, then remove the liner.
6. **Front-load the Waveshare**: feed the ribbon through the window first. Lower the module **left (USB) edge first with the right side raised about 12°**. The left-edge part next to the USB-C (X −4.9, TBV) stands 0.79 past the glass and only clears the pocket wall when tilted. Then lower the right side. The 4 boss pins enter the Waveshare holes and the glass seats flush on the gasket. Press for 30 s.
7. Plug the ribbon into the control PCB's bottom header (from the back).
8. Put the 3 key pins into the key plate and slide the plate into the top-wall slot from the back.
9. Close with the back shell: 2 x M2 x 14 countersunk at the top, 2 x M2 x 14 SHCS at H1/H2.
10. Fit the knob 0.5 above the face. Tighten the set screw on the flat (3.8 from the shaft tip; flat length TBV).

## 6. Design decisions and deviations from the brief

- **Waveshare retention: front-load, rear bosses with locating pins, glass bonded to the ledge. The 4 M2 holes are not screwed.** The brief asked for screws through the 4 holes into front-shell bosses. That can't be built: the holes lie inside the glass outline, 0.23–1.12 mm outside the touch VA. So the display stack covers their front side, and a boss in front of the PCB would sit inside the glass footprint. A module loaded from the back would hit those bosses with its glass, and one loaded from the front would hit them with its PCB. A screw from the back needs a nut on the display side, and a screw from the front can't be reached. The bosses therefore sit **behind** the PCB on bridges to the side walls (Z 6.0..8.2, clear of the IDC at 8.5). Their faces set the glass exactly flush (6.0 − 6.0 = 0). Ø1.9 x 1.0 pins locate the holes without reaching the display side. The pocket ledge (Z 0.9..2.0, with the 0.2 gasket) bears the glass from behind. If calipers show free space in front of the holes (TBV-W), an M2 nut can be added there. Then change `ws_pin_*` into Ø2.4 holes and screw from the back.
- **Front loading needs a ledge window**: PCB outline + 0.3 per side (X −3.4..39.9, Y −3.65..55.1, which includes the keys at Y 54.8). The window is notched at the USB-C and at the neighbour part. The glass then rests on 0.71 (left/right) to 1.6 (bottom) of ledge, and on no ledge along the two notches. The validator warns about this (adhesive land < 1.0).
- **Depth 22.95** = cell back 19.8 + 0.15 + **2 ribbon layers (2 x 0.9)** + back wall 1.2. J8 runs along Y and the control header along X, so the ribbon needs one 45° crease. Its flat run towards −X and the creased run towards −Y both pass behind the cell. One layer would give 22.05. A thicker ribbon or a 3-layer crease breaks the 23 target (TBV-R).
- **Control-PCB holes:** H1 moved from the preferred (−3.5, −44.5) to (−4.0, −20.0). The preferred spot sits on the corner of the THB001P body envelope (17 x 21, TBV). The two outer holes double as shell screws because there is no room for a separate column beside the control PCB (0.8–1.05 clearance to the walls). Positions are written to `layout-revB.md`.
- **SIDE cap (B.1):** the KiCad board puts the SW2 actuator tip at X 43.20 (PCB edge + 0.20) and its centre at Y -23.0. With 0.25 pre-travel the cap face is at 43.45, which leaves exactly the full **0.8 flange** with 1.0 of right wall behind the counterbore. The flange is still derived (`side_flange_t`); a tip further out thins it again.
- **MENU cap (B.1):** the B3F-1000 sits at (18.25, -18.1), only 6.9 from the H3 boss (Ø5.6 at (18.25, -25.0)). A Ø9 cap centred on the switch would put its panel hole and flange on that boss. The cap axis is therefore moved +Y to **(18.25, -16.6)**. The axis is derived (`menu_cap_xy`) so the flange flat (5.3 from the axis, 0.6 beyond the hole) is 0.3 from the boss, and the boss also keys the cap against rotation. A Ø3.0 nub, offset 1.5 to -Y, lands on the centre of the Ø3.5 plunger (top Z 4.2 = PCB top 8.5 − 4.3, Omron B3F table) with a 0.1 gap, so the nub is 1.1 long. The flange rear (Z 3.0) is 2.0 in front of the switch body. The 1.5 offset puts a small moment on the cap. If it feels rocky, shorten the proud height or drop to a Ø8 cap (offset 1.0).
- **J opening Ø14.15 (fallback Ø16).** The THB001P drawing is an image the tools could not read. The datasheet text gives 26° max tilt and a 7.75 plastic shaft. The 19.45 height and the Ø13.15 and Ø4 callouts come from the earlier parts survey and are interpreted as stick top, dome Ø and shaft Ø (TBV). The dome Ø13.15 + 2 x 0.5 sets the opening. At 26° the Ø4 shaft sweeps only r 2.96 across the panel band (pivot assumed 8.0 above the PCB, TBV). The stick top stands 10.95 above the face without a cap. The tilted tip clears the knob by 9.4. If the dome turns out to be larger, set `thb_hole_d` directly or use the 16 fallback.
- **D cross cap (derived).** It assumes the SKQU stem pivots 2.9 below its top (TBV), so actuation is at atan(0.4/2.9) = 7.85°. The flange ring (r ≥ 7.6, clear of the switch body) is the over-travel stop. Its rest gap of 1.91 behind the panel touches 0.15 after actuation and before +2° overtravel. The socket sits 1.4 deep over the stem top (Z 3.3). Cap length 6.2. **The cross shifts 0.85 sideways at the front face when it tilts, against the 0.3 clearance from the brief** (warning). Opening the clearance to 0.9 would leave only 0.85 of skin at the left face (cross tip X −7.0 next to the case at −8.75). So 0.3 stays until the real pivot is measured. If the cap binds, move the cursor centre ≥ 0.6 to +X and set `dp_clr = 0.9`. A pressed arm tip ends 0.14 behind the face. Control-PCB parts under the cross + lip must stay ≤ 1.5 tall.
- **Battery:** held by tape on the Waveshare rear plus a top-end fence on the back shell. The ribbon crosses the other three sides, so there are no full bay ribs.

## 7. TBV parameters (update after measuring, then rerun the validator)

| ID | Parameter(s) | Value | What to measure / risk |
|---|---|---|---|
| W | `ws_pcb_t`, front side of the 4 holes | 1.2, n/a | Is there free space between the PCB front and the display at the holes? If yes, screws are possible (section 6) |
| G | `lens_gasket_t`, `lens_clr` | 0.2, 0.15 | Gasket product and glass fit. Flush is set by the bosses (6.0 dimensioned) and the ledge (0.7 + gasket) |
| P | `nb_conn_x0`, `nb_conn_Z0`, `ins_tilt` | −4.9, 6.0, 12° | The left-edge part beyond the glass: is it real, and how deep? Is the tilt insertion OK? |
| S | `side_act_len`, `side_act_h` | 0.20 (KiCad, tip X 43.20), 3.5 | SW2 tip on the soldered board (push the switch fully home). Beyond 43.20 the flange thins below 0.8 |
| J1 | `thb_h_top`, `thb_dome_d`, `thb_shaft_d`, `thb_pivot_h`, `thb_frame_h`, `thb_frame_w/l` | 19.45, 13.15, 4.0, 8.0, 6.0, 17/21 | THB001P drawing. Frame must stay ≤ 6.3 above the PCB |
| D1 | `skqu_top_h`, `skqu_body_h`, `skqu_stem_d`, `skqu_pivot_h` | 5.2, 3.2, 3.0, top − 2.9 | SKQUCAA010 drawing. These set the cap socket, flange gap and side shift |
| E | `enc_body_h`, `enc_bush_len`, `enc_washer_t`, `enc_nut_af` | 6.5, 5.0, 0.5, 10.0 | PEC11R. `enc_body_h` sets the whole control-PCB depth |
| K | `knob_cb_h`, `knob_setscrew_from_top`, `enc_flat_len` | 4.0, 7.2, 7.0 | Kilo OEJ-75 drawing (OEJ-90 values used) |
| M | `menu_plunger_h`, `menu_plunger_d`, `menu_sw_body_h` | 4.3 (Omron table), 3.5, 3.5 (TBV) | B3F-1000 (Rev B.1). Height 4.3 sets the nub length 1.1; body height sets the 2.0 flange travel room |
| H | `cp_hdr_xc/yc/len/w/h` | 18.25/−10.5/50.8/8.9/9.0 | Position **confirmed** in KiCad (J1, bottom, centre (18.25, −10.5)). The KiCad F.Fab outline is 50.95 x 9.12 (X −7.28..43.67), still 0.27 inside the cavity. Mated height (19.1) sets the clearance to the back wall |
| I | `idc_w`, `idc_len`, `idc_h`, `idc_fold_Z`, `hdr_h` | 6.0, 50.8, 9.0, 19.5, 2.5 | HHKR34S socket with strain relief (H3DDS-3406G) on J8. The Assmann drawing could not be read. `idc_h` has 2.25 of margin before the fold reaches the back wall |
| R | `ribbon_t`, `ribbon_layers` | 0.9, 2 | AE34G thickness (not found in Assmann data). Two layers set the depth: 0.9 → 22.95, 1.0 → 23.15, so the target holds only up to 0.92. **Measure first** |
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
  - Cell 0.3 behind the Waveshare parts, 6.75 from the IDC. The IDC fold is 2.25 in front of the back wall.
