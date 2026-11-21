# Bead Designer UI update for the Waveshare 2.41" (450x600, 311 ppi): mockup spec

Mockups only. Nothing in the repo was changed. All values are **panel px** (1 px = 0.082 mm).
Renderer: `scripts/gfx.py`, `scripts/screens.py`, `build_draw.py`, `build_menus.py`, `build_sheet.py`. Chicago glyphs are read from `main/font_chicago.c`.
Every visual item is an option to discuss. The rejected redesign is not repeated: no grid lines, no round pegs, and not the #3A/#8C/#A8 scheme.

## 0. Today (baseline)
- `board.h` (working tree): `DISPLAY_W 225`, `DISPLAY_H 300`. Each logical px is drawn as 2x2 panel px.
- `render.c:48-55`: Solid cell = (225-40)/9 = 20 logical = **40 panel px**, gap 8.
- `render.c:35-36`: chip 24 log = 48 panel px, pitch 72.
- `text.h:6`: `TEXT_SCALE 2`, so Chicago is 4x on the panel (52 px line).
- Outlines and rings are 2 panel px. The texture "pixel" is 2 panel px.

## 1. Recommendation
**Option A "Soft bead"** (`draw_9/12/15.png`): a soft 2 px bevel, a 3-step corner sheen, a 2-3 px corner radius on beads, and a contact shadow kept inside the tile.
- **B "Flat + lift"** (`draw_12_optB.png`) is the fallback if A feels too glossy. It has flat square beads. Depth appears only in the chrome (lifted chip, panel shadows), and it costs the least.
- **C "Tray"** (`draw_12_optC.png`) has recessed empty slots and a stronger 3 px bevel. It is the most skeuomorphic, and its slot shading sits nearest to the rejected darker-cell look, so I would not lead with it.

## 2. Grid geometry (Solid variant, which is the default)
The grid is a square in the top 450x450 area. `ox = oy = (450-used)/2 + gap`, the same formula as `layout()`.

| N | cell | gap | pitch | used | peg (square, #787878) | bead corner r | cast shadow (dx,dy,blur) | cell mm / pitch mm |
|---|---|---|---|---|---|---|---|---|
| 9  | 43 | 6 | 49 | 447 | 8x8 | 3 | 2,2,1 | 3.5 / 4.0 |
| 12 | 32 | 5 | 37 | 449 | 6x6 | 2 | 2,2,1 | 2.6 / 3.0 |
| 15 | 25 | 4 | 29 | 439 | 5x5 | 2 | 1,2,0 | 2.0 / 2.4 |

**Other variants**
- Pegs uses the same numbers with see-through cells.
- Classic and Big pegs use a narrower gap:
  - 9: gap 3, cell 46
  - 12: gap 3, cell 34
  - 15: gap 2, cell 27

**Cells**
- Empty cell: checker #505050/#585858 at **1 px** (finer dithering) with a square peg. The agreed tones are unchanged.
- Bead bevel (A), measured from the edge:
  - light top/left: +20%, +11% toward white
  - dark bottom/right: -20%, -11% toward black
  - Bottom/right wins at the corners, as `bevel_px` does today.
- Sheen (A/C): three stepped bands at (x+y) < 0.62·cell, lightened +16% / +9% / +4%. The steps are visible on purpose, for a retro look, and the bands can be cached per size.
- Contact shadow: black at 85% before blur. It is **clipped to the bead's own tile** (cell plus its half-gaps), so the existing one-tile-per-cell redraw still works (`draw_cell`, `render.c:96`).

**Cursor** (agreed layering, made thicker at native)
- 1 px black at offset 1 outside the cell, then a **2 px white ring** at offsets 2-3. The whole ring fits inside the 4-6 px gap, so it never covers a bead.
- A 1 px white ring alone measures 0.08 mm and is too faint. This is the one place where I break "1 px outlines".
- A/C add a soft dark halo outside the ring (blur 2, 60%) so the ring reads on the texture.
- The ring is dimmed to 50% while the strip has focus. This is unchanged.

**Texture**
- Table A, tones #303030/#606060, is unchanged.
- **The texture pixel stays 2 panel px.** At 1 px the checker averages to flat #484848 at 311 ppi (see `crop_1to1.png`).
- "Finer dithering" is used inside cells, shadows and chrome instead.

## 3. Strip and name line
- **Chips:** 5 slots, chip 60x60 (4.9 mm) with corner r 5 and a 1 px black edge (agreed).
  - Pitch is 84 (6.9 mm). The touch hit box is the full 84 x 96 column.
  - x0 = (450-5·84)/2 + 12 = 27, centre y 497.
- **Selected chip:**
  - lifted 4 px, with shadow (0,+6, blur 4, 75%)
  - ring = 1 px black, then 2 px white at offsets 2-3, the same as the cursor
- **Other chips:** shadow (0,+2, blur 2, 55%).
- **Focus dimming:** the chips and the name line are at 50% while the board has focus. This is unchanged (`render.c:61`).
- **Name line:**
  - Chicago **3x with Scale3x smoothing**: 39 px line, 27 px caps (2.2 mm), top y 549, x 16.
  - The count is right-aligned at 434, colour #AAAAAA.
  - Outline: 1 px black, plus a drop shadow (1,2) at 60%.

## 4. Text
| Use | Size | Line px | mm |
|---|---|---|---|
| Everything (name line, menu rows, titles, dialog) | Chicago x3, Scale3x | 39 | 3.2 |
| Today, for reference | Chicago x4 plain | 52 | 4.3 |
| Not used (too small for the target users) | x2 | 26 | 2.1 |

**Scale3x** (AdvMAME3x) adds 1-px corner rounding and diagonal fill without changing a glyph's shape. It costs nothing at runtime if the 95 glyphs are expanded once at boot: 95 x 39 rows x up to 36 bits, about 17 KB.

## 5. Panels (menus, dialogs)
**Behind the panel**
- The drawing screen is dimmed to **38%**, keeping its texture, so the menu reads as a window on top of the work.
- The panel interior stays plain black (agreed).

**Panel**
- Corner r 6, 1 px white border.
- Shadow (0,+8), box blur r6 x2 passes, black 85%. On black it is only visible over the dimmed board.
- Main menu: x 12, y 12, w 426, h 567.

**Title bar**
- 52 px tall, title centred (System 7).
- "Racing stripes" 2 on / 2 off in #585858, with an 18 px black gap around the title.
- 1 px #808080 rule under the bar.
- **Close box:** 26x26, 1 px white, at x+18. It is the touch target for back/close (hit area 56x52). The button path is D-pad centre or MENU, as today.

**Rows**
- 56 px tall (4.6 mm). This is the compromise that fits 9 rows. A "Look" submenu (Board/Texture/Beads) would allow 68 px rows, which is better for touch.
- Text x+22. Right-hand values in #AAAAAA, right-aligned at x+w-22.
- Submenu rows end in a System 7 triangle (8x15, #AAAAAA).
- Rows that open a dialog end in "..." (Mac rule: Save..., Load..., Reset board...).
- Texture row: shows a 26x26 live swatch next to "1/8" (COULD).
- **Selection:** the agreed 1 px white frame, now with r 6, inset 6 px from the panel.

**Dialog** (`dialog.png`)
- 404x316 with a System 7 alert double frame: 1 px white outer, then a 2 px #808080 inner frame inset 4.
- Shadow (0,+10, blur 8, 90%). The parent panel is dimmed to 40%.
- Contents: caution icon (54 px triangle with "!"), a 3x title, the consequence ("23 beads lost"), and old → new thumbnails at 6 px per bead.
- **Buttons:** 160x64 (5.2 mm tall), r 11, 1 px white.
  - The **default** button has a System 7 3 px ring at 6 px offset, r 16, and that ring is the selection.
  - The default is **Cancel** for destructive actions.
  - Turn or L/R moves the ring; press activates.

## 6. Grid-size control (C)
**Where:** Pattern > **Grid size ▸ 12x12** (row 3). The grid size belongs to the pattern, so it does not go in the device-wide look settings. It opens a cascaded panel, `menu_grid.png`: x 30, y 196, w 408, rows 104 px.

**Each row shows:**
- a thumbnail of *this* pattern at that size (9/7/6 px per bead)
- "N x N"
- a consequence line: "current", "more room", or "cuts 6 beads" in amber #FFC840
- a check mark on the current size

**Behaviour (recommended):**
1. **Growing** (9→12→15) is always lossless. Content is placed centred at offset floor((new-old)/2), so 9→12 gives +1/+2, 12→15 gives +1/+2, and 9→15 gives +3/+3. The resize applies immediately, closes the menu, and sets `unsaved`.
2. **Shrinking:** choose the new-size window that keeps the most beads, preferring the centred one on ties.
   - If no bead is lost, apply it with no prompt.
   - If beads are lost, show the dialog "Cut 6 beads?" with Cancel as the default.
3. **Lossless way back:** keep the pre-resize cells in RAM (225 B) until the next bead edit. Choosing the old size again restores them exactly. This is a cheap, targeted "undo" for the most destructive new action.
4. The cursor is clamped and moves with the offset, so it stays on the same bead.
5. New, Reset and empty boards keep their current size. On first boot or after migration the size is 9 (today's look).
6. Brand lock is unaffected: resizing does not change colours.

**Button path:** turn or Up/Down moves the frame; press selects. Centre or the close box goes back to Pattern.

## 7. Firmware impact
**Renderer: native 450x600 instead of pixel doubling**
- `board.h`: `DISPLAY_W/H` → 450/600. `board_ws241.c` present(): drop the 2x expansion and copy rows directly. The 2x path stays for any board that needs it.
- Every constant in `render.c` must come from a per-board UI scale table:
  - `CHIP`, `CHIP_PITCH`, `TEXT_PAD`, `ROW_H`, `THUMB_PX`, `TILE_MAX`
  - menu `VISIBLE_ROWS`, which must come from the same source as `MENU_VISIBLE`
- `text.c`: add the Scale3x glyph cache and `TEXT_SCALE` per board (Freenove 2 plain, WS 3 smoothed).
- `layout()` takes N from the pattern rather than the `GRID_N` constant. It needs a gap/cell/peg/r table per size and per board variant (section 2).
- **Buffers:**
  - Internal static `s_strip` (DISPLAY_W x 32) and `s_line` would be 29-43 KB each in internal RAM at native size. **Compose straight into the PSRAM shadow framebuffer**, which the WS driver already has, and keep only the DMA bands internal.
  - `s_tile` grows to 49x49x2 = 4.8 KB.
- **Memory (PSRAM):** shadow fb plus 2 snapshot buffers = 3 x 540 KB = **1.62 MB** (today 3 x 135 KB). That fits the 8 MB PSRAM.
- **Optional caches:**
  - dimmed-board copy for menus: 540 KB
  - bead shading LUT per size: < 4 KB
  - glyphs: 17 KB
- **Bandwidth:** a full frame is ~13.5 ms at 80 MHz QSPI. `saver.c:48` says ~16 ms was measured. Full-screen work at 60 fps is therefore borderline, so animations must use dirty rectangles (see `motion.md`).
- **Saver:** `PITCH`/`SQUARE_DOTS` were tuned for 225x300 (`saver.c:41-47`). The mono bitmap grows from 8.4 KB to 33.75 KB internal. Alternatively keep the saver at 2x logical, which is a valid choice since it is full-screen motion anyway. **Flag for the firmware engineer.**

**Save format (pattern v7)**
- `pattern_t` gains `uint8_t n`, and cells becomes `cells[15*15]` (225 B).
- The record is ~234 B, up from ~90. NVS: 9 records ≈ 2.1 KB.
- RAM: `UI.cells[225]`, plus the slot cache 8 x 234 B and the resize backup 225 B.
- Migration: v6 → v7 sets n = 9, copies 81 cells, and fills the rest with `CELL_EMPTY`.
- `valid()` checks n ∈ {9,12,15}.
- Every `GRID_N*GRID_N` loop must use n·n: `store.c`, `menu.c` (count_used, total_beads, reset_board, set_brand), `render.c` (count_color, thumbnail), `export.c`.
- Thumbnails use a fixed ~90 px box at 10/7/6 px per bead.

**QR export**
- The URL already carries N (`export.c:118`, `#1.<brand>.<N>.`), so the format needs no change.
- At 15x15 the payload is about 225 cell chars plus the colour list, roughly 300-500 chars. That is about QR version 13-16 at ECC Low, well within `QR_MAX_VERSION 20`, and gives 4-5 px per module on 450 px.
- **Flag:** confirm that the export page (TeddyGavi/pixel-painter-export) renders N ≠ 9.

**Freenove (240x320)**
- 15x15 gives a 13 px cell (2.0 mm at 167 ppi), physically the same as the WS 15x15.
- It stays at TEXT_SCALE 2 with the old chrome. The grid table needs a 240-wide row.

## 8. Hardware facts to confirm (do not guess)
- RM690B0 brightness register (0x51) ramp: is it smooth enough for 150-500 ms fades? It is used for the saver fade so no pixels are redrawn.
- There is no TE line (`render.c:75`). Animations can tear mid-frame. Is double-buffered present() enough, or is a TE pin available?
- Touch is an FT6336U (`board_ws241.c:27`). Its touch-reset is held high and the controller is unused. It plays no part in these mockups beyond hit sizes.

## 9. Questions for the user (ask one at a time, in this order)
1. Which bead look do you prefer on the device: A (soft bevel, sheen and small shadow), B (flat squares, depth only in menus and chips) or C (recessed slots)?
2. Is Chicago at 3x (about 75% of today's text height, with smoothed corners) big enough, or should text stay at today's size?
3. Should the background texture keep today's 2-pixel dots, or try the finer 1-pixel version?
4. Should beads get slightly rounded corners (2-3 px), or stay perfectly square?
5. Is it OK for menus to become a window floating over the dimmed board, instead of a full black screen?
6. Keep or drop the Mac title-bar stripes and the close box?
7. When shrinking the grid would cut off beads, is "ask first (Cancel is the default) and allow going back to the old size until your next edit" the right behaviour?
8. What size should a brand-new device start with: 9x9 like today, or 12x12?
9. May warnings use one accent colour (amber), or should everything stay black, white and grey?
10. While you draw, the colour strip dims to half. Keep that, or dim it less (to about 70%)?
11. Should there be a "Motion: On/Off" setting, or are animations always on?
