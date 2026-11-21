# User decisions on the mockups (2026-11-18)

These override spec.md and motion.md wherever they differ.

1. **Bead look:** A "Soft bead", with rounded corners (2–3 px).
2. **Text:** Chicago 3x with Scale3x smoothing.
3. **Texture:** keep the 2-px dots.
4. **Menus:** full black screens, not the floating window. Drop the dimmed-board backdrop and the window shadow.
5. **Menu title:** System 7 striped title bar with a close box.
6. **Grid size:** Pattern > Grid size (9 / 12 / 15), stored per pattern.
   - Always confirm a size change, including growing.
   - Shrinking crops to keep the most beads.
   - The pre-resize board can be restored until the next edit.
7. **Default grid size:** 9x9, for new patterns and after a factory reset.
8. **Colours:** monochrome chrome only. Warnings get no accent colour.
9. **Strip:** still dims to 50% while placing beads.
10. **Motion:** On/Off setting, default On.

## Added later

11. **Paint brush (requested and built 2026-11-18, ahead of the rest of this update).**
    - **How it works:** hold the place button (the encoder push) and move with the D-pad. The start cell and every cell moved onto get the current colour, and holding a direction paints a line (D-pad auto-repeat). Releasing places nothing extra.
    - **Unchanged:** a plain press still places one bead, and turning while held still browses.
    - **Code:** `EV_PAINT` in `input.c` / `ui.c`. Bench keys `[` / `]` hold and release the encoder. Test scenario `sim/scripts/paint.txt` (simulator and board).
    - **For the motion stage:** the brush stroke should feel continuous (no bead-drop bounce on every cell).
