// Drawing via board display ops only — no GUI library, no full framebuffer.
// Layout (portrait, DISPLAY_W wide):
//   top    square 9x9 bead grid (DISPLAY_W x DISPLAY_W)
//   below  the palette strip: one square chip per colour (selected one framed),
//          and a text row with the colour name and how many beads of that colour
//          are in the pattern.
// The board texture (board_style.c) covers the whole drawing screen behind all of
// it; text drawn on it gets a 1 px black outline. Menu screens stay plain black.
// Updates are incremental (render_update): only cells, chips and text that changed
// are redrawn, because a full clear + redraw over SPI visibly flashes.
#include "render.h"
#include "ui.h"
#include "board.h"
#include "palettes.h"
#include "text.h"
#include "menu.h"
#include "board_style.h"
#include "qrcodegen.h"
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "battery.h"
#include "sdkconfig.h"

#define COL_BG       0x0000
#define COL_FRAME    0xFFFF  // cursor / selected chip
#define COL_FRAME_DIM 0x7BEF // cursor while the strip has focus (= dim(COL_FRAME))
#define COL_DIVIDER  0x39E7
#define COL_NAME     0xFFFF
#define COL_TEXT     0xFFFF
#define COL_COUNT    0xAD55

// Strip geometry (below the grid)
#define DIVIDER_Y  (DISPLAY_W + 3)   // top of the strip area (no line drawn)
#define CHIP       24          // chip square
#define CHIP_PITCH 36          // chip + frame + spacing
#define CHIP_Y     (DIVIDER_Y + 9) // frame ring starts 4 px above
#define TEXT_Y     (CHIP_Y + CHIP + 12)
#define TEXT_PAD   12

_Static_assert(TEXT_Y + TEXT_H + 1 <= DISPLAY_H, "strip text (+ outline) must fit on screen");

static int s_cell;   // cell size in px
static int s_gap;    // px between cells (depends on the board variant)
static int s_ox, s_oy;
static const display_ops_t *D;

static void layout(void)
{
    s_gap = board_gap();
    s_cell = (DISPLAY_W - (GRID_N + 1) * s_gap) / GRID_N;
    int used = GRID_N * s_cell + (GRID_N + 1) * s_gap;
    s_ox = (DISPLAY_W - used) / 2 + s_gap;
    s_oy = s_ox;
}

static const palette_t *pal(void) { return &PALETTES[UI.palette]; }

// Half brightness (each RGB565 channel >> 1). Whichever area does NOT have focus is
// drawn dimmed: the cursor frame while browsing colours, the strip while placing.
// dim(COL_FRAME) == COL_FRAME_DIM, so both use exactly the same amount.
static inline uint16_t dim(uint16_t c) { return (uint16_t)((c >> 1) & 0x7BEF); }

// Strip colours: full brightness only while it has focus (browsing).
static bool palette_focus(void) { return UI.browsing || UI.focus == FOCUS_PALETTE; }
static uint16_t strip_col(uint16_t c) { return palette_focus() ? c : dim(c); }

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

#define TILE_MAX (DISPLAY_W / GRID_N + 2)    // largest tile edge (cell + gap)
static uint16_t s_tile[TILE_MAX * TILE_MAX];

// ---- grid -------------------------------------------------------------------

static uint16_t cursor_frame(void)
{
    return palette_focus() ? COL_FRAME_DIM : COL_FRAME;
}

// frame = cursor frame colour (COL_BG = no frame).
// Each cell owns a (s_cell + s_gap) square tile: the cell plus its own half of the
// gap on every side, so neighbouring tiles never overlap and drawing order does
// not matter. The cursor frame is a 1 px ring right around the cell, inside that
// half-gap; the rest of the half-gap shows the board texture. Inside the frame a
// 1 px black ring separates it from the cell's own colour (same layering as the
// strip chips). Unframed cells show their colour full size.
static void draw_cell(int x, int y, uint16_t frame)
{
    const int h = s_gap / 2, n = s_cell, w = n + s_gap;
    int px = s_ox + x * (n + s_gap);
    int py = s_oy + y * (n + s_gap);
    uint8_t idx = UI.cells[y * GRID_N + x];
    bool empty = idx == CELL_EMPTY || idx >= pal()->count;
    uint16_t c = empty ? 0 : pal()->colors[idx].rgb565;
    bool framed = frame != COL_BG;
    // Bevel edges sit inside the cursor's black inner ring when framed.
    const int lo = framed ? 1 : 0, hi = framed ? n - 2 : n - 1;
    for (int ty = 0; ty < w; ty++) {
        for (int tx = 0; tx < w; tx++) {
            int X = px - h + tx, Y = py - h + ty;   // screen coordinates
            int cx = tx - h, cy = ty - h;           // inside the cell
            uint16_t v;
            if (cx < 0 || cy < 0 || cx >= n || cy >= n) {          // half-gap
                bool ring = cx >= -1 && cy >= -1 && cx <= n && cy <= n;
                v = framed && ring ? frame : board_px(X, Y);
            } else if (framed && (cx == 0 || cy == 0 || cx == n - 1 || cy == n - 1)) {
                v = COL_BG;                                   // cursor's inner black ring
            } else if (empty) {
                v = slot_px(X, Y, cx, cy, n);
            } else if (UI.bevel && (cx == hi || cy == hi)) {
                v = bevel_px(c, -1);                          // shadow bottom/right
            } else if (UI.bevel && (cx == lo || cy == lo)) {
                v = bevel_px(c, +1);                          // lit top/left
            } else {
                v = c;
            }
            s_tile[ty * w + tx] = be16(v);
        }
    }
    D->blit(px - h, py - h, w, w, s_tile);
}

static void draw_grid(void)
{
    // Tiles never overlap; the cursor cell is drawn with its frame.
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
    const int sy0 = CHIP_Y - 4;                      // board texture behind chips
    for (int y = 0; y < STRIP_H; y++)
        for (int x = 0; x < DISPLAY_W; x++)
            s_strip[y * DISPLAY_W + x] = be16(board_px(x, sy0 + y));
    int vis = strip_visible(), sel = strip_sel();
    int x0 = (DISPLAY_W - vis * CHIP_PITCH) / 2 + (CHIP_PITCH - CHIP) / 2 - 4;
    for (int k = 0; k < vis; k++) {
        int i = s_first + k, x = x0 + k * CHIP_PITCH;
        // Selected: 1 px white ring + 1 px black gap, same as the grid cursor.
        // Every chip has the 1 px black edge, so a mid-grey chip never blends
        // into the texture.
        if (i == sel)
            strip_rect(x + 2, 2, x + STRIP_H - 2, STRIP_H - 2, strip_col(COL_FRAME));
        strip_rect(x + 3, 3, x + STRIP_H - 3, STRIP_H - 3, COL_BG);
        uint16_t c = pal()->colors[strip_item(i)].rgb565;
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

// The line is 1 px taller than the text above and below, for the text outline.
#define LINE_H (TEXT_H + 2)
#define LINE_Y (TEXT_Y - 1)
static uint16_t s_line[DISPLAY_W * LINE_H];
static char s_info_label[48];
static bool s_info_long;            // label wider than its space
static bool s_marquee;              // currently scrolling
static int s_marquee_off;
static int64_t s_info_since_us;     // label unchanged since

// PowerBook-style battery icon, 15 x 8 px (body 13 x 8 + terminal nub), 3 segments,
// on a 1 px black margin. Big-endian pixels into a row buffer.
#define BATT_W 15
#define BATT_H 8
static void draw_battery(uint16_t *buf, int stride, int x, int y, int pct)
{
    uint16_t ink = be16(COL_TEXT), bg = be16(COL_BG);
    for (int yy = -1; yy <= BATT_H; yy++)
        for (int xx = -1; xx <= BATT_W; xx++) buf[(y + yy) * stride + x + xx] = bg;
    for (int xx = 0; xx < 13; xx++) { buf[y * stride + x + xx] = ink; buf[(y + 7) * stride + x + xx] = ink; }
    for (int yy = 0; yy < 8; yy++) { buf[(y + yy) * stride + x] = ink; buf[(y + yy) * stride + x + 12] = ink; }
    for (int yy = 2; yy < 6; yy++) { buf[(y + yy) * stride + x + 13] = ink; buf[(y + yy) * stride + x + 14] = ink; }
    int segs = pct > 66 ? 3 : pct > 33 ? 2 : pct > 10 ? 1 : 0;
    for (int k = 0; k < segs; k++)
        for (int yy = 2; yy < 6; yy++)
            for (int xx = 0; xx < 2; xx++) buf[(y + yy) * stride + x + 2 + 3 * k + xx] = ink;
}

static void compose_info(void)
{
    int idx = ui_color();
    char num[8];
    snprintf(num, sizeof num, "%d", count_color(idx));
    // Pass 1: render the glyphs as markers (NAME/COUNT ink, 0 = no ink) ...
    enum { NONE, NAME, COUNT };
    uint16_t *text = s_line + DISPLAY_W;          // text rows start 1 px down
    memset(s_line, 0, sizeof s_line);
    int cw = text_width(num);
    int cx = DISPLAY_W - TEXT_PAD - cw;
    bool batt = battery_low();                    // low battery: icon left of the count
    int bx = cx - 8 - BATT_W;
    int x0 = TEXT_PAD, x1 = (batt ? bx : cx) - 8; // label space
    if (s_marquee) {
        int tw = text_width(s_info_label);
        text_render_clip(text, DISPLAY_W, x0 - s_marquee_off, x0, x1, s_info_label, NAME, NONE);
        text_render_clip(text, DISPLAY_W, x0 - s_marquee_off + tw + MARQUEE_GAP, x0, x1,
                         s_info_label, NAME, NONE);
    } else {
        char fit[52];
        text_fit(s_info_label, x1 - x0, fit, sizeof fit);
        text_render(text, DISPLAY_W, x0, fit, x1 - x0, NAME, NONE);
    }
    text_render(text, DISPLAY_W, cx, num, cw, COUNT, NONE);
    // ... pass 2: ink -> colour, next to ink -> black outline, else board texture.
    // Rows are rewritten in place one behind, so keep the previous row's markers.
    static uint8_t prev[DISPLAY_W], cur[DISPLAY_W];
    const uint16_t ink[3] = { 0, be16(strip_col(COL_NAME)), be16(strip_col(COL_COUNT)) };
    memset(prev, 0, sizeof prev);
    for (int x = 0; x < DISPLAY_W; x++) cur[x] = (uint8_t)s_line[x];
    for (int y = 0; y < LINE_H; y++) {
        uint16_t *row = s_line + y * DISPLAY_W;
        const uint16_t *next = y + 1 < LINE_H ? row + DISPLAY_W : NULL;
        for (int x = 0; x < DISPLAY_W; x++) {
            uint16_t v;
            if (cur[x]) {
                v = ink[cur[x]];
            } else {
                bool near = false;
                for (int dx = -1; dx <= 1 && !near; dx++) {
                    int nx = x + dx;
                    if (nx < 0 || nx >= DISPLAY_W) continue;
                    near = prev[nx] || cur[nx] || (next && next[nx]);
                }
                v = near ? be16(COL_BG) : be16(board_px(x, LINE_Y + y));
            }
            row[x] = v;
        }
        memcpy(prev, cur, sizeof cur);
        if (next) for (int x = 0; x < DISPLAY_W; x++) cur[x] = (uint8_t)next[x];
    }
    if (batt) draw_battery(s_line, DISPLAY_W, bx, (LINE_H - BATT_H) / 2, battery_pct());
    D->blit(0, LINE_Y, DISPLAY_W, LINE_H, s_line);
}

// Content changed (colour, count, focus): show it shortened and restart the delay.
static void draw_info(void)
{
    const bead_color_t *bc = &pal()->colors[ui_color()];
    // The code first (what gets ordered), then the name.
    snprintf(s_info_label, sizeof s_info_label, "%s %s", bc->code, bc->name);
    char num[8];
    snprintf(num, sizeof num, "%d", count_color(ui_color()));
    int space = DISPLAY_W - TEXT_PAD - text_width(num) - 8 - TEXT_PAD - (battery_low() ? BATT_W + 8 : 0);
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
    // Battery icon in menu titles, unless the title needs the room (e.g. the QR
    // screen's "Scan with phone"): the full title wins over the icon there.
    int pct = battery_pct();
    bool icon = pct >= 0 && text_width(v->title) <= DISPLAY_W - 16 - (BATT_W + 8);
    band_text(8, v->title, DISPLAY_W - 16 - (icon ? BATT_W + 8 : 0), COL_TEXT);
    if (icon) draw_battery(s_strip, DISPLAY_W, DISPLAY_W - 8 - BATT_W, (ROW_H - BATT_H) / 2 - 1, pct);
    strip_rect(8, ROW_H - 2, DISPLAY_W - 8, ROW_H - 1, COL_DIVIDER);
    D->blit(0, 0, DISPLAY_W, ROW_H, s_strip);
}

// Rectangle in row coordinates, clipped to band b (rows are 1 or 2 bands tall).
static void row_rect(int b, int x0, int y0, int x1, int y1, uint16_t c)
{
    y0 -= b * ROW_H; y1 -= b * ROW_H;
    if (y0 < 0) y0 = 0;
    if (y1 > ROW_H) y1 = ROW_H;
    if (y0 < y1) strip_rect(x0, y0, x1, y1, c);
}

// Saved-slot thumbnail: 5 px per bead (45 px), a 1 px black gap and a 1 px white
// border (dim and empty for an empty slot), centred in the double-height row.
#define THUMB_PX   5
#define THUMB_X    12
#define THUMB_BOX  (GRID_N * THUMB_PX + 4)            // with gap + border: 49
static void draw_slot_thumb(int b, int rh, const menu_row_t *r)
{
    const int x0 = THUMB_X, y0 = (rh - THUMB_BOX) / 2;
    row_rect(b, x0, y0, x0 + THUMB_BOX, y0 + THUMB_BOX, r->thumb ? COL_FRAME : COL_DIVIDER);
    row_rect(b, x0 + 1, y0 + 1, x0 + THUMB_BOX - 1, y0 + THUMB_BOX - 1, COL_BG);
    const int tx = x0 + 2, ty = y0 + 2;
    if (!r->thumb) return;
    const bead_color_t *pc = PALETTES[r->thumb_pal].colors;
    for (int cy = 0; cy < GRID_N; cy++)
        for (int cx = 0; cx < GRID_N; cx++) {
            uint8_t c = r->thumb[cy * GRID_N + cx];
            row_rect(b, tx + cx * THUMB_PX, ty + cy * THUMB_PX, tx + (cx + 1) * THUMB_PX,
                     ty + (cy + 1) * THUMB_PX, c == CELL_EMPTY ? 0x2104 : pc[c].rgb565);
        }
}

// Screen y of row i (rows start under the title), or -1 if it is not on screen.
static int row_y(const menu_view_t *v, int i)
{
    if (i < v->top) return -1;
    int y = ROW_H;
    for (int j = v->top; j < i; j++) y += menu_row_units(j) * ROW_H;
    return y + menu_row_units(i) * ROW_H <= DISPLAY_H ? y : -1;
}

static void draw_menu_row(const menu_view_t *v, int i)
{
    if (i < 0 || i >= v->count) return;
    int y = row_y(v, i);
    if (y < 0) return;
    menu_row_t r;
    menu_row(i, &r);
    const int units = menu_row_units(i), rh = units * ROW_H;
    for (int b = 0; b < units; b++) {
        strip_rect(0, 0, DISPLAY_W, ROW_H, COL_BG);
        if (i == v->sel) {                       // 1 px white frame, like the cursor
            row_rect(b, 3, 1, DISPLAY_W - 3, rh - 1, COL_FRAME);
            row_rect(b, 4, 2, DISPLAY_W - 4, rh - 2, COL_BG);
        }
        int x = 12;
        if (r.slot) {
            draw_slot_thumb(b, rh, &r);
            x = THUMB_X + THUMB_BOX + 12;
        } else if (r.swatch >= 0) {
            uint16_t c = (uint16_t)r.swatch;
            int luma = ((c >> 11) * 8 * 3 + ((c >> 5) & 0x3F) * 4 * 6 + (c & 0x1F) * 8) / 10;
            if (luma < 48) strip_rect(9, 5, 31, 27, COL_DIVIDER);
            strip_rect(10, 6, 30, 26, c);
            x = 38;
        }
        // Text: first band carries the main line (+ right text), second band `sub`.
        const char *line = b == 0 ? r.left : r.sub;
        const char *right = b == 0 ? r.right : "";
        int rw = text_width(right);
        int rx = DISPLAY_W - 12 - rw;
        char fit[52];
        text_fit(line, rx - x - 6, fit, sizeof fit);    // long names end in "..."
        band_text(x, fit, rx - x - 6, b == 0 ? COL_TEXT : COL_TEXT_DIM);
        if (rw) band_text(rx, right, rw, COL_TEXT_DIM);
        D->blit(0, y + b * ROW_H, DISPLAY_W, ROW_H, s_strip);
    }
}

static menu_view_t s_menu_shown;

// QR code screen: black-on-white modules, centred under the title, as large as fits
// (integer px per module), with a light quiet zone of 4 modules (2 if space is
// tight); then a dim hint line. Composed band by band in s_strip like menu rows.
static void draw_qr_screen(const menu_view_t *v)
{
    const int size = qrcodegen_getSize(v->qr);
    const int area_h = DISPLAY_H - 2 * ROW_H;            // between title and hint
    const int lim = DISPLAY_W < area_h ? DISPLAY_W : area_h;
    int quiet = 4, s = lim / (size + 2 * quiet);
    if (s < 3) { quiet = 2; s = lim / (size + 2 * quiet); }
    if (s < 1) s = 1;
    const int box = (size + 2 * quiet) * s;
    const int bx = (DISPLAY_W - box) / 2, by = ROW_H + (area_h - box) / 2;
    const uint16_t white = be16(0xFFFF), black = be16(0x0000);
    for (int y0 = ROW_H; y0 < DISPLAY_H - ROW_H; y0 += ROW_H) {
        for (int r = 0; r < ROW_H; r++) {
            uint16_t *row = s_strip + r * DISPLAY_W;
            int y = y0 + r;
            for (int x = 0; x < DISPLAY_W; x++) row[x] = be16(COL_BG);
            if (y < by || y >= by + box) continue;
            int my = (y - by) / s - quiet;
            for (int x = bx; x < bx + box; x++) {
                int mx = (x - bx) / s - quiet;
                row[x] = qrcodegen_getModule(v->qr, mx, my) ? black : white;  // out of range = light
            }
        }
        D->blit(0, y0, DISPLAY_W, ROW_H, s_strip);
    }
    strip_rect(0, 0, DISPLAY_W, ROW_H, COL_BG);
    char fit[sizeof v->hint + 4];
    text_fit(v->hint, DISPLAY_W - 16, fit, sizeof fit);
    int hx = (DISPLAY_W - text_width(fit)) / 2;
    band_text(hx < 8 ? 8 : hx, fit, DISPLAY_W - 16, COL_TEXT_DIM);
    D->blit(0, DISPLAY_H - ROW_H, DISPLAY_W, ROW_H, s_strip);
}

static void render_menu_full(void)
{
    menu_view_t v;
    menu_view(&v);
    draw_title(&v);
    if (v.qr) { draw_qr_screen(&v); s_menu_shown = v; return; }
    int y = ROW_H;
    for (int i = v.top; i < v.count; i++) {             // rows that fit, in order
        if (row_y(&v, i) < 0) break;
        draw_menu_row(&v, i);
        y += menu_row_units(i) * ROW_H;
    }
    strip_rect(0, 0, DISPLAY_W, ROW_H, COL_BG);          // clear the rest, band by band
    for (; y < DISPLAY_H; y += ROW_H)
        D->blit(0, y, DISPLAY_W, DISPLAY_H - y < ROW_H ? DISPLAY_H - y : ROW_H, s_strip);
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
    bool browsing, palette_focus, bevel;
    int slot, pos, color, count, board, texture;
    uint8_t slots[SLOTS];
} shown_t;
static shown_t s_shown;

static void snapshot(shown_t *v)
{
    memset(v, 0, sizeof *v);            // padding too, so memcmp is meaningful
    v->mode = UI.mode; v->x = UI.cur_x; v->y = UI.cur_y;
    v->browsing = UI.browsing; v->slot = UI.slot; v->pos = UI.browse_pos;
    v->palette_focus = palette_focus(); v->bevel = UI.bevel;
    v->board = UI.board; v->texture = UI.texture;
    v->color = ui_color(); v->count = count_color(v->color);
    memcpy(v->slots, UI.slots, SLOTS);
}

static int grid_mode(ui_mode_t m) { return m == MODE_DRAW; }

// Board texture for a margin region, composed in the band buffer and sent at once.
static void board_region(int x0, int y0, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    // s_strip holds DISPLAY_W * STRIP_H pixels; margins are thin, split if needed
    int rows = (DISPLAY_W * STRIP_H) / w;
    for (int y = y0; y < y0 + h; y += rows) {
        int n = (y0 + h - y) < rows ? (y0 + h - y) : rows;
        for (int r = 0; r < n; r++)
            for (int x = 0; x < w; x++)
                s_strip[r * w + x] = be16(board_px(x0 + x, y + r));
        D->blit(x0, y, w, n, s_strip);
    }
}

void render_full(void)
{
#if CONFIG_BEAD_PERF_LOG
    ESP_LOGW("perf", "FULL REDRAW");
#endif
    static int once = 0;
    if (!once) { D = board_display(); D->init(); once = 1; }
    if (grid_mode(UI.mode)) {
        // No full-screen clear (it would flash): cell tiles, strip and info line
        // each overwrite their area in one blit; only the margins between them
        // are filled, straight to their final content (the board texture).
        layout();                                   // gap depends on the variant
        const int h = s_gap / 2, pitch = s_cell + s_gap;
        const int gx0 = s_ox - h, gx1 = gx0 + GRID_N * pitch;
        const int gy0 = s_oy - h, gy1 = gy0 + GRID_N * pitch;
        const int sy0 = CHIP_Y - 4, sy1 = sy0 + STRIP_H;
        board_region(0, 0, DISPLAY_W, gy0);
        board_region(0, gy0, gx0, gy1 - gy0);
        board_region(gx1, gy0, DISPLAY_W - gx1, gy1 - gy0);
        board_region(0, gy1, DISPLAY_W, sy0 - gy1);
        board_region(0, sy1, DISPLAY_W, LINE_Y - sy1);
        board_region(0, LINE_Y + LINE_H, DISPLAY_W, DISPLAY_H - LINE_Y - LINE_H);
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
    // Switching between drawing and the menu repaints everything (band by band);
    // so does a change of the canvas look.
    if (UI.mode != s_shown.mode || UI.board != s_shown.board ||
        UI.texture != s_shown.texture || UI.bevel != s_shown.bevel) { render_full(); return; }
    if (!grid_mode(UI.mode)) { render_menu_update(); return; }
    shown_t now;
    snapshot(&now);

    // Grid: old cursor cell loses its frame; the cursor cell is redrawn on every
    // event (place/erase change it; its frame dims while browsing).
    if (now.x != s_shown.x || now.y != s_shown.y) draw_cell(s_shown.x, s_shown.y, COL_BG);
    draw_cell(UI.cur_x, UI.cur_y, cursor_frame());

    // Strip: any change of list, selection, slot colours or focus -> one blit.
    if (now.browsing != s_shown.browsing || now.palette_focus != s_shown.palette_focus ||
        now.slot != s_shown.slot ||
        now.pos != s_shown.pos || memcmp(now.slots, s_shown.slots, SLOTS) != 0)
        draw_strip();
    if (now.palette_focus != s_shown.palette_focus || now.color != s_shown.color ||
        now.count != s_shown.count)
        draw_info();
    s_shown = now;
}

void render_idle_tick(void)
{
    // P2 TODO (AMOLED burn-in): dim after 60 s idle, sleep display after 5 min.
}

// Full-screen message (low battery, shutting down): two centred lines on black.
void render_message(const char *l1, const char *l2)
{
    D->fill_rect(0, 0, DISPLAY_W, DISPLAY_H, COL_BG);
    const char *lines[2] = { l1, l2 };
    for (int i = 0; i < 2; i++) {
        if (!lines[i]) continue;
        strip_rect(0, 0, DISPLAY_W, ROW_H, COL_BG);
        int w = text_width(lines[i]);
        band_text(w < DISPLAY_W - 16 ? (DISPLAY_W - w) / 2 : 8, lines[i], DISPLAY_W - 16, i ? COL_TEXT_DIM : COL_TEXT);
        D->blit(0, DISPLAY_H / 2 - ROW_H + i * ROW_H, DISPLAY_W, ROW_H, s_strip);
    }
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
