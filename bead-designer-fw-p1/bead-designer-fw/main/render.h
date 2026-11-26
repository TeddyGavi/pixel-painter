#pragma once
#include "board.h"
#include "input.h"
// Menu row height (title band and each row unit) and how many row units fit under
// the title: 32 px x 9 on 240x320, 56 px x 9 on the native 450x600.
#define MENU_ROW_H        (UI_HIRES ? 56 : 32)
#define MENU_VISIBLE_ROWS ((DISPLAY_H - MENU_ROW_H) / MENU_ROW_H)
void render_full(void);        // redraw whole current screen
void render_update(void);      // redraw only what the last event changed
void render_idle_tick(void);   // dim/sleep AMOLED after inactivity
void render_test_bars(void);
void render_message(const char *l1, const char *l2);   // full-screen notice (battery, shut down)
// Timed animation (name marquee): ms until render_tick() wants to run, -1 = never.
int  render_tick_ms(void);
void render_tick(void);   // bench: R/G/B/white/black bars to check colour order
// Motion (UI update stage 4, Waveshare only; Menu > Motion). render_event tells
// render_update which input caused the change (a place drops a bead, a paint stroke
// does not). render_anim_ms: ms until the next animation frame, -1 = none running.
void render_event(event_t ev);
int  render_anim_ms(void);
void render_anim_frame(void);
void render_anim_finish(void);  // jump to the final frame (any input does this first)
