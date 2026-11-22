#pragma once
#include <stdint.h>
#include <stdbool.h>

// Grid size is per pattern: 9x9, 12x12 or 15x15 (UI.n). Cells are row-major with
// stride UI.n; the rest of the array (up to GRID_MAX^2) stays CELL_EMPTY.
#define GRID_MIN 9
#define GRID_MAX 15
#define GRID_SIZES 3
static inline int grid_size(int i) { return GRID_MIN + 3 * i; }   // 9, 12, 15
static inline int grid_size_index(int n) { return n >= 15 ? 2 : n >= 12 ? 1 : 0; }
#define CELL_EMPTY 0xFF
#define SLOTS 5          // shortlist: colours the encoder cycles through

typedef enum { MODE_DRAW, MODE_MENU } ui_mode_t;
// Which part the user is working with: the strip is bright for PALETTE (encoder /
// browse used last) and dimmed for BOARD (D-pad / place / erase used last).
typedef enum { FOCUS_BOARD, FOCUS_PALETTE } ui_focus_t;

typedef struct {
    uint8_t cells[GRID_MAX * GRID_MAX]; // palette colour index, CELL_EMPTY = none
    int n;                          // grid size (9, 12 or 15), stored with the pattern
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
    bool motion;                    // micro-animations (Menu > Motion, Waveshare only)
} ui_state_t;

extern ui_state_t UI;

static inline int ui_color(void) { return UI.slots[UI.slot]; }

void ui_run(void); // main loop, never returns
