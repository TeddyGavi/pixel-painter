# Motion spec (60 fps = 16.7 ms per frame)

**Rules for every animation**
- **Time-based:** each frame computes state from elapsed time, so late frames are skipped rather than slowed.
- **Input always wins:** any button event during an animation jumps to its final state on the same frame, then handles the new event. A button never waits for an animation, and fast D-pad repeat must feel exactly like today.
- **Overrun policy:** if compose plus present exceeds 16.7 ms twice in a row, cut to the final frame. The "skip" column below says what to drop first.
- **Easing names:**
  - out-cubic = 1-(1-t)^3
  - out-back = small overshoot, s = 1.2
  - linear
- **Cost:** given in bytes pushed per frame. The full screen is 540 KB, ~13.5-16 ms. Tiles are 2-5 KB each, ~0.1 ms.
- **Buttons only:** every animation below is driven by buttons. Touch adds no animation of its own. With "Motion: Off" (if wanted), every one of them becomes its final frame.

| # | Animation | Trigger | Duration / frames | Easing | What moves | Dirty area / cost | Skip if late |
|---|---|---|---|---|---|---|---|
| 1 | **Cursor glide** | D-pad move | 83 ms / 5 | out-cubic | The ring (and halo) slides from the old cell to the new one. Bead tiles are not redrawn except where the ring passes. | Union of 2 tiles: 2x(49+6)² ≈ 12 KB at 9x9 | **Snap** if another move arrives within 83 ms (held key / autorepeat). Never glide across a wrap. Instead, snap and flash the ring once (1 frame at 50%) so the wrap is visible. |
| 2 | **Bead drop** | Encoder press = place | 100 ms / 6 | out-back | The bead grows from 70% to 106% and settles at 100% of the cell, centred. The shadow fades in over the last 3 frames. A hidden peg disappears at frame 1. | 1 tile, 2-5 KB | Drop the overshoot and draw the 100% bead. |
| 3 | **Bead pop (erase)** | D-pad centre | 67 ms / 4 | in-cubic | The bead shrinks from 100% to 0% while the peg and empty checker show behind it. | 1 tile | Draw the empty cell. |
| 4 | **Eyedropper** | Hold centre 600 ms | 117 ms / 7 | out-cubic | Frame 1: the cell's ring blinks white. The target chip runs anim 5. **The strip switches to full brightness during the pick (fixes the review issue that the pick was invisible).** | 1 tile + strip band | Skip the blink. |
| 5 | **Chip select** | Encoder turn (slot) | 100 ms / 6 | out-cubic | The ring jumps on frame 1 (instant feedback). The new chip rises 0→4 px with its shadow growing; the old chip settles 4→0. | Strip band 450x96x2 = 86 KB → ~2.5 ms; or 2 chip boxes 2x(84x96) | Draw the final lift. |
| 6 | **Browse (press+turn)** | Press and turn | none | – | Chips scroll instantly, as today, for speed and acceleration. | strip band | – |
| 7 | **Focus change** | First input on the other area | 67 ms / 4 | linear | Strip and name line fade 50%↔100%. The cursor ring fades the opposite way. | strip + line bands ≈ 130 KB | Snap. Two bands are allowed to go 2 frames late. |
| 8 | **Menu open** | MENU | 117 ms / 7 | out-cubic | **System 7 "zoom rects"**: 4 white 1 px outline rectangles step out from the MENU corner to the panel bounds (frames 1-4). On frames 5-7 the board dims 100%→38% and the panel with its shadow appears complete on frame 5. | Frames 1-4: outlines only (< 10 KB). Frame 5: one full screen. Dim the board in place with `c>>1`-style shifts or a precomputed 540 KB dimmed copy. | Drop the zoom rects and present the final menu. |
| 9 | **Menu close** | MENU / centre / close box | 67 ms / 4 | in-cubic | The panel disappears on frame 1 (redraw the board at 38%). The board brightens 38%→100% over 3 frames. Reverse zoom rects are optional. | 1-3 full frames | Single full redraw. |
| 10 | **Highlight move** | Up/Down/turn in a list | 67 ms / 4 | out-cubic | The selection frame slides between rows, using 2-row bands. Scrolling lists snap. | 2 rows ≈ 2x(426x56x2) = 95 KB | Snap if the next turn arrives within 67 ms. |
| 11 | **Commit blink** | Press on a row/button | 2x60 ms | – | Mac menu blink: the frame inverts off/on twice, then the action happens. | 1 row | **Skip** when a physical press repeats quickly. 120 ms is the maximum added latency. |
| 12 | **Submenu / picker** | Select a ▸ row | 100 ms / 6 | out-cubic | Zoom rects from the row to the child panel (frames 1-3). The child appears with its shadow on frame 4. The parent dims to 45%. | Outlines, then the child rect (~300 KB) | Present the child directly. |
| 13 | **Dialog open** | Destructive confirm | 100 ms / 6 | out-cubic | Zoom rects from the triggering row to the dialog. The dialog appears on frame 4 with its default (Cancel) ring already drawn. | Outlines, then the dialog rect 404x316 (255 KB) | Present directly. |
| 14 | **Dialog close** | Any choice | 0 | – | Instant (Mac). The result shows straight away: e.g. Save closes to the board and the name line flashes "Saved to 1" for 1.2 s (review: Save had no confirmation). | – | – |
| 15 | **Saver enter** | Idle timeout | 500 ms | linear | **Brightness** ramp 100%→0 via `set_brightness`. No pixels are drawn. The saver starts at 0 brightness, then ramps up to the saver level (≤60%) over 300 ms. | ~0 (register writes) | Cut straight to the saver. |
| 16 | **Saver exit** | Any input | 150 ms | out-cubic | The saver stops at once. The board is drawn while brightness is 0, then brightness ramps 0→100%. The waking press is swallowed (keep today's wake rule, `saver_wake` golden). | One full frame (hidden) | Brightness to 100 at once. |
| 17 | **Marquee** | As today | 50 px/s | linear | Already exists (`render.c:206`), unchanged. At native it moves 2 px per 20 ms, so use 1 px steps at 60 fps for smoothness. | Name line band | – |

**Burn-in and power**
- Every animation ends in a static frame, and none loops.
- The saver keeps moving.
- The zoom rects and the 2-px-white cursor are the brightest static pixels. The cursor moves with use, and the idle dim (`render_idle_tick` TODO) covers the rest.
- Animations add CPU work only while they run, about 100 ms per input. Their power cost is negligible next to the panel.
