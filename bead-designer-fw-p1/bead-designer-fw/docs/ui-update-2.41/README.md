# UI update for the Waveshare 2.41" (planned, not built yet)

This folder is a **temporary working record** of the planned UI update. Delete it
once the update is built and these notes are no longer needed.

- **Status (2026-11-21):** all 4 stages are built (native 450x600 renderer and the
  Soft-bead art; 9/12/15 grid and pattern save v7; System 7 menu chrome; motion, see
  below). This folder can go once the notes below are no longer needed.
- **[DECISIONS.md](DECISIONS.md):** the user's choices from the mockups, made one
  question at a time. **These override spec.md and motion.md wherever they differ.**
  In short:
  - bead look A "Soft bead" with rounded corners;
  - Chicago 3x with Scale3x smoothing;
  - keep the 2-px texture;
  - full-screen black menus with a System 7 striped title bar and close box;
  - a selectable 9/12/15 grid, always confirming size changes, default 9x9;
  - monochrome warnings;
  - the strip still dims to 50%;
  - a Motion On/Off setting, default On.
- **[spec.md](spec.md):** measurements (panel px), colours, firmware impact
  (native 450x600 renderer, pattern save v7, memory) and the question list.
- **[motion.md](motion.md):** 17 micro-animations, each with trigger, duration,
  easing, cost, how it degrades, and button-only behaviour.
- **mockups/:** native 450x600 renders:
  - `sheet.png` compares today with options A/B/C and shows all screens;
  - `draw_9/12/15.png` show the chosen option A at each grid size;
  - `menu.png` shows the floating-window menu, which was **not** chosen (menus stay full-screen);
  - `menu_grid.png` shows the grid-size picker;
  - `dialog.png` shows a confirmation dialog;
  - `crop_1to1.png` is a 1:1 crop of the text and strip, for judging sharpness.
- **scripts/:** the Python that drew the mockups. It uses the real Chicago font
  data from `main/font_chicago.c`.

## Planned build stages

1. Native 450x600 renderer and the Soft-bead art.
2. Variable grid (9/12/15) and pattern save format v7. Old saves migrate as 9x9.
   The QR export already carries the grid size.
3. Menus: crisper text, new row height, striped title bar and close box.
4. Motion, with the On/Off setting.

Each stage gets checked on the board and with `hw/run.sh run` and `sim/check.sh`.

## Stage 1 notes (built 2026-11-20)

- **Display:** `board.h` WS: `DISPLAY_W/H` 450x600 and `UI_HIRES 1`. `board_ws241.c`
  sends the framebuffer 1:1 and widens each window to even columns and rows. The
  saver keeps its 225x300 bitmap (`SAVER_W/H`), still expanded 2x2.
- **Art:** `main/render_hires.inc` ports the mockup maths: bead tones and class maps
  (bevel, sheen, rounded corners), contact shadows clipped to each tile, cursor ring
  with halo (cells are redrawn with a 12 px halo margin), lifted chips with shadows
  and a 2 px ring. `text.c` draws Chicago 3x with Scale3x. The name line has a 1 px
  outline and a (1, 2) 60 % drop shadow.
- **Menus:** native geometry only (56 px rows, 3x text, 10 px thumbnails); the
  System 7 title bar and close box are stage 3.
- **Freenove (240x320):** unchanged, pixel for pixel (all 40 `sim/check.sh` goldens
  pass). Board goldens (`hw/golden`) were regenerated at 450x600.
- **Speed (board, CONFIG_BEAD_PERF_LOG):** cursor move 9 ms; colour change 18 ms
  (was 86 ms before caching: bead tone tables, a 16x16 texture cache, per-slot strip
  backgrounds with their shadows in PSRAM, and the strip/name line composed in a
  39 KB internal buffer); full menu redraw 41 ms.
- **Memory:** 1.62 MB PSRAM for the framebuffer and two snapshots, 79 KB PSRAM band
  buffer, 148 KB PSRAM strip cache, 58 KB internal for the name line. Wi-Fi export
  still runs (`export_wifi` scenario).

## Stage 2 notes (built 2026-11-21)

- **Grid size per pattern:** `UI.n` (9, 12 or 15); cells are row-major with stride n
  in a 15x15 array (the rest `CELL_EMPTY`). `GRID_MIN`/`GRID_MAX`/`grid_size()` in
  `ui.h`. Gaps per size and board variant are in `board_style.c` (spec section 2;
  the 240x320 Freenove gets 22/16/12 px cells). The cursor starts in the middle.
- **Menu:** Pattern > Grid size (third row, the size on the right) lists the three
  sizes as double-height rows with a preview of *this* pattern at that size and
  "current" / "more room" / "cuts N beads" / "restore". Choosing another size
  always asks ("Change grid?"); the default is Resize, or Cancel when beads would
  be cut. Resizing closes the menu.
- **Resize rules (`main/grid.c`):** growing centres the old board
  (offset floor((new-old)/2)); shrinking keeps the window with the most beads, the
  centred one on ties. The cursor moves with the beads. The board from before the
  last resize can be restored exactly until the beads, brand or pattern change.
- **Save format v7:** `pattern_t` gains `n` and 15x15 cells (234 B per record).
  v6 and older records load as 9x9 (checked in the simulator: a v6 autosave and
  save slot written by the old firmware both load). Older firmware ignores v7.
- **Export:** QR URL and the Wi-Fi JSON carry n; the export page already accepts
  any n up to 64. Saved-slot thumbnails show 12x12 and 15x15 patterns in the same
  box (sub line "12x12, N beads").
- **Fix to stage 1:** cells read the bead class map outside the cell (in the
  half-gaps), so empty cells and beads smeared into the gaps. Board goldens were
  regenerated after the fix.
- **Tests:** new scenarios `grid_menu`, `grid_grow`, `grid_15`, `grid_cut`,
  `grid_restore`; `save_load` now steps to the 4th Pattern row for Reset.
- **Known:** on the 240 px Freenove the sub lines are shortened with "..."
  ("more ro..."); the 450 px screen fits them.

## Stage 3 notes (built 2026-11-21)

- **Waveshare only.** The 240x320 Freenove keeps the plain menus (all `sim/check.sh`
  goldens unchanged); everything below is under `UI_HIRES` in `render.c`.
- **Title bar** (`draw_title`): full-width black screen, as decided. 2 on / 2 off
  stripes in #585858 (x 8..442, rows 15..40), a 26 px close box at x 18 on a 3 px
  black margin, the title centred in a 12 px black gap, the battery icon at the
  right on a black margin, and a 1 px #808080 rule along the bottom of the 56 px band. A title
  too wide to centre is pushed sideways; if it still does not fit, the battery icon
  is dropped ("Scan with phone"), and only then is it shortened with "...".
  The close box is drawn only; touch (and its 56x52 hit area) comes later.
- **Rows:** `menu_row_t.kind` (`ROW_PLAIN` / `ROW_SUB` / `ROW_DIALOG`, set in
  `menu.c`). Submenu rows end in an 8x15 #AAAAAA triangle (the value moves 24 px
  left); dialog rows get "..." appended at draw time (Save..., Load..., Reset
  board...). Sub: Pattern, Brand, Edit shortlist, Colours used, Export, the
  shortlist slots, Browse colours and Enter code, Grid size.
- **Selection:** 1 px white frame with r 6 corners (`row_ring`), across both bands
  of double-height rows.
- **Not done (still open from spec section 5):** the System 7 alert dialog (double
  frame, caution icon, buttons with a default ring). Confirmations stay full-screen
  lists, in line with the "menus stay full black screens" decision. The texture
  row's live swatch (a COULD) was also skipped.
- **Tests:** new scenario `pattern_menu` (shows Save... / Grid size with its triangle).
  Board goldens for every menu screen were regenerated.

## Stage 4 notes (built 2026-11-21)

- **Waveshare only**, like stage 3: the Freenove has no animations and no Motion row
  (all `sim/check.sh` goldens unchanged).
- **Setting:** Menu > Motion (9th row, before Export), On / Off, default On. Stored as
  bit 1 of the `look` flags (set = Off), so older look records read as On and no format
  change was needed. Export moved to the 10th row (the menu scrolls to it); the
  `export_qr` / `export_wifi` scripts now press Up once (wraps to Export) instead of
  Down 8 times.
- **How:** `main/render_motion.inc`. Each animation sets overrides that the normal
  drawing code reads (cursor ring position and colour, a growing/shrinking bead, chip
  lifts, strip brightness, the menu frame's y range) and redraws its area once per
  frame (16 ms, time based). The last frame clears the overrides, so every animation
  ends on exactly the static screen: all board goldens still match with Motion on.
  Any input calls `render_anim_finish()` first (jump to the end), then is handled.
  Frames whose overrides did not change are skipped; two frames over 40 ms in a row cut
  to the end.
- **What was built from motion.md** (adapted to the decisions): cursor glide (snap on
  autorepeat; a wrap snaps with a 1-frame dim ring), bead drop (70 -> 106 -> 100 %, not
  for paint-brush strokes) and pop, chip select, focus fade (strip, name line, and the
  ring the opposite way), eyedropper (ring blinks off, strip bright while the picked chip
  rises, then fades back), highlight slide, commit blink (2 x 60 ms, blocking, skipped
  for presses within 400 ms), saver fade out / in and wake fade-in (`ui.c`; the saver
  keeps today's brightness, not motion.md's <= 60 %).
- **Menus stay full black screens**, so motion.md #8/#9/#12/#13 became System 7 zoom
  rects without a dimmed board: XOR outlines (3 visible at a time) from the MENU key
  (below the screen, centre) out to the screen edge, then the menu (67 ms); from the
  chosen row into a deeper screen (submenu, picker, confirmation; 50 ms); and on close
  the board appears at once while the rects shrink back to the MENU key (67 ms). Going
  back up a level is instant.
- **Not built:** #14's "Saved to 1" flash after Save (a new feature, not motion); #17
  marquee at 1 px steps (unchanged: 2 px per 40 ms).
- **Panel driver:** `present()` now copies this frame's and the previous frame's dirty
  rectangles into the snapshot separately instead of their bounding box. A cursor near
  the top plus the strip at the bottom made a ~370 KB PSRAM copy (~9 ms) per frame.
- **Speed (board):** glide, drop, pop, zooms and the highlight run at 16 ms a frame;
  frames that redraw the strip and name line take ~26 ms (strip 7.4 ms, name line
  6.9 ms, present 4.9 ms).
- **Checking frames:** `MOTION_SLOW` (top of `render_motion.inc`) plays everything N
  times slower so `F` dumps catch in-between frames; it also logs each start/end.
- **Tests:** new board scenarios `motion_menu` and `motion_off` (`hw/scripts/`). Board
  goldens `menu` and `batt_menu` were regenerated (Motion row instead of Export).


## Changes after stage 4 (user, 2026-11-21)

- **Close box removed** from the menu title bar (the stripes now run from x 8 to the
  battery icon). The decision for a striped title bar stands; only the box went.
- **Solid board: no centre peg** on empty cells (both boards). Classic keeps its peg;
  Pegs / Big pegs are unchanged. Goldens for every menu screen (board) and every
  Solid-board screen (board and simulator) were regenerated.

## Motion rework (user, 2026-11-21): smooth 60 fps, no artifacts

- **Removed:** the System 7 zoom rects (menu open/close, into submenus), the commit
  blink before a menu action, and the one-frame cursor ring flickers (eyedropper blink,
  wrap flash). Screen changes are now instant. The user's bar: completely smooth at
  60+ fps with no visual artifacts, a clean simple retro UI.
- **Kept:** cursor glide, bead drop/pop, chip lift, focus fade, eyedropper (strip bright
  while the chip rises), menu highlight slide, saver fades.
- **Measured on the board (real speed):** every in-between frame now comes 16 ms after
  the last (60 fps) for glide, drop, pop, chip lift, fades, eyedropper and the menu
  slide. The only longer gap is the first one after an encoder turn (~20 ms): the
  input's own frame (strip + name line) takes that long to draw.
- **How 60 fps was reached:** the cursor ring takes its new brightness at once instead of
  fading with the strip; after a glide the strip fade starts when the glide ends; a
  cursor-free copy of the board (`s_clean`, 405 KB PSRAM) lets glide frames redraw only
  the ring and halo (11 -> 4 ms); the name line is recoloured from its cached glyph
  marks during a fade (7 -> 2.7 ms); the strip skips texture under the chip slots; the
  cursor cell is redrawn only when it changed; the cursor area and the strip are
  presented separately; and the panel driver skips copying the previous frame's
  rectangle when the current one contains it.
- **Menu remembers its row:** MENU reopens the main menu on the row last highlighted
  (also the row a submenu was opened from). Not kept across a restart. New board
  scenario `menu_remember`; `brand_hama` now presses Up twice to reach Export.

