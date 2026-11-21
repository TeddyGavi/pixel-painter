# Test print in PLA (Bambu Lab, FDM)

A quick fit test of case rev B1 (variant D) on a Bambu Lab printer, before ordering the final MJF PA12 / SLA parts from JLC (see [README.md](README.md) section 3). FDM is coarser than MJF, so this print checks **fit and layout**, not final finish.

## Files

From [`stl-D-2026-11-19/`](stl-D-2026-11-19/):

| File | Print it? | Notes |
|---|---|---|
| `bead_case_D_front.stl` | **Yes** | Main test part |
| `bead_case_D_back.stl` | **Yes** | Main test part |
| `bead_case_D_key_plate.stl` | Yes | Slides into the top-wall slot |
| `bead_case_D_dpad_cap.stl` | Yes | Already laid out top-face down |
| `bead_case_D_menu_cap.stl` | Yes | |
| `bead_case_D_side_cap.stl` | Optional | 0.8 flange: marginal with a 0.4 nozzle |
| `bead_case_D_key_cap.stl` (key pins, Ø1.6) | Skip, or 0.2 nozzle | Too fine for a 0.4 nozzle |

## Slicer settings (Bambu Studio)

| Setting | Value | Why |
|---|---|---|
| Material | **PLA** (PETG also fine; it holds inserts slightly better) | |
| Nozzle | 0.4 (0.2 for the small caps / key pins if available) | |
| Layer height | **0.12–0.16 mm** | 2.0 front panel and 0.2 boss feet come out close to true |
| Walls | 3 | |
| Infill | 20–30 % | |
| Supports | **On, tree, touching the build plate only** | USB duct, key-plate slot, boss bridges |
| Orientation | **Outside face down on the plate**: use *Lay on face* (F) and click the front face (front shell) or the back face (back shell) | The STLs are exported in assembly position, so they load upside-down or floating |
| Brim | Optional, on the shells if corners lift | |

## Heat-set inserts (M2, 3.2 x 4)

- **Iron temperature:** about **200 °C for PLA** (230 °C for PETG). The 220–240 °C in the README is for PA12 nylon; on PLA it melts the boss.
- **Pressing:** press straight and slowly, and stop flush.
- **Hole fit:** the holes are sized for the 3.2 x 4 inserts. FDM holes print about 0.1–0.2 small, which helps the inserts grip.

## What this print checks

- [ ] Glass sits **flush** with the front face, and the Waveshare goes in with the ~12° tilt (USB edge first)
- [ ] The 3 × M2 x 4 screws reach the Waveshare standoffs; the glass doesn't rock
- [ ] The boss bodies clear the RTC and battery connectors and the CHG LED (look from the side with a light behind)
- [ ] Control PCB, encoder bushing, D-pad and MENU line up with their openings
- [ ] Adapter on J8 and the FFC Z-fold fit ([`diagrams/2-ffc-fold-zoom.png`](diagrams/2-ffc-fold-zoom.png))
- [ ] Back shell closes without forcing the adapter; the ribs just touch it
- [ ] Overall depth (21.5 mm) and feel in the hand

## What it can't check well

- **Fine parts:** the key pins (Ø1.6) and the SIDE cap flange (0.8) are below what a 0.4 nozzle prints reliably.
- **Exact fits:** FDM holes print small and outer edges slightly large. If the glass pocket or a hole is tight, suspect the printer first: measure, then report back before the model is changed.
- **Surface and wall strength:** the 1.0 walls in FDM are weaker than in MJF.

Record what fits and what doesn't (with caliper numbers where possible), then update `bead_case.scad`, rerun `validate_case.py`, and re-export the STLs before the JLC order.
