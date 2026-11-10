#pragma once
void render_full(void);        // redraw whole current screen
void render_update(void);      // redraw only what the last event changed
void render_idle_tick(void);   // dim/sleep AMOLED after inactivity
int  render_palette_cols(int count); // colour-wheel grid width for N colours
