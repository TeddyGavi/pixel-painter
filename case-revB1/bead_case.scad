// =====================================================================
// bead_case.scad  --  Bead pattern designer, handheld enclosure, Rev B
// Date: 2026-11-08, Rev B.1 2026-11-09 (MENU B3F-1000, SIDE Y -23.0 / tip 43.20,
// from kicad-control-J/D).  Parametric OpenSCAD source (2021.01 or newer).
//
// Rev B: FLAT 2.0 mm front panel, cover glass FLUSH with the front face,
// Waveshare front-loaded into a glass pocket and stopped from behind by 4
// front-shell bosses, separate control PCB behind the lower half of the panel,
// 34-way IDC ribbon between them, 102050 cell behind the Waveshare.
// Two control variants selected by `ctrl`: "J" thumbstick (C&K THB001P),
// "D" 5-way switch (Adafruit 504 / Alps SKQUCAA010) with a printed cross cap.
//
// Sources (authoritative):
//   layout-revB.md         frames, outlines, control XY, Z stack, battery
//   ws241-mechanical.md    Waveshare ESP32-S3-Touch-AMOLED-2.41 drawing data
//   controls-eval/parts.md PEC11R hardware, Kilo OEJNI-75-4-7, THB001P, SKQUCAA010
//   case-revA (A.1)        key plate, USB duct, SIDE counterbore, insert practice
//
// FRAMES (unchanged from Rev A)
//   X right, Y up = layout front view; origin = Waveshare bottom-left hole (board
//   rotated 180 deg).  Z (layout depth) = 0 at the FRONT face of the case, which
//   is also the glass front face (flush), + towards the back.  Model z = -Z.
//   zext/zcyl/zbox take layout Z values and do the flip.
//
// "TBV" = to be verified on the real part.
//
// OUTPUT: set `show` (or -D 'show="front"'), variant with -D 'ctrl="D"':
//   "assembly"  translucent shells + ghost parts (F5)
//   "front" "back" "key_plate" "menu_cap" "side_cap" "key_cap" "dpad_cap" (ctrl D)
//   "print_plate"  every printed part of the selected variant laid out flat
// =====================================================================

/* [Output] */
show        = "assembly";
ctrl        = "D";       // "J" = thumbstick THB001P | "D" = 5-way SKQUCAA010 + printed cross cap
section     = "none";    // assembly only: "none" | "x" (keep X <= section_pos) | "y" (keep Y <= section_pos)
section_pos = 5.0;       // 5.0 = through the cursor-control axis
explode     = 0;         // assembly only: shells pulled apart along z
ghost_alpha = 0.35;
shell_alpha = 0.55;
$fn         = 64;
eps         = 0.01;

/* [1. Case outline -- layout-revB "Case targets"] */
case_x0  = -8.75;        // outer left face  (54.0 wide)
case_x1  = 45.25;        // outer right face
case_y0  = -50.0;        // outer bottom face
case_y1  = 60.3;         // outer top face   (110.3 tall)
case_r   = 4.0;          // vertical corner radius, outer
edge_ch  = 0.8;          // 45 deg chamfer on the front and back edges
wall     = 1.2;          // left, right and bottom walls (MJF PA12: >= 1.0)
wall_top = 2.0;          // top wall: carries the key-plate rail (ledge 1.0 + rail 0.85 + clr)
panel_t  = 2.0;          // flat front panel, Z 0 .. 2.0
back_t   = 1.2;          // back wall
min_wall = 1.0;          // design rule used by the checks

/* [2. Waveshare ESP32-S3-Touch-AMOLED-2.41, rotated 180 deg] */
ws_holes      = [[0, 0], [36.5, 0], [0, 51.0], [36.5, 51.0]]; // M2 holes (dimensioned)
ws_hole_d     = 2.2;     // scaled
lens_x0       = -4.11;   // cover glass 44.72 x 61.56 (dimensioned)
lens_x1       = 40.61;
lens_y0       = -5.28;
lens_y1       = 56.28;
lens_r        = 1.0;
lens_t        = 0.70;
va_w          = 37.22;
va_h          = 49.65;
va_top_margin = 6.4;     // TBV (scaled)
ws_pcb_w      = 42.7;    // TBV (scaled), centred on the lens
ws_pcb_h      = 57.7;    // TBV
ws_pcb_t      = 1.2;     // TBV (ghost + pin length)
ws_pcb_rear_Z = 6.0;     // lens front -> PCB rear face (dimensioned)
ws_rear_parts_h = 3.5;   // dimensioned
hdr_h         = 2.5;     // TBV: J8 male header plastic
j8_col_x      = [32.73, 35.27];
j8_row_y0     = 5.18;
j8_rows       = 17;
j8_pitch      = 2.54;
j8_pin_Z1     = 17.6;    // TBV: J8 pin tips
key_x         = [9.75, 18.25, 26.75];
key_tip_y     = 54.8;    // TBV
key_Z         = 6.9;     // TBV (rear-side keys, Rev A.1 re-measure)
usb_y         = 25.5;    // dimensioned
usb_Z         = 7.8;     // TBV (shell Z 6.0..9.3)
usb_face_x    = -4.1;    // TBV
j7_y          = 12.8;    // TBV (battery connector; other candidate 37.5)
nb_conn_x0    = -4.9;    // TBV: left-edge part next to the USB-C reaches X -4.9 ...
nb_conn_y0    = 7.3;     //      ... over Y 7.3 .. 18.25 (scaled +-0.3)
nb_conn_y1    = 18.25;
nb_conn_Z0    = 6.0;     // TBV: assumed on the rear side (Z 6.0 ..)

/* [3. Glass pocket and Waveshare bosses] */
// The glass sits in a pocket (lens + lens_clr per side, depth lens_t + gasket) on a
// ledge; the ledge has a window big enough to pass the Waveshare PCB, so the
// module is FRONT-loaded.  Four bosses behind the PCB (faces at Z ws_pcb_rear_Z)
// stop it: glass front = boss face - 6.0 = Z 0 exactly.  Short pins in the boss
// faces locate the 4 M2 holes.  See README s6 for why screws cannot be used.
lens_clr      = 0.15;    // glass to pocket wall, per side
lens_gasket_t = 0.2;     // TBV: thin PSA/foam gasket on the ledge (also bonds the glass)
win_clr       = 0.3;     // ledge window around the PCB outline / edge parts
ws_boss_d     = 4.0;     // boss face Dia (annular ring ~3.6)
ws_pin_d      = 1.9;     // locating pin in the 2.2 hole
ws_pin_len    = 1.0;     // < PCB thickness: nothing reaches the display side
ws_bridge_w   = 3.0;     // bridge from boss to the nearest side wall (Y width)
ws_bridge_Z1  = 8.2;     // bridge/boss rear face (IDC socket starts at Z 8.5)
ins_tilt      = 12;      // deg: insert the module right side raised (left edge part clears the pocket)

/* [4. Control PCB -- layout-revB "Control PCB"] */
cp_x0  = -6.5;
cp_x1  = 43.0;
cp_y0  = -48.0;
cp_y1  = -7.0;
cp_r   = 2.0;
cp_t   = 1.6;
mount_holes = [[-4.0, -20.0], [40.0, -44.5], [18.25, -25.0]]; // control PCB M2 holes (written to layout-revB.md)
mount_shell = [true, true, false];  // true: the shell screw passes through this hole (clamps back shell + PCB)
cur_xy  = [5.0, -34.0];   // cursor control (stick or 5-way) axis
enc_xy  = [31.5, -34.0];  // colour knob encoder
menu_xy = [18.25, -18.1];  // B3F-1000 centre (KiCad SW1, Rev B.1); the cap axis is offset, see menu_cap_xy
side_y  = -23.0;            // SW2 actuator centre (KiCad, Rev B.1; was -24.0)
cp_hdr_xc  = 18.25;       // 2x17 shrouded header J1 on the PCB BOTTOM side (KiCad: pins -2.07..38.57 at Y -9.23/-11.77)
cp_hdr_yc  = -10.5;
cp_hdr_len = 50.8;        // AWHW-34: 16 x 2.54 + 10.16 (TBV)
cp_hdr_w   = 8.9;         // TBV
cp_hdr_h   = 9.0;         // TBV: PCB bottom -> top of the mated IDC socket (layout: to Z ~19.1)
cp_stub    = 1.8;         // THT lead stubs below the control PCB
cp_top_keep = 6.0;        // layout: top-side part height budget

/* [5. Encoder Bourns PEC11R-4215F-S0024] */
enc_body_w    = 12.4;    // ghost
enc_body_l    = 13.4;    // ghost
enc_body_h    = 6.5;     // TBV: PCB top -> mounting face (sets the control PCB depth)
enc_bush_d    = 7.0;
enc_bush_len  = 5.0;     // TBV
enc_shaft_d   = 6.0;
enc_shaft_len = 15.0;    // from the mounting face (brief)
enc_hole_d    = 7.4;
enc_washer_d  = 12.0;    // parts.md (Bourns)
enc_washer_id = 7.1;
enc_washer_t  = 0.5;     // TBV
enc_nut_af    = 10.0;    // parts.md (Bourns, probably A/F, TBV)
enc_nut_t     = 2.0;

/* [6. Knob Kilo OEJNI-75-4-7] */
knob_d          = 19.05;
knob_h          = 15.9;
knob_bore_depth = 13.0;
knob_cb_d       = 14.99;
knob_cb_h       = 4.0;   // TBV: OEJ-90 drawing gives 4.0; OEJ-75 not read
knob_gap        = 0.5;   // knob skirt above the front face
knob_setscrew_from_top = 7.2; // TBV (OEJ-90 value)
enc_flat_len    = 7.0;   // TBV: D-flat length from the shaft tip

/* [7. MENU Omron B3F-1000 (6 x 6) + printed cap] */
menu_sw         = 6.0;   // B3F-1000 body 6 x 6
menu_sw_body_h  = 3.5;   // TBV: body top above the PCB top
menu_plunger_h  = 4.3;   // Omron B3F table: B3F-1000 height 4.3 (PCB top -> plunger top)
menu_plunger_d  = 3.5;   // Omron drawing: round plunger Dia 3.5
menu_hole_d     = 9.4;
menu_cap_d      = 9.0;
menu_cap_proud  = 1.5;
menu_flange_d   = 12.0;
menu_flange_t   = 1.0;
menu_fl_hold    = 0.6;   // flange beyond the hole edge on its flat side (retention)
menu_boss_clr   = 0.3;   // flange flat to the H3 insert boss
menu_cap_gap    = 0.1;
menu_nub_d      = 3.0;
menu_dish       = 0.3;

/* [8. SIDE PTS645VL31-2 LFS (right edge) + printed cap] */
side_act_len   = 0.20;   // KiCad Rev B.1: SW2 actuator tip passes the PCB edge by 0.20
side_tip_x     = cp_x1 + side_act_len;  // 43.20 (KiCad SW2 F.Fab; was 43.56)
side_act_h     = 3.5;    // TBV: actuator axis above the PCB top face
side_hole_d    = 4.0;
side_cap_d     = 3.6;
side_flange_d  = 5.6;    // Rev A 6.4: reduced so the flange clears the panel (axis Z 5.0)
side_flange_max = 0.8;   // preferred flange thickness
side_cb_clr    = 0.2;
side_proud     = 1.0;
side_gap       = 0.25;   // pre-travel 0.2..0.3
side_body_d    = 6.79;   // ghost
side_body_w    = 7.5;    // ghost
side_body_h    = 6.0;    // ghost, TBV
side_act_d     = 3.5;    // ghost

/* [9. Waveshare keys: pins in a slide-in key plate (as Rev A.1)] */
kp_margin     = 2.5;
kp_clr        = 0.15;
kp_front_land = 1.2;
kp_tongue     = 0.8;
kp_tongue_t   = 0.85;
kp_land_in    = 1.0;
key_hole_d    = 2.0;
key_pin_d     = 1.6;
key_flange_d  = 3.0;
key_flange_t  = 0.8;
key_proud     = 0.8;
key_gap       = 0.2;

/* [10. USB-C opening (left wall, as Rev A.1)] */
usb_cut_w      = 9.5;
usb_cut_h      = 3.6;
usb_cut_r      = 1.5;
usb_relief_w   = 12.4;
usb_relief_h   = 6.8;
usb_relief_r   = 2.0;
usb_floor_t    = 1.0;
usb_frame_land = 1.0;
usb_face_gap   = 0.3;
usb_duct_t     = 1.0;
usb_shell_w    = 8.94;
usb_shell_h    = 3.26;
usb_shell_l    = 7.35;
usb_margin     = 0.5;    // required margin to neighbours / panel / parting plane

/* [11. Cursor control J: C&K THB001P] */
thb_h_top    = 19.45;    // TBV: PCB top -> stick top (drawing 19.45 +0.4/-0.1, label not read)
thb_shaft_h  = 7.75;     // ordering code "0": plastic shaft 7.75 (above the housing, TBV)
thb_dome_d   = 13.15;    // TBV: housing dome / collar Dia (drawing callout 13.15, label not read)
thb_frame_w  = 17.0;     // TBV: body X (parts.md "17 x 21")
thb_frame_l  = 21.0;     // TBV: body Y
thb_frame_h  = 6.0;      // TBV: metal frame top above the PCB (must clear the panel at 6.5)
thb_pivot_h  = 8.0;      // TBV: stick pivot above the PCB top
thb_tilt     = 26;       // deg, datasheet "26 deg max from vertical, full circle"
thb_shaft_d  = 4.0;      // TBV: drawing "Dia 4 +0.05/-0.1"
thb_hole_clr = 0.5;      // radial clearance of the panel opening
thb_hole_fallback = 16.0; // brief fallback if the computed value is not trusted

/* [12. Cursor control D: Alps SKQUCAA010 (Adafruit 504) + printed cross cap] */
// Dimensions from the Alps SKQUCAA010 drawing (style 3, snap-in), confirmed by the user's
// caliper on the physical Adafruit 504 (2026-11-15). Heights are above the PCB top face.
skqu_body_w     = 10.0;  // Alps: body square 10
skqu_body_h     = 3.15;  // Alps: housing top
skqu_collar_h   = 5.8;   // Alps: top of the round collar, where the square stem starts
skqu_collar_d   = 7.0;   // user caliper 2026-11-15: raised metal collar Dia 7.0 (black plastic ring 6.0 across flats, ~6.2 diagonal); sizes the cap-rear relief
skqu_top_h      = 10.0;  // Alps: stem top
skqu_stem_w     = 3.2;   // Alps: square stem 3.2 x 3.2
skqu_stem_trim  = 0;     // optional: mm cut off the stem top before fitting the cap (lowers the cap)
skqu_dir_travel = 0.4;   // Alps: 4-direction travel, measured 2.9 below the stem top
skqu_meas_below = 2.9;
skqu_pivot_h    = 1.23;  // Alps: centre of stem rotation
skqu_push       = 0.2;   // Alps: centre-push travel (user: clicks well under 1 mm)
dp_span         = 23.2;  // cross tip to tip   } opening stays 24.6 x 8.6 with the larger clearance
dp_arm          = 7.2;   // arm width          }
dp_clr          = 0.7;   // opening clearance per side (side shift at the face is about 0.5 at actuation)
dp_skin         = 0.8;   // cap material in front of the stem top
dp_lip          = 1.9;   // flange beyond the cross outline (1.2 over the panel; tilt over-travel stop)
dp_fl_t         = 0.8;
dp_fl_rin       = 7.6;   // flange ring starts outside the switch body corners (7.07)
dp_sock_fit     = 0.1;   // square socket over the stem: 3.3 square, press fit + a drop of glue
dp_relief       = 0.8;   // cap rear stops this far in front of the collar top (tilt + push + margin)
dp_relief_clr   = 0.5;   // radial clearance of the collar relief
dp_stop_margin  = 0.15;  // flange touches the panel this much after the switch actuates
dp_over_deg     = 2.0;   // tilt allowed past actuation for the checks

/* [13. Shell joint, inserts, screws] */
Z_split         = 13.0;  // parting plane (behind the USB duct, Z 12.2)
lip_h           = 1.5;
lip_t           = 1.2;
lip_clr         = 0.15;
insert_hole_d   = 3.2;   // M2 x 4 heat-set insert (ruthex RX-M2x4)
insert_len      = 4.0;
insert_wall     = 1.0;   // top-corner insert bosses
cp_boss_wall    = 1.2;   // control-PCB insert bosses
pilot_d         = 1.8;
pilot_len       = 1.0;
screw_clear_d   = 2.4;
screw_head_d    = 3.8;
screw_head_h    = 2.0;
cb_clr          = 0.4;
screw_engage    = 3.5;   // control-PCB screws into their inserts
screw_len_shell = 14;    // M2 x 14 SHCS: back shell + control PCB -> front-shell insert
screw_len_cp    = 6;     // M2 x 6 SHCS: control PCB -> insert (third hole)
back_column_wall = 1.0;
screw_len_top   = 14;    // M2 x 14 countersunk at the two top corners, head flush
csk_d           = 4.0;
top_column_wall = 1.2;

/* [14. Battery 102050, IDC, ribbon, back shell] */
batt_w       = 20.0;     // X
batt_l       = 50.0;     // Y
batt_t       = 10.0;     // Z
batt_x0      = 4.0;      // layout: X 4..24
batt_y0      = 0.0;      // layout: Y 0..50
batt_gap     = 0.3;      // Waveshare rear parts -> cell (0.3 foam tape on Kapton)
batt_clr     = 0.5;      // XY bay clearance (total)
batt_clr_z   = 0.15;     // cell back -> ribbon
batt_fence_t = 1.2;
batt_fence_h = 3.0;
idc_w        = 6.0;      // TBV: IDC socket width (X) on J8
idc_len      = 50.8;     // TBV: 17 x 2.54 + 7.62
idc_h        = 9.0;      // TBV: HHKR34S socket incl. strain relief (H3DDS-3406G; layout: Z 8.5 .. 17.5)
idc_fold_Z   = 19.5;     // layout: cable fold behind the socket
ribbon_t     = 0.9;      // TBV: AE34G 1.27 mm pitch grey ribbon (H3DDS-3406G)
ribbon_layers = 2;       // 45 deg crease: the run to -X and the run to -Y overlap behind the cell
ribbon_w     = 34 * 1.27;

// =====================================================================
// Derived values (never edit)
// =====================================================================
function mz(Z) = -Z;

cav_x0 = case_x0 + wall;
cav_x1 = case_x1 - wall;
cav_y0 = case_y0 + wall;
cav_y1 = case_y1 - wall_top;
cav_r  = max(case_r - wall, 0.5);

lens_cx   = (lens_x0 + lens_x1) / 2;
lens_cy   = (lens_y0 + lens_y1) / 2;
ws_pcb_x0 = lens_cx - ws_pcb_w / 2;
ws_pcb_x1 = lens_cx + ws_pcb_w / 2;
ws_pcb_y0 = lens_cy - ws_pcb_h / 2;
ws_pcb_y1 = lens_cy + ws_pcb_h / 2;
va_x0 = lens_cx - va_w / 2;
va_x1 = lens_cx + va_w / 2;
va_y1 = lens_y1 - va_top_margin;
va_y0 = va_y1 - va_h;

// glass pocket + ledge window
pocket_Z1 = lens_t + lens_gasket_t;                 // 0.9
pk_x0 = lens_x0 - lens_clr;
pk_x1 = lens_x1 + lens_clr;
pk_y0 = lens_y0 - lens_clr;
pk_y1 = lens_y1 + lens_clr;
win_x0 = ws_pcb_x0 - win_clr;
win_x1 = ws_pcb_x1 + win_clr;
win_y0 = ws_pcb_y0 - win_clr;
win_y1 = max(ws_pcb_y1, key_tip_y) + win_clr;
ledge_min = min(win_x0 - lens_x0, lens_x1 - win_x1, win_y0 - lens_y0, lens_y1 - win_y1); // glass on ledge
nb_over   = lens_x0 - nb_conn_x0;                   // left-edge part beyond the glass edge
ws_glass_front_Z = ws_pcb_rear_Z - 6.0;             // boss face sets the glass: must be 0
ledge_glass_front_Z = pocket_Z1 - lens_gasket_t - lens_t; // ledge sets the glass: must be 0
ws_bridge_side = [for (h = ws_holes) h[0] < lens_cx ? -1 : 1];

// control PCB stack
cp_top_Z = panel_t + enc_body_h;                   // 8.5
cp_bot_Z = cp_top_Z + cp_t;                        // 10.1
cp_hdr_Z1 = cp_bot_Z + cp_hdr_h;                   // 19.1
cp_boss_d = insert_hole_d + 2 * cp_boss_wall;      // 5.6

// battery / IDC / ribbon / depth
batt_Z0   = ws_pcb_rear_Z + ws_rear_parts_h + batt_gap;   // 9.8
batt_Z1   = batt_Z0 + batt_t;                             // 19.8
idc_Z0    = ws_pcb_rear_Z + hdr_h;                        // 8.5
idc_Z1    = idc_Z0 + idc_h;                               // 17.5
idc_xc    = (j8_col_x[0] + j8_col_x[1]) / 2;
idc_yc    = j8_row_y0 + (j8_rows - 1) * j8_pitch / 2;
idc_x0    = idc_xc - idc_w / 2;
idc_x1    = idc_xc + idc_w / 2;
idc_y0    = idc_yc - idc_len / 2;
idc_y1    = idc_yc + idc_len / 2;
rib_Z0    = batt_Z1 + batt_clr_z;
rib_Z1    = rib_Z0 + ribbon_layers * ribbon_t;
back_in_Z = max(rib_Z1, idc_fold_Z + 0.3, cp_hdr_Z1 + ribbon_t + 0.3);
back_out_Z = back_in_Z + back_t;                          // total depth (front face is Z 0)
bay_x0 = batt_x0 - batt_clr / 2;
bay_x1 = batt_x0 + batt_w + batt_clr / 2;
bay_y0 = batt_y0 - batt_clr / 2;
bay_y1 = batt_y0 + batt_l + batt_clr / 2;

// top-corner shell bosses: in the top wall corners, insert skin 1.0 to the top face
insert_boss_d = insert_hole_d + 2 * insert_wall;          // 5.2
top_boss_y    = case_y1 - min_wall - insert_hole_d / 2;   // 57.7
top_boss_xy   = [[case_x0 + case_r - 0.05, top_boss_y], [case_x1 - case_r + 0.05, top_boss_y]];
top_boss_len  = Z_split - panel_t;
top_column_d  = screw_clear_d + 2 * top_column_wall;
top_screw_tip_Z = back_out_Z - screw_len_top;
cb_d          = screw_head_d + cb_clr;
back_column_d = cb_d + 2 * back_column_wall;
cp_screw_tip_Z   = cp_top_Z - screw_engage;               // 5.0
shell_head_seat_Z = cp_screw_tip_Z + screw_len_shell;     // 19.0
cp3_tip_Z        = cp_bot_Z - screw_len_cp;               // M2 x 6 from the PCB bottom

// encoder + knob
enc_face_Z   = panel_t;                                   // mounting face on the panel inner face
shaft_tip_Z  = enc_face_Z - enc_shaft_len;                // -13.0
bush_tip_Z   = enc_face_Z - enc_bush_len;                 // -3.0
knob_bot_Z   = -knob_gap;
knob_top_Z   = knob_bot_Z - knob_h;
knob_protrusion = -knob_top_Z;                            // above the front face
knob_engage  = knob_bot_Z - shaft_tip_Z;                  // shaft length inside the knob
nut_top_Z    = -(enc_washer_t + enc_nut_t);               // stack on the front face
knob_cb_top_Z = knob_bot_Z - knob_cb_h;
nut_ac       = enc_nut_af / cos(30);

// MENU: the cap axis moves +Y off the switch until its flange flat clears the H3 insert boss
menu_h3            = mount_holes[2];
menu_fl_flat       = menu_hole_d / 2 + menu_fl_hold;      // flange flat, towards H3 (-Y)
menu_cap_xy        = [menu_xy[0], max(menu_xy[1], menu_h3[1] + cp_boss_d / 2 + menu_boss_clr + menu_fl_flat)];
menu_nub_off       = menu_xy - menu_cap_xy;               // nub (cap frame) sits on the plunger axis
menu_plunger_Z     = cp_top_Z - menu_plunger_h;
menu_contact_local = (panel_t + menu_flange_t) - (menu_plunger_Z - menu_cap_gap);
menu_pocket_d      = menu_plunger_d + 0.3;
menu_stem_h        = menu_flange_t + panel_t + menu_cap_proud;
menu_dish_c        = menu_cap_d * 0.8;
menu_dish_R        = (pow(menu_dish_c, 2) / 4 + pow(menu_dish, 2)) / (2 * menu_dish);

// SIDE
side_Z        = cp_top_Z - side_act_h;                    // 5.0
side_cap_x0   = side_tip_x + side_gap;
side_flange_t = min(side_flange_max, case_x1 - min_wall - side_cap_x0); // thinner if the tip is far out
side_fl_x1    = side_cap_x0 + side_flange_t;
side_cb_depth = side_fl_x1 - cav_x1;
side_wall_left = case_x1 - max(side_fl_x1, cav_x1);
side_cap_len  = (case_x1 - side_cap_x0) + side_proud;

// key plate (Rev A.1 with the 2.0 top wall)
kp_x0 = min(key_x) - key_flange_d / 2 - kp_margin;
kp_x1 = max(key_x) + key_flange_d / 2 + kp_margin;
kp_Z0 = key_Z - key_hole_d / 2 - kp_front_land;
kp_ty1 = cav_y1 + kp_tongue_t;
kp_in_y = cav_y1 - kp_land_in;
kp_land_Z1 = Z_split - lip_h - lip_clr;
key_inner_ext = kp_in_y - key_flange_t - (key_tip_y + key_gap);
key_pin_len   = key_inner_ext + key_flange_t + kp_land_in + wall_top + key_proud;

// USB
usb_floor_x1 = usb_face_x - usb_face_gap;
usb_floor_x0 = usb_floor_x1 - usb_floor_t;
usb_duct_hw  = usb_relief_w / 2 + usb_duct_t;
usb_frame_hw = usb_cut_w / 2 + usb_frame_land;
usb_duct_hh  = usb_relief_h / 2 + usb_duct_t;

// J: thumbstick opening (computed from the stick geometry)
thb_band_lo = (cp_top_Z - panel_t) - thb_pivot_h;         // panel inner face height above the pivot
thb_band_hi = cp_top_Z - thb_pivot_h;                     // panel front face height above the pivot
thb_env_r   = max(abs(thb_band_lo), abs(thb_band_hi)) * tan(thb_tilt) + (thb_shaft_d / 2) / cos(thb_tilt);
thb_hole_d  = 2 * (max(thb_dome_d / 2, thb_env_r) + thb_hole_clr);
thb_top_Z   = cp_top_Z - thb_h_top;                       // stick top
thb_dome_Z  = cp_top_Z - (thb_h_top - thb_shaft_h);       // housing top

// D: cross cap
skqu_top_Z    = cp_top_Z - (skqu_top_h - skqu_stem_trim);  // -1.5: the stem stands proud of the panel face
skqu_collar_Z = cp_top_Z - skqu_collar_h;                  // 2.7
skqu_pivot_Z  = cp_top_Z - skqu_pivot_h;                   // 7.27
skqu_body_Z0  = cp_top_Z - skqu_body_h;                    // 5.35
dp_lever     = skqu_top_h - skqu_meas_below - skqu_pivot_h; // 5.87: pivot to the travel measuring point
dp_act_deg   = atan(skqu_dir_travel / dp_lever);           // tilt at actuation (3.9 deg)
dp_max_deg   = dp_act_deg + dp_over_deg;
dp_fl_r      = dp_span / 2 + dp_lip;                      // flange tip radius on an arm axis
dp_rise_act  = dp_fl_r * sin(dp_act_deg) - (skqu_pivot_Z - panel_t) * (1 - cos(dp_act_deg));
dp_fl_Z0     = panel_t + dp_rise_act + dp_stop_margin;    // flange front face (rest)
dp_fl_Z1     = dp_fl_Z0 + dp_fl_t;
dp_top_Z     = skqu_top_Z - dp_skin;                      // cross top
dp_proud     = -dp_top_Z;                                 // cross top in front of the panel face
dp_sock_Z1   = skqu_collar_Z - dp_relief;                 // socket open end = cap rear over the collar
dp_sock_od   = skqu_collar_d + 2 * dp_relief_clr;         // collar relief Dia in the cap rear
dp_cap_len   = dp_fl_Z1 - dp_top_Z;
dp_lat_front = skqu_pivot_Z * sin(dp_act_deg);            // cross side shift at the front face, at actuation
dp_sink      = (dp_span / 2) * sin(dp_act_deg) - dp_proud; // pressed arm tip behind the front face (<0: stays proud)

// ---------------- design-rule checks (re-run by validate_case.py for both variants) ----------------
assert(ctrl == "J" || ctrl == "D", "ctrl must be \"J\" or \"D\"");
assert(abs(ws_glass_front_Z) < 0.001 && abs(ledge_glass_front_Z) < 0.001, "glass not flush with Z 0");
assert(ws_bridge_Z1 <= idc_Z0 - 0.3, "Waveshare boss bridges reach the IDC socket");
assert(win_x0 > pk_x0 && win_x1 < pk_x1 && win_y0 > pk_y0 && win_y1 < pk_y1, "ledge window outside the glass pocket");
assert(ledge_min >= 0.6, "glass rests on less than 0.6 mm of ledge");
assert(-nb_over * cos(ins_tilt) + nb_conn_Z0 * sin(ins_tilt) >= 0.2, "left-edge part hits the pocket wall at the insertion tilt");
assert(kp_Z0 > panel_t, "key-plate slot breaks into the front panel");
assert(wall_top - kp_tongue_t - kp_clr >= min_wall - 1e-6, "key-plate rail ledge thinner than 1.0");
assert(key_inner_ext > 0.5, "key pins too short");
assert(side_flange_t >= 0.4, "SIDE flange below 0.4: move SW2 inboard");
assert(side_wall_left >= min_wall - 1e-6, "right wall behind the SIDE counterbore < 1.0");
assert(side_Z - side_flange_d / 2 >= panel_t + 0.1 && side_Z + side_flange_d / 2 <= cp_top_Z - 0.1, "SIDE flange hits the panel or the control PCB");
assert(enc_bush_len - panel_t >= enc_washer_t + enc_nut_t, "encoder bushing too short for washer + nut");
assert(knob_engage <= knob_bore_depth, "shaft bottoms in the knob bore: raise knob_gap");
assert(nut_top_Z >= knob_cb_top_Z + 0.3, "nut + washer do not fit under the knob counterbore (depth)");
assert(knob_cb_d - nut_ac >= 0.6 && knob_cb_d - enc_washer_d >= 0.6, "nut or washer too wide for the counterbore");
assert(shell_head_seat_Z <= back_out_Z - screw_head_h, "M2 x 14 shell screw head stands proud");
assert(Z_split - top_screw_tip_Z >= 2.5 && Z_split - top_screw_tip_Z <= insert_len + pilot_len, "top screw engagement out of range");
assert(cp3_tip_Z >= cp_top_Z - insert_len - pilot_len && cp3_tip_Z <= cp_top_Z - 2.5, "third control screw engagement out of range");
assert(cp_top_Z - insert_len - pilot_len >= panel_t + 0.5, "control-PCB insert pilot breaks into the panel");
assert(usb_Z - usb_duct_hh >= panel_t + usb_margin && usb_Z + usb_duct_hh <= Z_split - usb_margin, "USB duct within 0.5 of the panel or the parting plane");
assert(usb_y - usb_frame_hw - nb_conn_y1 >= usb_margin - 1e-6 && nb_conn_x0 - usb_floor_x0 >= usb_margin - 1e-6, "USB frame/duct within 0.5 of the left-edge part");
assert(menu_contact_local < menu_stem_h - 1.5, "MENU cap pocket too deep");
assert(norm(menu_cap_xy - menu_h3) - cp_boss_d / 2 - menu_fl_flat >= menu_boss_clr - 1e-6, "MENU flange flat on the H3 boss");
assert(menu_fl_flat < menu_flange_d / 2, "MENU flange flat outside the flange");
assert(norm(menu_nub_off) + menu_nub_d / 2 < menu_cap_d / 2, "MENU nub outside the cap");
assert(norm(menu_cap_xy - enc_xy) - enc_hole_d / 2 - menu_hole_d / 2 >= 1.0, "MENU hole within 1.0 of the encoder hole");
assert(ctrl != "J" || norm(menu_cap_xy - cur_xy) - thb_hole_d / 2 - menu_hole_d / 2 >= 1.0, "MENU hole within 1.0 of the stick opening");
assert((cp_top_Z - menu_sw_body_h) - (panel_t + menu_flange_t) >= 0.8, "MENU flange too close to the switch body");
assert(batt_Z0 >= ws_pcb_rear_Z + ws_rear_parts_h + 0.2, "cell on the Waveshare rear parts");
assert(back_in_Z >= idc_fold_Z + 0.3, "back wall on the IDC fold");
assert(ctrl != "J" || thb_frame_h <= enc_body_h - 0.2, "THB001P frame reaches the panel");
assert(ctrl != "D" || dp_fl_Z0 - panel_t >= skqu_push + 0.2, "d-pad flange gap smaller than the centre-push travel");
assert(ctrl != "D" || dp_fl_Z0 > skqu_top_Z, "d-pad flange in front of the 5-way stem top");
assert(ctrl != "D" || dp_sock_Z1 - skqu_top_Z >= 2.0, "d-pad socket grips less than 2.0 of the stem");
assert(ctrl != "D" || dp_sock_Z1 <= dp_fl_Z0, "d-pad socket rear behind the flange");
assert(ctrl != "D" || dp_lat_front <= dp_clr, "d-pad side shift at the face exceeds the opening clearance");
assert(ctrl != "D" || case_x0 - (cur_xy[0] - dp_span / 2 - dp_clr) <= -min_wall, "cross opening within 1.0 of the left face");

// =====================================================================
// Helpers (Z arguments are layout depth)
// =====================================================================
module rrect2d(x0, x1, y0, y1, r) {
    rr = max(min(r, (x1 - x0) / 2 - eps, (y1 - y0) / 2 - eps), eps);
    translate([x0 + rr, y0 + rr]) offset(r = rr) square([x1 - x0 - 2 * rr, y1 - y0 - 2 * rr]);
}
module zext(Z0, Z1) { translate([0, 0, mz(Z1)]) linear_extrude(height = Z1 - Z0) children(); }
module zcyl(x, y, d, Z0, Z1) { translate([x, y, mz(Z1)]) cylinder(d = d, h = Z1 - Z0); }
module zbox(x0, x1, y0, y1, Z0, Z1) { translate([x0, y0, mz(Z1)]) cube([x1 - x0, y1 - y0, Z1 - Z0]); }
module cbox(x0, x1, y0, y1, r, Z0, Z1, chf = 0, chb = 0) {
    hull() {
        if (chf > 0) zext(Z0, Z0 + eps) rrect2d(x0 + chf, x1 - chf, y0 + chf, y1 - chf, r - chf);
        if (chb > 0) zext(Z1 - eps, Z1) rrect2d(x0 + chb, x1 - chb, y0 + chb, y1 - chb, r - chb);
        zext(Z0 + chf, Z1 - chb) rrect2d(x0, x1, y0, y1, r);
    }
}
module xslot(w, h, r, x0, x1, y, Z) {
    translate([x0, y, mz(Z)]) rotate([0, 90, 0]) linear_extrude(height = x1 - x0)
        rrect2d(-h / 2, h / 2, -w / 2, w / 2, r);
}
module case_prism(Z0, Z1) { zext(Z0, Z1) rrect2d(case_x0, case_x1, case_y0, case_y1, case_r); }
module cav2d(inset = 0) { rrect2d(cav_x0 + inset, cav_x1 - inset, cav_y0 + inset, cav_y1 - inset, cav_r - inset); }
module cross2d() {
    translate(cur_xy) {
        square([dp_span, dp_arm], center = true);
        square([dp_arm, dp_span], center = true);
    }
}

// =====================================================================
// FRONT SHELL
// =====================================================================
module ws_bosses() {
    for (i = [0 : len(ws_holes) - 1]) {
        h = ws_holes[i];
        zcyl(h[0], h[1], ws_boss_d, ws_pcb_rear_Z, ws_bridge_Z1);
        zcyl(h[0], h[1], ws_pin_d, ws_pcb_rear_Z - ws_pin_len, ws_pcb_rear_Z + eps);
        if (ws_bridge_side[i] < 0)
            zbox(cav_x0 - eps, h[0], h[1] - ws_bridge_w / 2, h[1] + ws_bridge_w / 2, ws_pcb_rear_Z, ws_bridge_Z1);
        else
            zbox(h[0], cav_x1 + eps, h[1] - ws_bridge_w / 2, h[1] + ws_bridge_w / 2, ws_pcb_rear_Z, ws_bridge_Z1);
    }
}
module front_bosses() {
    intersection() {
        case_prism(0, Z_split);
        union() {
            ws_bosses();
            for (p = mount_holes) zcyl(p[0], p[1], cp_boss_d, panel_t - eps, cp_top_Z);
            for (p = top_boss_xy) zcyl(p[0], p[1], insert_boss_d, panel_t - eps, Z_split);
        }
    }
}
module usb_duct() {
    zbox(case_x0 + eps, usb_floor_x0, usb_y - usb_duct_hw, usb_y + usb_duct_hw, usb_Z - usb_duct_hh, usb_Z + usb_duct_hh);
    zbox(usb_floor_x0 - eps, usb_floor_x1, usb_y - usb_frame_hw, usb_y + usb_frame_hw, usb_Z - usb_duct_hh, usb_Z + usb_duct_hh);
}
module insert_hole(p, Zface) {
    zcyl(p[0], p[1], insert_hole_d, Zface - insert_len, Zface + eps);
    zcyl(p[0], p[1], pilot_d, Zface - insert_len - pilot_len, Zface - insert_len + eps);
}
module glass_cut() {
    zext(-1, pocket_Z1) rrect2d(pk_x0, pk_x1, pk_y0, pk_y1, lens_r + lens_clr);
    zext(pocket_Z1 - eps, panel_t + eps) rrect2d(win_x0, win_x1, win_y0, win_y1, 1.0);
    // ledge notches for the left-edge parts that stand past the PCB (USB-C mouth, neighbour part)
    zbox(pk_x0 + eps, win_x0 + eps, usb_y - usb_shell_w / 2 - win_clr, usb_y + usb_shell_w / 2 + win_clr, pocket_Z1 - eps, panel_t + eps);
    zbox(pk_x0 + eps, win_x0 + eps, nb_conn_y0 - win_clr, nb_conn_y1 + win_clr, pocket_Z1 - eps, panel_t + eps);
}
module cursor_cut() {
    if (ctrl == "J") zcyl(cur_xy[0], cur_xy[1], thb_hole_d, -1, panel_t + 1);
    else zext(-1, panel_t + 1) offset(r = dp_clr) cross2d();
}
module front_cuts() {
    glass_cut();
    cursor_cut();
    zcyl(enc_xy[0], enc_xy[1], enc_hole_d, -1, panel_t + 1);
    zcyl(menu_cap_xy[0], menu_cap_xy[1], menu_hole_d, -1, panel_t + 1);
    // key-plate slot through the top wall (open to the back) + rail grooves
    zbox(kp_x0, kp_x1, cav_y1 - 1, case_y1 + 1, kp_Z0, Z_split + 1);
    zbox(kp_x0 - kp_tongue - kp_clr, kp_x1 + kp_tongue + kp_clr, cav_y1 - 1, kp_ty1 + kp_clr, kp_Z0, Z_split + 1);
    // SIDE
    translate([cav_x1 - 1, side_y, mz(side_Z)]) rotate([0, 90, 0]) cylinder(d = side_hole_d, h = wall + 2, $fn = 32);
    if (side_cb_depth > 0)
        translate([cav_x1 - 1, side_y, mz(side_Z)]) rotate([0, 90, 0])
            cylinder(d = side_flange_d + 2 * side_cb_clr, h = 1 + side_cb_depth, $fn = 48);
    // USB-C: overmold pocket + opening
    xslot(usb_relief_w, usb_relief_h, usb_relief_r, case_x0 - 1, usb_floor_x0, usb_y, usb_Z);
    xslot(usb_cut_w, usb_cut_h, usb_cut_r, case_x0 - 1, usb_floor_x1 + 1, usb_y, usb_Z);
    // inserts
    for (p = mount_holes) insert_hole(p, cp_top_Z);
    for (p = top_boss_xy) insert_hole(p, Z_split);
}
module front_shell() {
    difference() {
        union() {
            difference() {
                cbox(case_x0, case_x1, case_y0, case_y1, case_r, 0, Z_split, chf = edge_ch);
                zext(panel_t, Z_split + 1) cav2d();
            }
            front_bosses();
            usb_duct();
        }
        front_cuts();
    }
}

// =====================================================================
// BACK SHELL
// =====================================================================
module skirt() {
    difference() {
        zext(Z_split - lip_h, Z_split + eps) difference() { cav2d(lip_clr); cav2d(lip_clr + lip_t); }
        for (p = top_boss_xy) zcyl(p[0], p[1], insert_boss_d + 4 * lip_clr, Z_split - lip_h - 1, Z_split + 1);
        // USB duct (left wall) and control-PCB header ends (both walls)
        zbox(cav_x0 - 1, usb_floor_x1 + 0.5, usb_y - usb_duct_hw - 0.5, usb_y + usb_duct_hw + 0.5, Z_split - lip_h - 1, Z_split + 1);
        zbox(cav_x0 - 1, cav_x1 + 1, cp_hdr_yc - cp_hdr_w / 2 - 0.5, cp_hdr_yc + cp_hdr_w / 2 + 0.5, Z_split - lip_h - 1, Z_split + 1);
    }
}
module batt_fence() {   // top-end fence only: the ribbon crosses the other sides of the cell
    zbox(bay_x0 - batt_fence_t, bay_x1 + batt_fence_t, bay_y1, bay_y1 + batt_fence_t, batt_Z1 - batt_fence_h, back_in_Z + eps);
}
module back_shell() {
    difference() {
        union() {
            difference() {
                cbox(case_x0, case_x1, case_y0, case_y1, case_r, Z_split, back_out_Z, chb = edge_ch);
                zext(Z_split - 1, back_in_Z) cav2d();
            }
            skirt();
            intersection() {
                case_prism(cp_bot_Z - 1, back_out_Z);
                union() {
                    for (i = [0 : len(mount_holes) - 1]) if (mount_shell[i])
                        zcyl(mount_holes[i][0], mount_holes[i][1], back_column_d, cp_bot_Z, back_in_Z + eps);
                    for (p = top_boss_xy) zcyl(p[0], p[1], top_column_d, Z_split, back_in_Z + eps);
                    batt_fence();
                }
            }
        }
        for (i = [0 : len(mount_holes) - 1]) if (mount_shell[i]) {
            p = mount_holes[i];
            zcyl(p[0], p[1], screw_clear_d, cp_bot_Z - 1, shell_head_seat_Z + eps);
            zcyl(p[0], p[1], cb_d, shell_head_seat_Z, back_out_Z + 1);
        }
        for (p = top_boss_xy) {
            zcyl(p[0], p[1], screw_clear_d, Z_split - 1, back_out_Z + 1);
            translate([p[0], p[1], mz(back_out_Z) - eps])
                cylinder(d1 = csk_d, d2 = screw_clear_d, h = (csk_d - screw_clear_d) / 2 + eps);
        }
    }
}

// =====================================================================
// SMALL PRINTED PARTS (local frames: actuation axis = +z, rear face at z = 0)
// =====================================================================
module menu_cap() {
    top = menu_stem_h;
    difference() {
        union() {
            // flange with a flat on the -Y side (clears the H3 boss; the boss also stops it rotating)
            intersection() {
                cylinder(d = menu_flange_d, h = menu_flange_t);
                translate([-menu_flange_d / 2, -menu_fl_flat, -eps]) cube([menu_flange_d, menu_flange_d, menu_flange_t + 2 * eps]);
            }
            cylinder(d = menu_cap_d, h = top);
            if (menu_contact_local < 0)   // nub on the plunger axis (offset from the cap axis)
                translate([menu_nub_off[0], menu_nub_off[1], menu_contact_local]) cylinder(d = menu_nub_d, h = -menu_contact_local + eps);
        }
        if (menu_contact_local > 0)
            translate([menu_nub_off[0], menu_nub_off[1], -eps]) cylinder(d = menu_pocket_d, h = menu_contact_local + eps);
        if (menu_dish > 0)
            translate([0, 0, top + menu_dish_R - menu_dish]) sphere(r = menu_dish_R, $fn = 128);
    }
}
module side_cap() {
    cylinder(d = side_flange_d, h = side_flange_t);
    cylinder(d = side_cap_d, h = side_cap_len, $fn = 32);
}
module key_pin() {
    cylinder(d = key_pin_d, h = key_pin_len, $fn = 24);
    translate([0, 0, key_inner_ext]) cylinder(d = key_flange_d, h = key_flange_t, $fn = 24);
}
// d-pad cross cap, modelled in assembly coordinates
module dpad_cap() {
    difference() {
        union() {
            zext(dp_top_Z, dp_fl_Z0 + eps) cross2d();
            zext(dp_fl_Z0, dp_fl_Z1) difference() {
                intersection() { offset(delta = dp_lip) cross2d(); cav2d(0.3); }
                translate(cur_xy) circle(r = dp_fl_rin);
            }
        }
        // square socket over the stem (blind at the front, dp_skin thick)
        zext(skqu_top_Z, dp_sock_Z1 + eps) translate(cur_xy) square(skqu_stem_w + dp_sock_fit, center = true);
        // relief over the round collar
        zcyl(cur_xy[0], cur_xy[1], dp_sock_od, dp_sock_Z1, dp_fl_Z1 + 1);
        // 0.3 x 0.3 edge rebate on the cross top (breaks the sharp edge)
        translate([0, 0, mz(dp_top_Z) - 0.3]) linear_extrude(height = 0.3 + eps)
            difference() { offset(delta = 1) cross2d(); offset(delta = -0.3) cross2d(); }
    }
}
module dpad_cap_flat() {    // print orientation: top face down
    translate([0, 0, mz(dp_top_Z)]) rotate([180, 0, 0]) translate([-cur_xy[0], -cur_xy[1], 0]) dpad_cap();
}
module key_plate() {
    difference() {
        union() {
            zbox(kp_x0 + kp_clr, kp_x1 - kp_clr, cav_y1, case_y1, kp_Z0 + kp_clr, Z_split);
            zbox(kp_x0 - kp_tongue, kp_x1 + kp_tongue, cav_y1, kp_ty1, kp_Z0 + kp_clr, Z_split);
            zbox(kp_x0 - kp_tongue, kp_x1 + kp_tongue, kp_in_y, cav_y1 + eps, kp_Z0 + kp_clr, kp_land_Z1);
        }
        for (kx = key_x)
            translate([kx, kp_in_y - 1, mz(key_Z)]) rotate([-90, 0, 0]) cylinder(d = key_hole_d, h = kp_land_in + wall_top + 2, $fn = 24);
    }
}
module key_plate_flat() { translate([-kp_x0, -mz(Z_split), case_y1]) rotate([-90, 0, 0]) key_plate(); }

module menu_cap_placed() { translate([menu_cap_xy[0], menu_cap_xy[1], mz(panel_t + menu_flange_t)]) menu_cap(); }
module side_cap_placed() { translate([side_cap_x0, side_y, mz(side_Z)]) rotate([0, 90, 0]) side_cap(); }
module key_pins_placed() { for (kx = key_x) translate([kx, key_tip_y + key_gap, mz(key_Z)]) rotate([-90, 0, 0]) key_pin(); }

module small_parts() {
    translate([0, 0, max(0, -menu_contact_local)]) menu_cap();
    translate([-menu_flange_d / 2, menu_flange_d / 2 + 4, 0]) key_plate_flat();
    translate([menu_flange_d / 2 + side_flange_d / 2 + 3, 0, 0]) side_cap();
    for (i = [0 : len(key_x) - 1])
        translate([menu_flange_d / 2 + side_flange_d + 6 + i * (key_flange_d + 3), 0, 0]) key_pin();
    if (ctrl == "D") translate([dp_span / 2 + menu_flange_d / 2 + 30, 0, 0]) dpad_cap_flat();
}
module print_plate() {
    translate([0, 0, 0]) rotate([180, 0, 0]) front_shell();               // front face down
    translate([case_x1 - case_x0 + 10, 0, back_out_Z]) back_shell();       // back face down
    translate([case_x0, case_y0 - 45, 0]) small_parts();
}

// =====================================================================
// GHOST STAND-INS (assembly only)
// =====================================================================
module ghost_waveshare() {
    color([0.55, 0.75, 1.0], ghost_alpha) zext(0, lens_t) rrect2d(lens_x0, lens_x1, lens_y0, lens_y1, lens_r);
    color([0.1, 0.1, 0.1], ghost_alpha) zext(lens_t, ws_pcb_rear_Z - ws_pcb_t)
        rrect2d(ws_pcb_x0 + 0.5, ws_pcb_x1 - 0.5, ws_pcb_y0 + 0.5, ws_pcb_y1 - 0.5, 1);
    color([0.1, 0.5, 0.2], ghost_alpha) zext(ws_pcb_rear_Z - ws_pcb_t, ws_pcb_rear_Z) difference() {
        rrect2d(ws_pcb_x0, ws_pcb_x1, ws_pcb_y0, ws_pcb_y1, 1.5);
        for (h = ws_holes) translate(h) circle(d = ws_hole_d);
    }
    color([0.3, 0.3, 0.3], ghost_alpha) zext(ws_pcb_rear_Z, ws_pcb_rear_Z + ws_rear_parts_h)
        rrect2d(ws_pcb_x0 + 6, j8_col_x[0] - 2.5, ws_pcb_y0 + 6, ws_pcb_y1 - 6, 1);
    color([0.8, 0.8, 0.8], ghost_alpha)
        zbox(usb_face_x, usb_face_x + usb_shell_l, usb_y - usb_shell_w / 2, usb_y + usb_shell_w / 2,
             usb_Z - usb_shell_h / 2, usb_Z + usb_shell_h / 2);
    color([0.9, 0.9, 0.9], ghost_alpha)
        for (kx = key_x) zbox(kx - 1.75, kx + 1.75, key_tip_y - 2.5, key_tip_y, key_Z - 0.9, key_Z + 0.9);
    color([0.15, 0.15, 0.15], ghost_alpha)
        zbox(j8_col_x[0] - j8_pitch / 2, j8_col_x[1] + j8_pitch / 2, j8_row_y0 - j8_pitch / 2,
             j8_row_y0 + (j8_rows - 1) * j8_pitch + j8_pitch / 2, ws_pcb_rear_Z, idc_Z0);
}
module ghost_idc_ribbon() {
    color([0.25, 0.25, 0.25], ghost_alpha) zbox(idc_x0, idc_x1, idc_y0, idc_y1, idc_Z0, idc_Z1);
    color([0.7, 0.7, 0.75], ghost_alpha) {
        zbox(idc_x0, idc_x1, idc_yc - ribbon_w / 2, idc_yc + ribbon_w / 2, idc_Z1, idc_fold_Z);
        // flat run behind the cell and the 45 deg crease (2 layers), then down to the control header
        zbox(cp_hdr_xc - ribbon_w / 2, idc_x1, cp_hdr_yc, idc_yc + ribbon_w / 2, rib_Z0, rib_Z1);
    }
}
module ghost_control() {
    color([0.1, 0.45, 0.15], ghost_alpha) zext(cp_top_Z, cp_bot_Z) difference() {
        rrect2d(cp_x0, cp_x1, cp_y0, cp_y1, cp_r);
        for (h = mount_holes) translate(h) circle(d = 2.2);
    }
    color([0.6, 0.6, 0.65], ghost_alpha) {
        zbox(enc_xy[0] - enc_body_w / 2, enc_xy[0] + enc_body_w / 2, enc_xy[1] - enc_body_l / 2, enc_xy[1] + enc_body_l / 2, enc_face_Z, cp_top_Z);
        zcyl(enc_xy[0], enc_xy[1], enc_bush_d, bush_tip_Z, enc_face_Z);
        zcyl(enc_xy[0], enc_xy[1], enc_shaft_d, shaft_tip_Z, bush_tip_Z);
        zcyl(enc_xy[0], enc_xy[1], enc_washer_d, -enc_washer_t, 0);
        translate([enc_xy[0], enc_xy[1], mz(nut_top_Z)]) cylinder(d = nut_ac, h = enc_nut_t, $fn = 6);
    }
    color([0.2, 0.2, 0.2], ghost_alpha) {
        zbox(menu_xy[0] - menu_sw / 2, menu_xy[0] + menu_sw / 2, menu_xy[1] - menu_sw / 2, menu_xy[1] + menu_sw / 2,
             cp_top_Z - menu_sw_body_h, cp_top_Z);
        zcyl(menu_xy[0], menu_xy[1], menu_plunger_d, menu_plunger_Z, cp_top_Z - menu_sw_body_h);
        zbox(side_tip_x - side_act_len - side_body_d, side_tip_x - side_act_len, side_y - side_body_w / 2, side_y + side_body_w / 2,
             cp_top_Z - side_body_h, cp_top_Z);
        translate([side_tip_x - side_act_len, side_y, mz(side_Z)]) rotate([0, 90, 0]) cylinder(d = side_act_d, h = side_act_len);
        zbox(cp_hdr_xc - cp_hdr_len / 2, cp_hdr_xc + cp_hdr_len / 2, cp_hdr_yc - cp_hdr_w / 2, cp_hdr_yc + cp_hdr_w / 2, cp_bot_Z, cp_hdr_Z1);
    }
    if (ctrl == "J") color([0.3, 0.3, 0.35], ghost_alpha) {
        zbox(cur_xy[0] - thb_frame_w / 2, cur_xy[0] + thb_frame_w / 2, cur_xy[1] - thb_frame_l / 2, cur_xy[1] + thb_frame_l / 2,
             cp_top_Z - thb_frame_h, cp_top_Z);
        zcyl(cur_xy[0], cur_xy[1], thb_dome_d, thb_dome_Z, cp_top_Z - thb_frame_h);
        zcyl(cur_xy[0], cur_xy[1], thb_shaft_d, thb_top_Z, thb_dome_Z);
    } else color([0.3, 0.3, 0.35], ghost_alpha) {
        zbox(cur_xy[0] - skqu_body_w / 2, cur_xy[0] + skqu_body_w / 2, cur_xy[1] - skqu_body_w / 2, cur_xy[1] + skqu_body_w / 2,
             skqu_body_Z0, cp_top_Z);
        zcyl(cur_xy[0], cur_xy[1], skqu_collar_d, skqu_collar_Z, skqu_body_Z0);
        zext(skqu_top_Z, skqu_collar_Z) translate(cur_xy) square(skqu_stem_w, center = true);
    }
}
module ghost_knob() {
    color([0.1, 0.1, 0.1], ghost_alpha + 0.2) difference() {
        zcyl(enc_xy[0], enc_xy[1], knob_d, knob_top_Z, knob_bot_Z);
        zcyl(enc_xy[0], enc_xy[1], knob_cb_d, knob_cb_top_Z, knob_bot_Z + eps);
        zcyl(enc_xy[0], enc_xy[1], enc_shaft_d, knob_bot_Z - knob_bore_depth, knob_bot_Z);
    }
}
module ghost_battery() {
    color([0.2, 0.35, 0.8], ghost_alpha) zbox(batt_x0, batt_x0 + batt_w, batt_y0, batt_y0 + batt_l, batt_Z0, batt_Z1);
}
module ghosts() { ghost_waveshare(); ghost_idc_ribbon(); ghost_control(); ghost_knob(); ghost_battery(); }

module sect() {
    if (section == "x") intersection() { children(); translate([section_pos - 1000, -1000, -1000]) cube(2000); }
    else if (section == "y") intersection() { children(); translate([-1000, section_pos - 1000, -1000]) cube(2000); }
    else children();
}
module assembly() {
    color([0.85, 0.55, 0.25], shell_alpha) sect() translate([0, 0, explode]) front_shell();
    color([0.45, 0.45, 0.5], shell_alpha) sect() translate([0, 0, -explode]) back_shell();
    color([0.95, 0.85, 0.2]) sect() translate([0, 0, explode]) {
        menu_cap_placed();
        side_cap_placed();
        key_pins_placed();
        key_plate();
        if (ctrl == "D") dpad_cap();
    }
    sect() ghosts();
}

// =====================================================================
// Console summary
// =====================================================================
echo(str("Rev B ctrl=", ctrl, ": depth front..back = ", back_out_Z, " mm (target <= 23); parting plane Z ", Z_split,
         "; control PCB Z ", cp_top_Z, "..", cp_bot_Z, "; cell Z ", batt_Z0, "..", batt_Z1, "; ribbon Z ", rib_Z0, "..", rib_Z1));
echo(str("Knob top ", knob_protrusion, " mm proud; shaft engagement ", knob_engage, "; nut+washer top Z ", nut_top_Z,
         " vs counterbore ceiling Z ", knob_cb_top_Z));
if (ctrl == "J")
    echo(str("THB001P: panel opening Dia ", thb_hole_d, " (fallback ", thb_hole_fallback, "); stick top ", -thb_top_Z, " mm proud"));
else
    echo(str("D-pad: actuation tilt ", dp_act_deg, " deg, flange gap ", dp_fl_Z0 - panel_t, ", side shift at the face ", dp_lat_front,
             " vs clearance ", dp_clr, "; cap length ", dp_cap_len, "; pressed arm tip ", dp_sink, " behind the face"));
if (side_flange_t < 0.6)
    echo(str("WARNING: SIDE flange only ", side_flange_t, " mm (tip X ", side_tip_x, "): print the cap in SLA, or move SW2 to tip X <= ",
             case_x1 - min_wall - side_flange_max - side_gap));

// =====================================================================
// Dispatcher
// =====================================================================
if (show == "assembly") assembly();
else if (show == "front") front_shell();
else if (show == "back") back_shell();
else if (show == "print_plate") print_plate();
else if (show == "menu_cap") menu_cap();
else if (show == "side_cap") side_cap();
else if (show == "key_cap") key_pin();
else if (show == "key_plate") key_plate_flat();
else if (show == "dpad_cap") { if (ctrl == "D") dpad_cap_flat(); else echo("dpad_cap: set ctrl = \"D\""); }
else assert(false, str("unknown show = ", show));
