# BEAD PATTERN DESIGNER: BUILD PLAN AND DELIVERABLE SPECIFICATIONS

| Field | Value |
|---|---|
| Document | Build plan, deliverable specifications and timeline |
| Companion | bead-designer-prototype-spec.md (Rev A.5) — parts, pin map, power, control mapping |
| Revision | Plan Rev 1.1 — APPROVED 2026-11-08 with section 8 decisions |
| Date | 2026-11-08 |
| Status | In progress: P0 + agents A/B running |

---

## 1. Current state (sequential review)

Everything was ordered from exactly two sellers: Amazon.ca and DigiKey Canada.

| # | Arrives | Item | Role | Open checks |
|---|---|---|---|---|
| 1 | Mon 2026-11-09 | Freenove ESP32 CYD 2.4" 240x320 IPS, resistive touch ($26.87) | Interim firmware board only, not part of the product | Pinout and display driver IC on arrival |
| 2 | Tue 2026-11-10 | HXJN LiPo 3.7 V 1100 mAh 102050 ($24.64) | Product battery | Connector type, protection PCM under tape, polarity vs J7 pin 1 = BAT+ |
| 3 | ~Nov 10-13 | DigiKey order, $134.65, free shipping | Encoders x5, knobs x5, B3F switches, PTS645 right-angle switches (10x VL31 + 1x VK31), 10k resistors x25, 10 nF caps x10, Adafruit 4922 battery cables x2, M-M and M-F jumpers | None — all lines were in stock |
| 4 | ~Nov 10-13 (Prime) | Breadboard kit: 830 + 400 points, 140 U-jumpers, 65 flex jumpers ($19.99) | POC platform | None |
| 5 | Nov 20-26 | Waveshare ESP32-S3-Touch-AMOLED-2.41, 600x450 ($86.87) | Product brain + display for prototype Rev A | **Must be V2 / "Rev2.0" silkscreen.** If V1, pin map is redone before any wiring |

Carried-over technical state (all in spec Rev A.5, unchanged):
- Pin map (V2): ENC_L = GPIO1/7/8, ENC_R = GPIO38/39/40, MENU = GPIO41, SIDE = GPIO42, all on header J8.
- Encoder filter: 10k pull-up + 10k series + 10 nF per A/B line; switches get 10k pull-up + optional 10 nF.
- Power: J7 pin 1 = BAT+, ETA6098 charger fixed at 0.5 A, GPIO16 power latch must be driven high on battery, BAT_ADC on GPIO17 (divide-by-3).
- Controls: Draw / Palette / Settings / Export mode map, cursor wraps, palette = pixelated colour grid.

## 2. Phase plan overview

| Phase | What | Blocked by |
|---|---|---|
| P0 | Firmware skeleton + repo (can start today) | Plan approval |
| P1 | Freenove bring-up: display, UI core, one encoder | Freenove + DigiKey arrivals |
| P2 | Full breadboard POC on Waveshare per wiring diagram (**Deliverable 1**) | Waveshare arrival, revision check |
| P3 | Measurements: board outline, stack-up, shaft, battery | P2 hardware in hand |
| P4 | Carrier PCB in KiCad (**Deliverable 2**) | P3 measurements (draft can start earlier) |
| P5 | Case in OpenSCAD/FreeCAD (**Deliverable 3**) | P3 + P4 outline |
| P6 | JLCPCB order (PCB + 3D print), assembly, firmware polish | P4 + P5 |

## 3. Deliverable 1 — Breadboard POC package

What you receive (as inline diagrams + a markdown build sheet):

1. **Freenove day-1 guide** (ready the day it arrives): confirmed pinout (an agent will pull the Freenove tutorial for the exact board), which 2-3 free GPIOs to use for one encoder + one button, display driver identification, and the ESP-IDF project configured for it.
2. **Breadboard layout picture, Freenove stage**: 830-pt board, power rails from 3V3/GND, one PEC11R with its two RC filter blocks, one B3F button. Simple colour-coded diagram like the wiring diagrams already delivered.
3. **Breadboard layout picture, Waveshare stage**: full 8-signal layout — both encoders (4 filter blocks), both switches, pull-ups, J8 header-to-rail jumpers (M-F), row/column positions called out so you can place parts hole by hole.
4. **Battery re-termination sheet**: 4922 cut-and-splice steps, polarity check against J7 silkscreen, PCM inspection, DW01A fallback.
5. **Bring-up checklist**: power latch test (GPIO16), PCNT encoder count test, button debounce test, display init, BAT_ADC reading.

Firmware plan (plain C on ESP-IDF, as agreed):

```
main/
  app_main.c      boot, power latch (GPIO16 high), task init
  board/          ONLY hardware-specific code
    board_freenove.c   SPI ILI9341/ST7789 init + blit  (P1)
    board_ws241.c      QSPI RM690B0 init + blit        (P2)
    board.h            pins, display ops struct
  input.c         PCNT quadrature x2, button ISRs -> event queue
  ui.c            mode state machine (Draw/Palette/Settings/Export)
  render.c        cell/cursor/palette/menu drawing via board ops
  palettes.c      Perler / Hama / Artkal const tables
  store.c         NVS save slots
  export.c        SoftAP + HTTP server + grid JSON + QR (qrcodegen)
```

Everything above `board/` is identical on both boards — that is the point of P1: by the time the Waveshare arrives, only `board_ws241.c` is new work.

## 4. Deliverable 2 — KiCad carrier PCB package

Scope (decision 8.1 affects this): carrier board that the Waveshare board plugs into, carrying encoders, switches, filters and mounting.

Contents, authored as KiCad s-expression text files and sent for your local instance:

| File | Content |
|---|---|
| carrier.kicad_pro | Project, board stackup (2-layer, 1.6 mm) |
| carrier.kicad_sch | J8 2x17 socket, 2x PEC11R-S, PTS645VL31 (SIDE), B3F-4055 or PTS-based MENU, 12x R, 8x C, mounting holes, test points |
| carrier.kicad_pcb | Outline from P3 measurements, placement per ergonomics, ground pour |
| sym/footprint libs | Only where KiCad official libs lack a part (PEC11R and B3F exist in official libs; PTS645 vertical footprint will be drawn and included) |
| jlcpcb/ | Gerbers, drill, BOM.csv, CPL.csv (assembly optional — all parts are THT, hand-solder is fine) |
| carrier.step | For the case CAD |

Honest constraints:
- This workspace has no kicad-cli, so **DRC/ERC runs on your local KiCad** — the files are deliverable, the final green check is yours. I will state the KiCad version targeted (9.x unless you say 8.x) and keep to official library footprints so nothing is missing on import.
- The board outline and the J8 socket position are locked by P3 measurements of the real Waveshare board (its published outline drawing was never retrievable — an agent will retry; if found, P4 can start before P3).

## 5. Deliverable 3 — Case package

- **Format**: parametric **OpenSCAD source** (.scad) — text I can fully author here and you can open, tweak and export (STL/STEP via FreeCAD) locally. No binary CAD tool exists in this workspace, so .scad source is the verifiable deliverable; FreeCAD imports the exported STL/STEP for inspection.
- **Parts**: front shell (screen window, 2x M7 bushing holes, MENU hole/cap, side-button cap in wall), back shell (battery bay for 102050 cell, USB-C cutout, screw bosses for M2 heat-set inserts), 2 printable knob-cap options (optional, you bought aluminium knobs).
- **Every critical dimension is a named parameter** (wall thickness, board outline, stack heights, shaft protrusion) so P3 measurements drop in without remodelling.
- **JLCPCB print**: recommended MJF nylon (PA12) for the shells; SLA resin for a cheap first fit-check. Export STL from the .scad, upload, pick material.
- Case waits for P3/P4 by necessity; a draft with TBV parameters can be authored earlier for you to inspect the construction.

## 6. Agent plan

| Agent | Task | When |
|---|---|---|
| A | Freenove CYD exact pinout + display driver from Freenove docs/tutorial | On approval (before board arrives) |
| B | Waveshare 2.41 outline/mounting dimensions (retry dimension drawing, GitHub repo, community measurements) | On approval |
| C | KiCad files authoring + self-review pass | P4 |
| D | OpenSCAD case authoring | P5 |
| E | Independent review of C/D outputs (fresh agent, checks nets against spec pin map, checks .scad parameter math) | After C/D |

All state lives in the two markdown specs + delivered files, so any later model/agent picks up from the documents alone if credits run out mid-project.

## 7. Realistic timeline

| Dates (2026-27) | Milestone |
|---|---|
| Nov 8 | This plan reviewed, P0 firmware skeleton scaffolded |
| Nov 9-13 | P1: Freenove bring-up, UI core, 1 encoder + button on breadboard |
| Nov 14-19 | P1 cont.: palettes, save slots, SoftAP export working on Freenove |
| Nov 20-26 | Waveshare arrives → revision check → P2 full POC (**Deliverable 1 done**) |
| Nov 24-30 | P3 measurements; P4 schematic (draft earlier if agent B finds the outline) |
| Dec 1-7 | P4 layout + your local DRC round (**Deliverable 2 done**) |
| Dec 3-10 | P5 case .scad + your FreeCAD inspection (**Deliverable 3 done**) |
| Dec 8-12 | JLCPCB order: PCB (~CA$10-30 + ship) + MJF shells (~CA$30-60) |
| Dec 20 - Jan 5 | Parts land (December shipping is slow); assembly rev 1 |
| Jan 2027 | Debug, case rev 2 if needed → working custom handheld |

Two realistic buffers built in: first PCBs almost always need one revision (budget for a second ~$40 order in January), and the case typically takes 2 prints to get button feel and screen fit right.

## 8. Decisions (RESOLVED 2026-11-08)

1. **Custom board v1 display**: socketed carrier — the Waveshare board plugs into a 2x17 socket on the carrier. Bare-panel full-custom board deferred to v2.
2. **KiCad version**: buyer runs **KiCad 10.0.7**. Files will be authored to open cleanly in 10.x (KiCad 10 also reads 9.x-format files and upgrades on save); exact format version tokens to be verified in P4 against the 10.0.7 file format.
3. **Case source format**: OpenSCAD parametric .scad source; buyer exports STL/STEP and inspects in FreeCAD.
4. **Assembly**: hand-soldered by buyer (all THT). JLCPCB supplies bare PCBs + 3D-printed shells only.
