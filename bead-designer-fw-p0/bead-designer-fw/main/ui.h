#pragma once
#include <stdint.h>

#define GRID_N 9
#define CELL_EMPTY 0xFF

typedef enum { MODE_DRAW, MODE_PALETTE, MODE_SETTINGS, MODE_EXPORT } ui_mode_t;

typedef struct {
    uint8_t cells[GRID_N * GRID_N]; // palette colour index, CELL_EMPTY = none
    int cur_x, cur_y;               // cursor, wraps at edges
    int palette;                    // index into PALETTES[]
    int color;                      // selected colour index in current palette
    ui_mode_t mode;
    // palette-picker highlight
    int pick_x, pick_y;
    // settings
    int sel;                        // menu selection
} ui_state_t;

extern ui_state_t UI;

void ui_run(void); // main loop, never returns
