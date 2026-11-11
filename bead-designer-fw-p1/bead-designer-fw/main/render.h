#pragma once
void render_full(void);        // redraw whole current screen
void render_update(void);      // redraw only what the last event changed
void render_idle_tick(void);   // dim/sleep AMOLED after inactivity
void render_test_bars(void);   // bench: R/G/B/white/black bars to check colour order
