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
#include "menu.h"
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define GAP 2
#define COL_BG       0x0000
#define COL_FRAME    0xFFFF  // cursor / selected chip
#define COL_FRAME_DIM 0x7BEF // cursor while the strip has focus (= dim(COL_FRAME))
#define COL_EMPTY    0x18E3  // empty cell
#define COL_DIVIDER  0x39E7
#define COL_NAME     0xFFFF
#define COL_TEXT     0xFFFF
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
// drawn dimmed: the cursor frame while browsing colours, the strip while placing.
// dim(COL_FRAME) == COL_FRAME_DIM, so both use exactly the same amount.
static inline uint16_t dim(uint16_t c) { return (uint16_t)((c >> 1) & 0x7BEF); }

// Strip colours: full brightness only while it has focus (browsing).
static uint16_t strip_col(uint16_t c) { return UI.browsing ? c : dim(c); }

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

#define TILE_MAX 32                          // largest tile edge (grid cell + ring = 26)
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
    return UI.browsing ? COL_FRAME_DIM : COL_FRAME;
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

// The strip shows a list of chips: normally the SLOTS shortlist, while browsing a
// scrolling window onto the whole palette in browse order (grouped by family).
static int s_first;  // first list position visible

static int strip_len(void)  { return UI.browsing ? pal()->count : SLOTS; }
static int strip_sel(void)  { return UI.browsing ? UI.browse_pos : UI.slot; }
static int strip_item(int i){ return UI.browsing ? palette_browse_index(pal(), i) : UI.slots[i]; }

static int strip_visible(void)
{
    int fit = (DISPLAY_W - 2 * 8) / CHIP_PITCH, n = strip_len();
    return n < fit ? n : fit;
}

// Scroll so list position `i` is visible.
static void strip_scroll_to(int i)
{
    int vis = strip_visible(), f = s_first;
    if (i < f) f = i;
    if (i >= f + vis) f = i - vis + 1;
    if (f > strip_len() - vis) f = strip_len() - vis;
    if (f < 0) f = 0;
    s_first = f;
}

// The whole strip row is composed off-screen and sent in ONE blit: switching
// between the 5 slots and the catalogue window moves every chip, which done
// chip by chip would need a visible clear.
#define STRIP_H (CHIP + 8)
static uint16_t s_strip[DISPLAY_W * STRIP_H];

static void strip_rect(int x0, int y0, int x1, int y1, uint16_t c)
{
    uint16_t b = be16(c);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) s_strip[y * DISPLAY_W + x] = b;
}

static void draw_strip(void)
{
    strip_scroll_to(strip_sel());
    strip_rect(0, 0, DISPLAY_W, STRIP_H, COL_BG);
    int vis = strip_visible(), sel = strip_sel();
    int x0 = (DISPLAY_W - vis * CHIP_PITCH) / 2 + (CHIP_PITCH - CHIP) / 2 - 4;
    for (int k = 0; k < vis; k++) {
        int i = s_first + k, x = x0 + k * CHIP_PITCH;
        // white ring, black gap, colour square (mockup style, square)
        if (i == sel) {
            strip_rect(x, 0, x + STRIP_H, STRIP_H, strip_col(COL_FRAME));
            strip_rect(x + 2, 2, x + STRIP_H - 2, STRIP_H - 2, COL_BG);
        }
        uint16_t c = pal()->colors[strip_item(i)].rgb565;
        // Very dark chips (black, charcoal) vanish on the black strip: outline them.
        int luma = ((c >> 11) * 8 * 3 + ((c >> 5) & 0x3F) * 4 * 6 + (c & 0x1F) * 8) / 10;
        if (luma < 48)
            strip_rect(x + 3, 3, x + STRIP_H - 3, STRIP_H - 3, strip_col(COL_DIVIDER));
        strip_rect(x + 4, 4, x + STRIP_H - 4, STRIP_H - 4, strip_col(c));
    }
    D->blit(0, CHIP_Y - 4, DISPLAY_W, STRIP_H, s_strip);
}

// "S93 Peach ...... 13": code + name left, bead count right. The whole line is
// composed off-screen and sent in one blit, so it never shows half updated.
// A label wider than its space is shown shortened with "..."; after
// MARQUEE_DELAY_US unchanged it scrolls once right-to-left (one blit per frame),
// then shows the shortened form again and waits another MARQUEE_DELAY_US.
#define MARQUEE_DELAY_US 5000000
#define MARQUEE_FRAME_MS 40
#define MARQUEE_STEP     2          // px per frame (50 px/s)
#define MARQUEE_GAP      48         // px between the end and the repeated start

static uint16_t s_line[DISPLAY_W * TEXT_H];
static char s_info_label[48];
static bool s_info_long;            // label wider than its space
static bool s_marquee;              // currently scrolling
static int s_marquee_off;
static int64_t s_info_since_us;     // label unchanged since

static void compose_info(void)
{
    int idx = ui_color();
    char num[8];
    snprintf(num, sizeof num, "%d", count_color(idx));
    const uint16_t bg = be16(COL_BG);
    for (int i = 0; i < DISPLAY_W * TEXT_H; i++) s_line[i] = bg;
    int cw = text_width(num);
    int cx = DISPLAY_W - TEXT_PAD - cw;
    int x0 = TEXT_PAD, x1 = cx - 8;               // label space
    uint16_t fg = be16(strip_col(COL_NAME));
    if (s_marquee) {
        int tw = text_width(s_info_label);
        text_render_clip(s_line, DISPLAY_W, x0 - s_marquee_off, x0, x1, s_info_label, fg, bg);
        text_render_clip(s_line, DISPLAY_W, x0 - s_marquee_off + tw + MARQUEE_GAP, x0, x1,
                         s_info_label, fg, bg);
    } else {
        char fit[52];
        text_fit(s_info_label, x1 - x0, fit, sizeof fit);
        text_render(s_line, DISPLAY_W, x0, fit, x1 - x0, fg, bg);
    }
    text_render(s_line, DISPLAY_W, cx, num, cw, be16(strip_col(COL_COUNT)), bg);
    D->blit(0, TEXT_Y, DISPLAY_W, TEXT_H, s_line);
}

// Content changed (colour, count, focus): show it shortened and restart the delay.
static void draw_info(void)
{
    const bead_color_t *bc = &pal()->colors[ui_color()];
    // The code first (what gets ordered), then the name.
    snprintf(s_info_label, sizeof s_info_label, "%s %s", bc->code, bc->name);
    char num[8];
    snprintf(num, sizeof num, "%d", count_color(ui_color()));
    int space = DISPLAY_W - TEXT_PAD - text_width(num) - 8 - TEXT_PAD;
    s_info_long = text_width(s_info_label) > space;
    s_marquee = false;
    s_marquee_off = 0;
    s_info_since_us = esp_timer_get_time();
    compose_info();
}

int render_tick_ms(void)
{
    if (UI.mode != MODE_DRAW || !s_info_long) return -1;
    if (s_marquee) return MARQUEE_FRAME_MS;
    int64_t left = MARQUEE_DELAY_US - (esp_timer_get_time() - s_info_since_us);
    return left <= 0 ? 1 : (int)(left / 1000) + 1;
}

void render_tick(void)
{
    if (UI.mode != MODE_DRAW || !s_info_long) return;
    int64_t now = esp_timer_get_time();
    if (!s_marquee) {
        if (now - s_info_since_us < MARQUEE_DELAY_US) return;
        s_marquee = true;
        s_marquee_off = 0;
    }
    s_marquee_off += MARQUEE_STEP;
    if (s_marquee_off >= text_width(s_info_label) + MARQUEE_GAP) {
        s_marquee = false;                         // one full pass: rest again
        s_info_since_us = now;
    }
    compose_info();
}

// ---- menu / list screens --------------------------------------------------------
// Title band + VISIBLE rows, each ROW_H tall, each composed off-screen in the
// band buffer (s_strip, same 240 x 32 size) and sent in one blit. Switching screens
// replaces the old content band by band; the screen never goes blank in between.
#define ROW_H        32
#define MENU_VISIBLE ((DISPLAY_H - ROW_H) / ROW_H)    // 9; menu.c VISIBLE_ROWS
_Static_assert(STRIP_H == ROW_H, "menu bands reuse the strip buffer");
#define COL_TEXT_DIM 0xAD55

static void band_text(int x, const char *str, int max_w, uint16_t fg)
{
    text_render(s_strip + 3 * DISPLAY_W, DISPLAY_W, x, str, max_w, be16(fg), be16(COL_BG));
}

static void draw_title(const menu_view_t *v)
{
    strip_rect(0, 0, DISPLAY_W, ROW_H, COL_BG);
    band_text(8, v->title, DISPLAY_W - 16, COL_TEXT);
    strip_rect(8, ROW_H - 2, DISPLAY_W - 8, ROW_H - 1, COL_DIVIDER);
    D->blit(0, 0, DISPLAY_W, ROW_H, s_strip);
}

static void draw_menu_row(const menu_view_t *v, int i)
{
    int k = i - v->top;
    if (k < 0 || k >= MENU_VISIBLE) return;
    strip_rect(0, 0, DISPLAY_W, ROW_H, COL_BG);
    if (i < v->count) {
        menu_row_t r;
        menu_row(i, &r);
        if (i == v->sel) {                               // white frame, like the cursor
            strip_rect(2, 0, DISPLAY_W - 2, ROW_H, COL_FRAME);
            strip_rect(4, 2, DISPLAY_W - 4, ROW_H - 2, COL_BG);
        }
        int x = 12;
        if (r.swatch >= 0) {
            uint16_t c = (uint16_t)r.swatch;
            int luma = ((c >> 11) * 8 * 3 + ((c >> 5) & 0x3F) * 4 * 6 + (c & 0x1F) * 8) / 10;
            if (luma < 48) strip_rect(9, 5, 31, 27, COL_DIVIDER);
            strip_rect(10, 6, 30, 26, c);
            x = 38;
        }
        int rw = text_width(r.right);
        int rx = DISPLAY_W - 12 - rw;
        char fit[52];
        text_fit(r.left, rx - x - 6, fit, sizeof fit);  // long names end in "..."
        band_text(x, fit, rx - x - 6, COL_TEXT);
        if (rw) band_text(rx, r.right, rw, COL_TEXT_DIM);
    }
    D->blit(0, ROW_H * (1 + k), DISPLAY_W, ROW_H, s_strip);
}

static menu_view_t s_menu_shown;

static void render_menu_full(void)
{
    menu_view_t v;
    menu_view(&v);
    draw_title(&v);
    for (int k = 0; k < MENU_VISIBLE; k++) draw_menu_row(&v, v.top + k);
    s_menu_shown = v;
}

static void render_menu_update(void)
{
    menu_view_t v;
    menu_view(&v);
    if (v.version != s_menu_shown.version || v.top != s_menu_shown.top ||
        v.count != s_menu_shown.count) {
        render_menu_full();                              // content/scroll changed
        return;
    }
    if (strcmp(v.title, s_menu_shown.title) != 0) draw_title(&v);   // e.g. family
    if (v.sel != s_menu_shown.sel) {                     // just the highlight moved
        draw_menu_row(&v, s_menu_shown.sel);
        draw_menu_row(&v, v.sel);
    }
    s_menu_shown = v;
}

// ---- screen state -------------------------------------------------------------

// What is on screen now, so render_update() can redraw only what changed.
typedef struct {
    ui_mode_t mode;
    int x, y;
    bool browsing;
    int slot, pos, color, count;
    uint8_t slots[SLOTS];
} shown_t;
static shown_t s_shown;

static void snapshot(shown_t *v)
{
    memset(v, 0, sizeof *v);            // padding too, so memcmp is meaningful
    v->mode = UI.mode; v->x = UI.cur_x; v->y = UI.cur_y;
    v->browsing = UI.browsing; v->slot = UI.slot; v->pos = UI.browse_pos;
    v->color = ui_color(); v->count = count_color(v->color);
    memcpy(v->slots, UI.slots, SLOTS);
}

static int grid_mode(ui_mode_t m) { return m == MODE_DRAW; }

void render_full(void)
{
#if CONFIG_BEAD_PERF_LOG
    ESP_LOGW("perf", "FULL REDRAW");
#endif
    static int once = 0;
    if (!once) { layout(); D->init(); once = 1; }
    if (grid_mode(UI.mode)) {
        // No full-screen clear (it would flash): cell tiles, strip and info line
        // each overwrite their area in one blit; only the margins between them
        // are filled, straight to their final black.
        const int gx0 = s_ox - 1, gx1 = gx0 + GRID_N * (s_cell + GAP);
        const int gy0 = s_oy - 1, gy1 = gy0 + GRID_N * (s_cell + GAP);
        const int sy0 = CHIP_Y - 4, sy1 = sy0 + STRIP_H;
        D->fill_rect(0, 0, DISPLAY_W, gy0, COL_BG);
        D->fill_rect(0, gy0, gx0, gy1 - gy0, COL_BG);
        D->fill_rect(gx1, gy0, DISPLAY_W - gx1, gy1 - gy0, COL_BG);
        D->fill_rect(0, gy1, DISPLAY_W, DIVIDER_Y - gy1, COL_BG);
        D->fill_rect(0, DIVIDER_Y, 8, 1, COL_BG);
        D->fill_rect(8, DIVIDER_Y, DISPLAY_W - 16, 1, COL_DIVIDER);
        D->fill_rect(DISPLAY_W - 8, DIVIDER_Y, 8, 1, COL_BG);
        D->fill_rect(0, DIVIDER_Y + 1, DISPLAY_W, sy0 - DIVIDER_Y - 1, COL_BG);
        D->fill_rect(0, sy1, DISPLAY_W, TEXT_Y - sy1, COL_BG);
        D->fill_rect(0, TEXT_Y + TEXT_H, DISPLAY_W, DISPLAY_H - TEXT_Y - TEXT_H, COL_BG);
        draw_grid();
        draw_strip();
        draw_info();
    } else {
        render_menu_full();
    }
    snapshot(&s_shown);
}

void render_update(void)
{
    // Switching between drawing and the menu repaints everything (band by band).
    if (UI.mode != s_shown.mode) { render_full(); return; }
    if (!grid_mode(UI.mode)) { render_menu_update(); return; }
    shown_t now;
    snapshot(&now);

    // Grid: old cursor cell loses its frame; the cursor cell is redrawn on every
    // event (place/erase change it; its frame dims while browsing).
    if (now.x != s_shown.x || now.y != s_shown.y) draw_cell(s_shown.x, s_shown.y, COL_BG);
    draw_cell(UI.cur_x, UI.cur_y, cursor_frame());

    // Strip: any change of list, selection, slot colours or focus -> one blit.
    if (now.browsing != s_shown.browsing || now.slot != s_shown.slot ||
        now.pos != s_shown.pos || memcmp(now.slots, s_shown.slots, SLOTS) != 0)
        draw_strip();
    if (now.browsing != s_shown.browsing || now.color != s_shown.color ||
        now.count != s_shown.count)
        draw_info();
    s_shown = now;
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
