# UI update for the Waveshare 2.41" (planned, not built yet)

This folder is a **temporary working record** of the planned UI update. Delete it
once the update is built and these notes are no longer needed.

- **Status (2026-11-18):** designed and decided; implementation deferred. Battery
  management comes first, at the user's request.
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
