// Drawing via board display ops only — no GUI library, no full framebuffer.
// Layout (portrait, DISPLAY_W wide):
//   top    square 9x9 bead grid (DISPLAY_W x DISPLAY_W)
//   below  divider, then the palette strip: one square chip per colour (selected
//          one framed), and a text row with the colour name and how many beads of
//          that colour are in the pattern.
// Updates are incremental (render_update): only cells, chips and text that changed
// are redrawn, because a full clear + redraw over SPI visibly flashes.
#include "render.h"
#include "ui.h"
#include "board.h"
#include "palettes.h"
#include "text.h"
#include <stdio.h>
#include "esp_log.h"
#include "sdkconfig.h"

#define GAP 2
#define COL_BG       0x0000
#define COL_FRAME    0xFFFF  // cursor / selected chip
#define COL_FRAME_DIM 0x7BEF // cursor while the strip has focus (= dim(COL_FRAME))
#define COL_EMPTY    0x18E3  // empty cell
#define COL_DIVIDER  0x39E7
#define COL_NAME     0xFFFF
#define COL_COUNT    0xAD55

// Strip geometry (below the grid)
#define DIVIDER_Y  (DISPLAY_W + 3)
#define CHIP       24          // chip square
#define CHIP_PITCH 36          // chip + frame + spacing
#define CHIP_Y     (DIVIDER_Y + 9) // frame ring starts 4 px above
#define TEXT_Y     (CHIP_Y + CHIP + 12)
#define TEXT_PAD   12

_Static_assert(TEXT_Y + TEXT_H <= DISPLAY_H, "strip text must fit on screen");

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

static const palette_t *pal(void) { return &PALETTES[UI.palette]; }

// Half brightness (each RGB565 channel >> 1). Whichever area does NOT have focus is
// drawn dimmed: the cursor frame while picking a colour, the strip while placing.
// dim(COL_FRAME) == COL_FRAME_DIM, so both use exactly the same amount.
static inline uint16_t dim(uint16_t c) { return (uint16_t)((c >> 1) & 0x7BEF); }

// Strip colours: full brightness only while it has focus (picking).
static uint16_t strip_col(uint16_t c) { return UI.mode == MODE_PALETTE ? c : dim(c); }

// The chip the strip is showing: the colour being picked, or the selected one.
static int shown_chip(void) { return UI.mode == MODE_PALETTE ? UI.pick : UI.color; }

static int count_color(int idx)
{
    int n = 0;
    for (int i = 0; i < GRID_N * GRID_N; i++) n += (UI.cells[i] == idx);
    return n;
}

// ---- off-screen tiles ---------------------------------------------------------
// Every visible update is composed here and sent with ONE blit, so the panel never
// receives an intermediate state (a cell briefly white or black, a chip wiped
// before its redraw). The panel has no TE line and refreshes from its RAM about
// every 16 ms regardless of us, so any multi-pass drawing sometimes gets scanned
// out half done: that was the occasional "flash" on keypresses.
// Static buffers live in internal RAM, which is DMA-capable; blit returns only
// once the transfer has gone out, so they can be reused right after.

static inline uint16_t be16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

#define TILE_MAX (CHIP + 8)                 // largest tile edge (chip slot)
static uint16_t s_tile[TILE_MAX * TILE_MAX];

// Fill [x0,x1) x [y0,y1) of a w-wide tile with one colour.
static void tile_rect(int w, int x0, int y0, int x1, int y1, uint16_t c)
{
    uint16_t b = be16(c);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) s_tile[y * w + x] = b;
}

// ---- grid -------------------------------------------------------------------

static uint16_t cell_color(uint8_t idx)
{
    if (idx == CELL_EMPTY || idx >= pal()->count) return COL_EMPTY;
    return pal()->colors[idx].rgb565;
}

static uint16_t cursor_frame(void)
{
    return UI.mode == MODE_PALETTE ? COL_FRAME_DIM : COL_FRAME;
}

// frame = cursor frame colour (COL_BG = no frame).
// Each cell owns a (s_cell + 2) square tile: the cell plus its own 1 px half of the
// 2 px GAP on every side, so neighbouring tiles never overlap and drawing order
// does not matter. The cursor frame is 1 px wide and lives in that half-gap; the
// neighbour's half stays black, so the frame never touches a neighbouring bead.
// Inside the frame a 1 px black ring separates it from the cell's own colour (same
// layering as the strip chips). Unframed cells show their colour full size.
static void draw_cell(int x, int y, uint16_t frame)
{
    int px = s_ox + x * (s_cell + GAP);
    int py = s_oy + y * (s_cell + GAP);
    uint16_t c = cell_color(UI.cells[y * GRID_N + x]);
    const int w = s_cell + 2;
    tile_rect(w, 0, 0, w, w, frame);                       // frame (black if none)
    if (frame != COL_BG) {
        tile_rect(w, 1, 1, w - 1, w - 1, COL_BG);           // inner black ring
        tile_rect(w, 2, 2, w - 2, w - 2, c);
    } else {
        tile_rect(w, 1, 1, w - 1, w - 1, c);
    }
    D->blit(px - 1, py - 1, w, w, s_tile);
}

static void draw_grid(void)
{
    // Each cell's border overlaps the shared GAP with its neighbours, so the
    // cursor cell is drawn last; otherwise later cells paint over its frame.
    for (int y = 0; y < GRID_N; y++)
        for (int x = 0; x < GRID_N; x++)
            if (x != UI.cur_x || y != UI.cur_y) draw_cell(x, y, COL_BG);
    draw_cell(UI.cur_x, UI.cur_y, cursor_frame());
}

// ---- palette strip ----------------------------------------------------------

static int s_first;  // first chip index visible (strip scrolls for big palettes)

static int strip_visible(void)
{
    int fit = (DISPLAY_W - 2 * 8) / CHIP_PITCH;
    return pal()->count < fit ? pal()->count : fit;
}

// Scroll so chip `i` is visible. Returns 1 if the window moved.
static int strip_scroll_to(int i)
{
    int vis = strip_visible(), f = s_first;
    if (i < f) f = i;
    if (i >= f + vis) f = i - vis + 1;
    if (f > pal()->count - vis) f = pal()->count - vis;
    if (f < 0) f = 0;
    int moved = (f != s_first);
    s_first = f;
    return moved;
}

static void draw_chip(int i, int framed)
{
    int vis = strip_visible();
    if (i < s_first || i >= s_first + vis) return;
    int x0 = (DISPLAY_W - vis * CHIP_PITCH) / 2 + (CHIP_PITCH - CHIP) / 2;
    int x = x0 + (i - s_first) * CHIP_PITCH, y = CHIP_Y;
    // One tile: white ring, black gap, colour square (mockup style, square). The
    // slot (CHIP + 8) is narrower than CHIP_PITCH, so chip tiles never overlap.
    const int w = CHIP + 8;
    tile_rect(w, 0, 0, w, w, framed ? strip_col(COL_FRAME) : COL_BG);
    if (framed) tile_rect(w, 2, 2, w - 2, w - 2, COL_BG);
    tile_rect(w, 4, 4, w - 4, w - 4, strip_col(pal()->colors[i].rgb565));
    D->blit(x - 4, y - 4, w, w, s_tile);
}

static void draw_strip(void)
{
    // No clear first: every visible slot is redrawn as a whole tile, and the
    // spaces between slots are only ever black (from render_full).
    int vis = strip_visible(), sel = shown_chip();
    for (int i = s_first; i < s_first + vis; i++) draw_chip(i, i == sel);
}

// "Magenta ........ 13": name left, bead count right. The whole line is composed
// off-screen and sent in one blit, so it never shows half updated.
static uint16_t s_line[DISPLAY_W * TEXT_H];

static void draw_info(void)
{
    int idx = shown_chip();
    char num[8];
    snprintf(num, sizeof num, "%d", count_color(idx));
    const uint16_t bg = be16(COL_BG);
    for (int i = 0; i < DISPLAY_W * TEXT_H; i++) s_line[i] = bg;
    int cw = text_width(num);
    int cx = DISPLAY_W - TEXT_PAD - cw;
    text_render(s_line, DISPLAY_W, TEXT_PAD, pal()->colors[idx].name,
                cx - TEXT_PAD - 8, be16(strip_col(COL_NAME)), bg);
    text_render(s_line, DISPLAY_W, cx, num, cw, be16(strip_col(COL_COUNT)), bg);
    D->blit(0, TEXT_Y, DISPLAY_W, TEXT_H, s_line);
}

// ---- screen state -------------------------------------------------------------

// What is on screen now, so render_update() can redraw only what changed.
static ui_mode_t s_shown_mode;
static int s_shown_x, s_shown_y, s_shown_chip, s_shown_count;

static void remember_shown(void)
{
    s_shown_mode = UI.mode;
    s_shown_x = UI.cur_x; s_shown_y = UI.cur_y;
    s_shown_chip = shown_chip();
    s_shown_count = count_color(s_shown_chip);
}

static int grid_mode(ui_mode_t m) { return m == MODE_DRAW || m == MODE_PALETTE; }

void render_full(void)
{
#if CONFIG_BEAD_PERF_LOG
    ESP_LOGW("perf", "FULL REDRAW");
#endif
    static int once = 0;
    if (!once) { layout(); D->init(); once = 1; }
    D->fill_rect(0, 0, DISPLAY_W, DISPLAY_H, COL_BG);
    if (grid_mode(UI.mode)) {
        draw_grid();
        D->fill_rect(8, DIVIDER_Y, DISPLAY_W - 16, 1, COL_DIVIDER);
        strip_scroll_to(shown_chip());
        draw_strip();
        draw_info();
    }
    // MODE_SETTINGS: P1 TODO menu rows + selection bar
    // MODE_EXPORT:   P1 TODO QR code; until then SSID/URL text
    remember_shown();
}

void render_update(void)
{
    // Leaving or entering a non-grid screen needs the whole screen.
    if (!grid_mode(UI.mode) || !grid_mode(s_shown_mode)) { render_full(); return; }

    // Grid: old cursor cell loses its frame, new one drawn last. Place/erase only
    // change the cursor cell; the cursor frame dims while the strip has focus.
    if (s_shown_x != UI.cur_x || s_shown_y != UI.cur_y)
        draw_cell(s_shown_x, s_shown_y, COL_BG);
    if (s_shown_x != UI.cur_x || s_shown_y != UI.cur_y || s_shown_mode != UI.mode
        || UI.mode == MODE_DRAW)
        draw_cell(UI.cur_x, UI.cur_y, cursor_frame());

    // Strip: focus change re-colours the whole strip (dim <-> bright); otherwise
    // move the frame (or redraw the strip if it had to scroll).
    int chip = shown_chip();
    if (s_shown_mode != UI.mode) {
        strip_scroll_to(chip);
        draw_strip();
        draw_info();
    } else if (chip != s_shown_chip) {
        if (strip_scroll_to(chip)) draw_strip();
        else { draw_chip(s_shown_chip, 0); draw_chip(chip, 1); }
    }
    if (s_shown_mode == UI.mode && (chip != s_shown_chip || count_color(chip) != s_shown_count))
        draw_info();
    remember_shown();
}

void render_idle_tick(void)
{
    // P2 TODO (AMOLED burn-in): dim after 60 s idle, sleep display after 5 min.
}

void render_test_bars(void)
{
    // Five horizontal bars, top to bottom: red, green, blue, white, black.
    // Red and blue swapped = wrong RGB/BGR order; light/negative = wrong inversion.
    static const uint16_t bars[] = { 0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000 };
    const int n = sizeof bars / sizeof *bars;
    // D is set up by the first render_full(), which ui_run() calls before any event.
    for (int i = 0; i < n; i++) {
        int y0 = DISPLAY_H * i / n, y1 = DISPLAY_H * (i + 1) / n;
        D->fill_rect(0, y0, DISPLAY_W, y1 - y0, bars[i]);
    }
}
