#pragma once
#include <stdint.h>
#include <stdbool.h>

#define GRID_N 9
#define CELL_EMPTY 0xFF
#define SLOTS 5          // shortlist: colours the encoder cycles through

typedef enum { MODE_DRAW, MODE_MENU } ui_mode_t;
// Which part the user is working with: the strip is bright for PALETTE (encoder /
// browse used last) and dimmed for BOARD (D-pad / place / erase used last).
typedef enum { FOCUS_BOARD, FOCUS_PALETTE } ui_focus_t;

typedef struct {
    uint8_t cells[GRID_N * GRID_N]; // palette colour index, CELL_EMPTY = none
    int cur_x, cur_y;               // cursor, wraps at edges
    int palette;                    // index into PALETTES[]
    uint8_t slots[SLOTS];           // shortlist (palette colour indices)
    int slot;                       // selected slot: the colour that gets placed
    bool browsing;                  // encoder held + turned: browsing the catalogue
    int browse_pos;                 // position in the palette's browse order
    ui_mode_t mode;
    ui_focus_t focus;
    bool unsaved;                   // beads changed since the last Save/Load (Pattern menu)
    // Canvas look: device-wide display setting, not part of a pattern (board_style.h)
    uint8_t board;                  // board_t layout
    uint8_t texture;                // 0..TEXTURE_COUNT-1
    bool bevel;                     // subtle bead bevel
    uint8_t saver;                  // screen saver timeout, index into SAVER_TIMEOUT_S
} ui_state_t;

extern ui_state_t UI;

static inline int ui_color(void) { return UI.slots[UI.slot]; }

void ui_run(void); // main loop, never returns
