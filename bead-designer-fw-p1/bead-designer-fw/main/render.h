#pragma once
void render_full(void);        // redraw whole current screen
void render_update(void);      // redraw only what the last event changed
void render_idle_tick(void);   // dim/sleep AMOLED after inactivity
void render_test_bars(void);
void render_message(const char *l1, const char *l2);   // full-screen notice (battery, shut down)
// Timed animation (name marquee): ms until render_tick() wants to run, -1 = never.
int  render_tick_ms(void);
void render_tick(void);   // bench: R/G/B/white/black bars to check colour order
