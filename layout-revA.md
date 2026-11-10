# Layout Rev A — shared coordinates for KiCad carrier + OpenSCAD case

Decided 2026-11-08. Source data: ws241-mech/ws241-mechanical.md (Waveshare drawing), spec Rev A.5.
All values in mm. TBV = to be verified by measuring real parts (P3).

## Frames

**XY (front view)**: looking at the screen, device in portrait, X right, Y UP.
Origin = centre of the Waveshare's bottom-left mounting hole (after the rotation below).
KiCad carrier top view == this front view (carrier top copper faces the Waveshare's back),
so in KiCad use X right, and Y_kicad = -Y here.

**Z (depth)**: 0 = front face of the Waveshare cover glass, positive = towards the back of the device.

## Waveshare orientation: rotated 180° (DECISION)

The Waveshare's PWR / BOOT / RESET keys are side-actuated on one short edge of its PCB. In the
drawing's orientation that edge would face down into the knob area, inside the case, unreachable.
Rotating the board 180° puts those keys on the device's TOP edge (reachable through the top wall,
like a phone's buttons) and the USB-C on the LEFT side. Firmware rotates the image 180° to match.
Because the hole pattern and the J8 header are symmetric about the board centre, only J8's side
and pin numbering change:

| Item | Front-view XY |
|---|---|
| Waveshare holes (M2, ~Ø2.2) | (0,0) (36.50,0) (0,51.00) (36.50,51.00) |
| Lens outline (44.72 x 61.56, R1) | X -4.11 .. 40.61, Y -5.28 .. 56.28 |
| Viewing area 37.22 x 49.65 | centred in X; ~6.4 mm from lens TOP edge, ~5.5 mm from BOTTOM (scaled, TBV) |
| J8 / carrier socket J1 | now on the RIGHT: outer column X = 36.50 - 1.23 = 35.27, inner column X = 32.73; pin rows Y = 5.18 .. 45.82 (17 rows, 2.54 pitch). Pin 1 corner TBV from silkscreen — 180° rotation swaps which end it is |
| Waveshare keys (PWR/BOOT/RST, order TBV) | top edge, X = 9.75 / 18.25 / 26.75, at the PCB top edge (~Y 55.0, TBV), Z ≈ 4-6 (PCB level) |
| USB-C | left edge, Y ≈ 25.5, Z ≈ PCB level (~4.5-6, TBV) |

## Controls (front view)

| Item | Centre XY | Notes |
|---|---|---|
| MENU (B3F-4055, 12x12 mm, round cap) | (18.25, -15.0) | below the screen, centred |
| ENC1 left, X axis (PEC11R-4215F-S0024) | (3.25, -37.0) | knob Ø23.4 (Kilo OEJNI-90-4-7) |
| ENC2 right, Y axis | (33.25, -37.0) | 30.0 mm centres, 6.6 mm gap between knobs |
| SIDE (C&K PTS645VL31-2 LFS, right-angle) | right edge of carrier, Y = 20.0 | actuator points +X through the right wall via a printed cap |

## Outlines

| Item | Extent |
|---|---|
| Carrier PCB, 2-layer 1.6 mm | X -7.75 .. 44.25 (52.0 wide), Y -46.0 .. 54.0 (100.0 tall), R3 corners |
| Case outer | X -9.75 .. 46.25 (56.0 wide), Y -50.0 .. 60.3 (~110 tall), R4 vertical corners |
| Carrier mounting | the 4 Waveshare holes carry M2 x 11 mm standoffs (Waveshare to carrier); 2 extra M2 holes at (-4.5, -20.0) and (41.0, -20.0) screw the carrier to case bosses |

## Stack-up (Z), all parametric in the case

| Item | Z | Basis |
|---|---|---|
| Lens front face | 0.0 | datum |
| Waveshare PCB rear face | 6.0 | drawing |
| Tallest Waveshare rear part | 9.5 | drawing (6.0 + 3.5) |
| Male header plastic on Waveshare | 6.0 .. 8.5 | standard 2.5 mm, TBV |
| Carrier top face | 17.0 | 8.5 + 8.5 mm standard female socket, TBV |
| Carrier bottom face | 18.6 | 1.6 mm PCB |
| Encoder body top (front side, towards Z=0) | 17.0 - 6.5 = 10.5 | PEC11R body height ~6.5, TBV |
| Knob-deck panel (front wall in knob zone) | inner 10.5, outer 8.5 | 2.0 mm wall: the knob deck is STEPPED 8.5 mm behind the screen face |
| Battery 102050 (10 x 20 x 50) | 20.6 .. 30.6 behind the carrier, lying along X in the knob zone or along Y | parametric; lead runs to Waveshare J7 |
| Back wall | 30.6 .. 32.2 | ~32 mm total depth (≈27 mm with a 5 mm thin cell) |

Consequence: the device has a raised screen boss with a lower knob deck — the knobs sit 8.5 mm
behind the screen face, so their tops end up roughly 7 mm in front of it instead of 16 mm.
