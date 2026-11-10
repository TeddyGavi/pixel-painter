// Drawing via board display ops only — no GUI library, no full framebuffer.
// Layout (portrait): square grid on top (DISPLAY_W x DISPLAY_W), strip below
// for palette chip + name + count; menus draw over the grid area.
// P1 TODO: tiny bitmap font (glyph blit) + dirty-rect tracking. For now the
// skeleton redraws coarsely; fine-grained updates come with the font.
#include "render.h"
#include "ui.h"
#include "board.h"
#include "palettes.h"

#define GAP 2
static int s_cell;   // cell size in px
static int s_ox, s_oy;

static const display_ops_t *D;

static void layout(void)
{
    D = board_display();
    s_cell = (DISPLAY_W - (GRID_N + 1) * GAP) / GRID_N;
    int used = GRID_N * s_cell + (GRID_N + 1) * GAP;
    s_ox = (DISPLAY_W - used) / 2 + GAP;
    s_oy = s_ox;
}

static uint16_t cell_color(uint8_t idx)
{
    const palette_t *p = &PALETTES[UI.palette];
    if (idx == CELL_EMPTY || idx >= p->count) return 0x18E3; // dark empty
    return p->colors[idx].rgb565;
}

static void draw_cell(int x, int y, int highlighted)
{
    int px = s_ox + x * (s_cell + GAP);
    int py = s_oy + y * (s_cell + GAP);
    uint16_t c = cell_color(UI.cells[y * GRID_N + x]);
    if (highlighted) {
        D->fill_rect(px - GAP, py - GAP, s_cell + 2 * GAP, s_cell + 2 * GAP, 0xFFFF);
        D->fill_rect(px, py, s_cell, s_cell, c);
    } else {
        D->fill_rect(px - GAP, py - GAP, s_cell + 2 * GAP, s_cell + 2 * GAP, 0x0000);
        D->fill_rect(px, py, s_cell, s_cell, c);
    }
}

int render_palette_cols(int count) { return count <= 12 ? 4 : 8; }

void render_full(void)
{
    static int once = 0;
    if (!once) { layout(); D->init(); once = 1; }
    D->fill_rect(0, 0, DISPLAY_W, DISPLAY_H, 0x0000);
    switch (UI.mode) {
    case MODE_DRAW:
        for (int y = 0; y < GRID_N; y++)
            for (int x = 0; x < GRID_N; x++)
                draw_cell(x, y, x == UI.cur_x && y == UI.cur_y);
        // P1 TODO: palette strip with selected chip, name, code, bead count
        break;
    case MODE_PALETTE: {
        // pixelated colour grid over the drawing area
        const palette_t *p = &PALETTES[UI.palette];
        int cols = render_palette_cols(p->count);
        int sw = DISPLAY_W / (cols + 1);
        for (int i = 0; i < p->count; i++) {
            int cx = (i % cols), cy = (i / cols);
            int hl = (cx == UI.pick_x && cy == UI.pick_y);
            D->fill_rect(sw / 2 + cx * sw - 2, sw / 2 + cy * sw - 2, sw - 4 + 4 * hl,
                         sw - 4 + 4 * hl, hl ? 0xFFFF : 0x0000);
            D->fill_rect(sw / 2 + cx * sw, sw / 2 + cy * sw, sw - 8, sw - 8,
                         p->colors[i].rgb565);
        }
        break;
    }
    case MODE_SETTINGS: /* P1 TODO: menu rows + selection bar */ break;
    case MODE_EXPORT:   /* P1 TODO: QR code; until then SSID/URL text */ break;
    }
}

void render_update(void)
{
    // Coarse for now: P1 adds dirty-rect so only old+new cursor cells redraw.
    render_full();
}

void render_idle_tick(void)
{
    // P2 TODO (AMOLED burn-in): dim after 60 s idle, sleep display after 5 min.
}
