#pragma once
// Canvas look: grid variant (cell gap + empty-cell style), 8x8 texture, bead bevel.
// The texture covers the whole drawing screen (behind the grid, the strip and the
// name line). All functions take SCREEN coordinates, so the texture lines up
// seamlessly across the separately-blitted tiles. Values come from UI (per
// pattern, saved with it).
#include <stdint.h>
#include <stdbool.h>

// Stored values: 0 and 1 keep the meaning of the earlier Faint/Pegboard layouts
// (24/2 solid, and the default), so saved patterns keep a sensible look.
typedef enum {
    BOARD_CLASSIC,   // 24 px cells, 2 px gaps, solid empty cells
    BOARD_SOLID,     // 22 px cells, 4 px gaps, solid empty cells without a peg (default)
    BOARD_PEGS,      // 22 / 4, empty cells see-through with a peg dot
    BOARD_BIG_PEGS,  // 24 / 2, empty cells see-through with a peg dot
    BOARD__COUNT
} board_t;
#define BOARD_DEFAULT BOARD_SOLID
#define TEXTURE_COUNT 8

extern const char *const BOARD_NAMES[BOARD__COUNT];

int board_gap(int n);                               // px between cells of an n x n grid
bool board_open(void);                              // empty cells see-through (Pegs, Big pegs)
uint16_t board_px(int x, int y);                    // background texture pixel
uint16_t slot_px(int x, int y, int cx, int cy, int cell);  // empty-cell pixel; cx,cy in cell
uint16_t bevel_px(uint16_t c, int dir);             // dir +1 = lit edge, -1 = shadow edge
