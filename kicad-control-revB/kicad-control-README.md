# Bead Designer control PCB Rev B: variants J and D (KiCad 10.0.7)

Generated 2026-11-08 from `layout-revB.md`, including the case-final section "Control PCB mounting holes (from case)".
The files are hand-written KiCad s-expressions: schematic version 20260306, board and footprints 20260206, symbol library 20251024, generator_version "10.0", project version 3.
Generators and checkers are in `scratchpad/kc-tools-revB/`.

| Folder | Variant | Cursor control |
|---|---|---|
| `kicad-control-J/` | **J** | C&K THB001P analog thumbstick with push (JS1) |
| `kicad-control-D/` | **D** | Alps SKQUCAA010 5-way switch, the Adafruit 504 part (SW3). It is end of life, so the footprint is kept replaceable |

Each folder contains `control.kicad_pro`, `control.kicad_sch`, `control.kicad_pcb`, `fp-lib-table`, `sym-lib-table`, `control.kicad_sym` (the project-local THB001P or SKQUCAA010 symbol), and `control.pretty/` (custom footprints).

Both variants share the same 49.5 x 41.0 mm outline (front view X -6.5..43.0, Y -48..-7, corner radius 2), the colour encoder, MENU, SIDE, three M2 holes, and the 2x17 header.

## Placement

Coordinates are front view in mm. The top side faces the front panel. KiCad Y = -Y, and the KiCad origin is the bottom-left Waveshare hole.

| Ref | Part | Position | Side | Notes |
|---|---|---|---|---|
| J1 | AWHW-34G-0202-T 2x17 shrouded header | pin 1 at (-2.07, -9.23). Odd row at Y -9.23, even row at Y -11.77, centre (18.25, -10.5) | **bottom** | Pins run along +X. Same pin numbering as Waveshare J8 |
| JS1 (J) | THB001P | (5.0, -34.0), rotated 90 deg so the body is 17 (X) x 21 (Y), the envelope the case assumes | top | **Provisional footprint, see TBV 1** |
| SW3 (D) | SKQUCAA010 | (5.0, -34.0) | top | |
| ENC1 | PEC11R-4215F-S0024 | shaft at (31.5, -34.0) | top | A-C-B row at the bottom, switch pins at the top |
| SW1 | MENU, **B3F-1000 (6 mm)** | centre (18.25, -18.10) | top | **Changed from the B3F-4055 at (18.25, -14). See deviation 1** |
| SW2 | SIDE, PTS645VL31-2 LFS right-angle | pin row X 40.05, actuator centre Y -23.0 | top | **Actuator tip X = 43.20.** Tip passes the PCB edge by 0.20 (intentional). Courtyard edge is at 42.85 |
| H1 / H2 / H3 | M2 NPTH, dia 2.2 | (-4.0, -20.0) / (40.0, -44.5) / (18.25, -25.0) | - | From the case. Keep-outs: dia 6.2 on top for all three; dia 6.6 on the bottom for H1/H2 and dia 4.4 for H3 |
| TP1 / TP2 | GND / 3V3 THT test pads, dia 2.0 | J: (21.85, -46.0) / (18.6, -46.4). D: (5.0, -45.2) / (8.0, -45.2) | top | |
| R*, C* | 10k axial (0207, P7.62) and 10 nF KEMET C317 (P5.00) | see the board | **bottom** | Mounting them on the bottom side removes the C317 height problem (6.6 mm) under the 6.0 mm top-side budget |

### Deviations from layout-revB.md (coordinator / case, please confirm)

1. **MENU is now an Omron B3F-1000 (6x6 mm, H 4.3; four are owned) at (18.25, -18.10).**
   - The case fixes J1 at (18.25, -10.5). Its pin rows at Y -9.23 and -11.77 come through to the top side.
   - The case also fixes the H3 boss (dia 5.6) at (18.25, -25.0).
   - That leaves Y -12.6 to -21.9 at X 18.25. This is less than the 12 mm B3F-4055 body, and the B3F-4055 pins (Y -11.5) would land on the J1 even row.
   - The MENU cap hole in the front shell moves from Y -14 to Y -18.1. The cap is 6 mm square and the plunger is 2.2 mm below the panel inner face.
   - The B3F-1000 pin-1 row (Y -15.85, pad edge -14.85) sits 0.1 mm inside the shroud footprint (shroud edge -14.95). Trim the MENU leads flush on the bottom side before fitting J1.
2. **SIDE actuator centre is at Y -23.0, not -24.0.** At -24.0 the switch body would overlap the encoder body by 0.45 mm. At -23.0 the bodies are 0.55 mm apart (Fab check). The courtyards still overlap by 2.0 mm at the encoder's conservative rectangular courtyard; this is waived.
3. **J1 shroud.** At 50.8 x 8.9 mm the AWHW-34 shroud is longer than the 49.5 mm PCB. Its courtyard (51.84 long) sits outside the outline on the left, right and top (waived). The case-revB note assumes exactly this envelope. The pad pattern is identical to a plain 2x17 pin header if the shroud ever has to go.

## Pin tables (J8 = Waveshare V2 header, J1 pin n = J8 pin n)

| J8 pin | ESP32 | Variant J net | Variant D net |
|---|---|---|---|
| 3 | GPIO1 | STICK_SW (JS1 push) | NAV_C (5-way centre) |
| 6 | EXIO5 | NC | BTN_MENU |
| 8 | EXIO6 | NC | BTN_SIDE |
| 13 | GPIO7 (ADC) | STICK_X (JS1 X wiper) | ENC_A |
| 15 | GPIO8 (ADC) | STICK_Y (JS1 Y wiper) | ENC_B |
| 19 | GPIO38 | ENC_A | NAV_UP |
| 21 | GPIO39 | ENC_B | NAV_DOWN |
| 23 | GPIO40 | ENC_SW | NAV_LEFT |
| 24 | GPIO42 | BTN_SIDE | ENC_SW |
| 25 | GPIO41 | BTN_MENU | NAV_RIGHT |
| 16, 30, 32 | 3V3 | +3V3 | +3V3 |
| 1, 18, 34 | GND | GND | GND |
| 4, 33 | (GND on J8) | GND | **NC** (reversal guard: a 180-deg flipped ribbon would put pin 4 on USB_5V and pin 33 on BAT; changed 2026-11-15) |
| 10 | EXIO7 | NC | **USB_PRESENT** (R13/R14 divider; high = USB 5 V present; added 2026-11-17) |
| 31 | USB_5V | NC | **USB_5V** into R13 only (reversed ribbon lands it on J8-4 GND: safe) |
| other | - | no-connect | no-connect |

Signal conditioning:
- **Encoder A/B:** 10k pull-up to 3V3 on the raw line (ENC_x_RAW), 10k series resistor, then 10 nF to GND at the GPIO.
- **Switches** (encoder push, MENU, SIDE, stick push, every 5-way line): 10k pull-up to 3V3 and 10 nF to GND. Each switch closes to GND.
- **Stick (J):** pot ends go to 3V3 (pins 1 and 1'/4) and GND (pins 3 and 3'/6). Wipers (pins 2 and 2'/5) go to the ADC pins, each with 10 nF to GND.
- **Stick push (J):** pads a (7) and d (10) are the diagonal pins of the 4-pin switch, which are opposite contacts on any 4-pin tact switch. Pads b and c are left open.
- **5-way (D):** physical directions per the Alps top view: A (pin 1) = UP, B (pin 2) = LEFT, C (pin 3) = DOWN, D (pin 5) = RIGHT, 6 = centre, 4 = COM to GND. The PCB net names on pins 2 and 5 are crossed (pin 2 = NAV_RIGHT, pin 5 = NAV_LEFT); firmware `board.h` swaps them (2026-11-15), so the board was not respun.

Resistor and capacitor references:

| Function | Variant J | Variant D |
|---|---|---|
| Encoder A filter | R1 (pull-up), R2 (series), C1 | same |
| Encoder B filter | R3, R4, C2 | same |
| Encoder push | R5, C3 | same |
| MENU | R6, C4 | same |
| SIDE | R7, C5 | same |
| Stick push | R8, C6 | - |
| Stick wipers X / Y | C7 / C8 | - |
| NAV_UP / NAV_DOWN / NAV_LEFT / NAV_RIGHT / NAV_C | - | R8+C6 / R9+C7 / R10+C8 / R11+C9 / R12+C10 |

## BOM

Owned stock: PEC11R x5, B3F-4055 x4, B3F-1000 x4, PTS645VL31 x10, 10k x25, C317 10 nF x10.

| Item | MPN | J qty | D qty | Covered by owned stock (both boards) | To buy (both boards) |
|---|---|---|---|---|---|
| 2x17 shrouded header | Assmann AWHW-34G-0202-T | 1 | 1 | 0 | **2** |
| Encoder | Bourns PEC11R-4215F-S0024 | 1 | 1 | 2 of 5 | 0 |
| MENU switch | Omron B3F-1000 | 1 | 1 | 2 of 4 | 0 |
| SIDE switch | C&K PTS645VL31-2 LFS | 1 | 1 | 2 of 10 | 0 |
| Thumbstick | C&K THB001P (Mouser 611-THB001P) | 1 | - | 0 | **1** (+1 spare suggested) |
| 5-way switch | Alps SKQUCAA010 / Adafruit 504 | - | 1 | 0 | **1** (+1 spare, EOL) |
| 10k 1% 0.25 W axial | YAGEO MFR-25FRF52-10K or equivalent | 8 | 12 | 20 of 25 | 0 |
| R13 2.2k 1% 1206 SMD (D only, bottom) | YAGEO RC1206FR-072K2L | 0 | 1 | 0 | 1 (+spares) |
| R14 4.7k 1% 1206 SMD (D only, bottom) | YAGEO RC1206FR-074K7L | 0 | 1 | 0 | 1 (+spares) |
| 10 nF 50 V X7R radial, 5.08 mm leads | KEMET C317C103K5R5TA | 8 | 10 | 10 of 10 | **8** (+2 spare) |
| Knob (off-board) | Kilo OEJNI-75-4-7 | 1 | 1 | 0 | 2 |
| 34-way IDC ribbon, F/F | Assmann H3DDS-3406G (replaces discontinued H3DDH) | 1 | 1 | 0 | 1-2 (one per assembled device) |
| M2 screws | M2x14 SHCS (H1, H2), M2x6 SHCS (H3) | 2 + 1 | 2 + 1 | 0 | per case BOM |

The 4 owned B3F-4055 are no longer used (see deviation 1).

## Assembly order (per board)

0. **D only: SMD first.** Solder R13 (2.2k) and R14 (4.7k), the two 1206 resistors on the bottom under the encoder area, before any THT part (they sit 0.67 mm from the ENC1 S1/S2 joints).
1. **Bottom-side passives first** (R*, C*): insert from the bottom and solder on the top.
   - Trim the leads to 1.0 mm or less on the top side.
   - D only: anything under the cross cap must stay at 1.5 mm or less above the top face. The cross area is drawn on Dwgs.User.
   - Stand the C317s upright, body flush to the board (do not bend them over). Their height (up to about 6.6 mm) is fine on the bottom.
2. Top-side low parts: TP1/TP2 are bare pads, so nothing to fit.
3. MENU SW1 (B3F-1000). Trim its bottom-side leads flush, because they sit under the J1 shroud edge.
4. SIDE SW2. Push it against the board so the actuator tip ends at X 43.20.
5. Cursor control: JS1 THB001P (J) or SW3 SKQUCAA010 (D, snap-in).
6. ENC1 PEC11R. Solder the lugs too, since they carry GND.
7. J1 header last, on the bottom side, key notch toward the PCB top edge (+Y). Solder it on the top side.
8. Tests:
   - 3V3 to GND resistance is not a short.
   - Each switch line reads about 10k to 3V3, and 0 ohm when pressed.
   - Stick wipers read 0-10k across their travel.
   - Check the ribbon with pin 1 to pin 1 continuity before connecting it to the Waveshare.
   - D only (USB sense): J1-31 to GND reads about 6.9k (R13 + R14); J1-10 to GND reads about 4.7k; **J1-31 to J1-30 / J1-32 (3V3) must read open** before the first connection.

## Footprints

| Footprint | Source | How it was checked |
|---|---|---|
| Connector_IDC:IDC-Header_2x17_P2.54mm_Vertical, Button_Switch_THT:SW_PUSH_6mm, Button_Switch_THT:SW_Tactile_SPST_Angled_PTS645Vx31-2LFS, Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal, MountingHole:MountingHole_2.2mm_M2, TestPoint:TestPoint_THTPad_D2.0mm_Drill1.0mm | Official KiCad library (GitHub kicad-footprints mirror) | Geometry copied verbatim |
| **Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm** | Official library | `C_Rect_L4.0mm_W2.5mm_P5.00mm` **does not exist** in the library snapshot I could check. Only `C_Rect_L4.0mm_W2.5mm_P2.50mm` does, and GitLab was not reachable to check the current library. The disc P5.00 footprint has a 5.0 mm pitch with dia 0.8 drills, which suits the C317's 5.08 mm leads |
| control:RotaryEncoder_Bourns_PEC11R-4xxxF-Sxxxx_Vertical | Custom (from the carrier) | Bourns PEC11R datasheet text: A-C-B order, 2.5 mm pitch, switch pins 5.0 apart, 5x dia 1.0 holes. Row offsets 7.5/7.0 and lug positions agree with the official EC11E footprint (PEC11R is EC11-compatible). The lug slot is 2.2 x 2.8 at +/-5.45, which accepts a lug pitch of 10.9-11.2. **Lug spacing TBV** |
| control:SW_Alps_SKQUCAA010_5way_THT | Custom | Alps SKQU drawing callouts: 10.3 across, 6.5 row span, 0.45 offset of the middle pins, 4x dia 1.2 + 2x dia 1.0. Pin map 1=A 2=B 3=C 4=COM 5=D 6=centre. Cross-checked against the rusefi/kicad-libraries SKQUCAA010 module. **The direction of the middle-pin offset is TBV** |
| control:Joystick_CK_THB001P_PROVISIONAL | Custom, **provisional** | Hole sizes and counts (4x dia 1.5 legs, 4x dia 1.2 switch, 6x dia 1.0 pots), the 17x21 body and the pinout (wiper = 2) come from the C&K THB datasheet text. **Hole positions are inferred:** the drawing could not be retrieved (ckswitches/littelfuse/LCSC/Mouser PDFs were blocked or text-only). **Redraw from the C&K drawing before ordering J** |

## What to check in KiCad 10.0.7

- **Open:** open `control.kicad_pro`. KiCad may offer to add the project symbol and footprint libraries; they are already in `sym-lib-table` and `fp-lib-table`.
- **ERC:** expect 0 errors. Warnings you can ignore:
  - "symbol does not match library" for Conn_02x17 (generated from the KiCad generator rules) and for MountingHole and SW_Push (converted from KiCad 9).
- **Before DRC:** press **B** to fill the zones. The GND zones are saved unfilled; every net is already connected by tracks.
- **DRC: expected and acceptable**
  - J1 courtyard and B.SilkS outside the board edge (shroud overhang, deviation 3).
  - Courtyard overlap ENC1/SW2 (bodies 0.55 mm apart).
  - ENC1 courtyard corner inside the H2 keep-out circle. Its body is 4.4 mm from H2, which clears the 2.8 mm boss radius.
  - "Footprint differs from library" for the official footprints, which came from an older library snapshot. Do not update them blindly: re-run DRC after any update.
- **DRC: anything else is a real problem**, in particular clearance, unconnected items, or copper in a keep-out.
- **1:1 print test** before ordering:
  - In the PCB editor: File > Print, scale 1:1, layers F.Cu, F.Fab, Edge.Cuts.
  - Push the real THB001P, SKQUCAA010 and PEC11R pins through the paper on foam.
  - THB001P must match exactly; it is provisional.
  - For the SKQU, check the 0.45 offset direction of the middle pins.
  - For the PEC11R, check that the lugs land in the slots.

## JLCPCB order notes

- 2 layers, 1.6 mm FR-4, HASL (lead-free optional), green or any colour, 1 oz copper.
- 5 pcs of each variant: two separate orders or two designs in one cart. Do not panelise J and D together.
- Size 49.5 x 41.0 mm. Minimum track 0.3 (0.2 for the USB_5V / USB_PRESENT sense tracks added in revB4), clearance 0.2 (USB_5V kept ≥0.3 from 3V3), minimum drill 0.3 (vias) and 0.8 (THT). All standard.

Gerber export in KiCad 10:

1. Press B to fill the zones, then run DRC.
2. File > Fabrication Outputs > Gerbers (.gbr) (the Plot dialog).
   - Layers: F.Cu, B.Cu, F.SilkS, B.SilkS, F.Mask, B.Mask, Edge.Cuts.
   - Options: plot reference designators; "Use Protel filename extensions"; Gerber X2 off (JLC accepts both); "Subtract soldermask from silkscreen" on.
3. Generate Drill Files: Excellon, units mm, decimal format, absolute origin, PTH and NPTH in separate files, map format Gerber. Click Generate.
4. Zip the plot folder and upload it to jlcpcb.com. Check that the preview shows the three NPTH M2 holes and the outline.

## Open TBVs

1. **THB001P footprint hole positions (blocking for J).**
2. SKQUCAA010: the middle-pin offset direction and the A..D direction map.
3. PEC11R lug spacing.
4. MENU change to B3F-1000 and its new cap position (18.25, -18.1): the case needs to follow. The SIDE actuator centre at Y -23.0 also changes the case cap hole.
5. J1 key-notch orientation versus how the ribbon is routed to the Waveshare header.
6. The C317 footprint name the spec suggested (C_Rect_L4.0mm_W2.5mm_P5.00mm) does not exist. C_Disc_D5.0mm_W2.5mm_P5.00mm is used instead.
7. Encoder body height of 6.5 mm (sets the PCB Z) and the THB001P stick height (19.45 mm, so about 10.9 mm protrudes above the front face).
