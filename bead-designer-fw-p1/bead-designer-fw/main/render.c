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
// UI_HIRES (the 2.41" at its native 450x600, UI update stage 1): the "Soft bead"
// look from docs/ui-update-2.41 (bevel, sheen, rounded corners, contact shadow,
// cursor halo, lifted chips with shadows, outlined name line with a drop shadow),
// ported from the mockup scripts (scripts/screens.py, gfx.py). The 240x320
// Freenove keeps the original flat look and layout, pixel for pixel.
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
#include <stdlib.h>
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "battery.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define COL_BG       0x0000
#define COL_FRAME    0xFFFF  // cursor / selected chip
#define COL_FRAME_DIM 0x7BEF // cursor while the strip has focus (= dim(COL_FRAME))
#define COL_DIVIDER  0x39E7
#define COL_NAME     0xFFFF
#define COL_TEXT     0xFFFF
#define COL_COUNT    0xAD55

// Strip geometry (below the grid)
#if UI_HIRES
#define CHIP       60          // chip square (4.9 mm), spec section 3
#define CHIP_R     5           // chip corner radius
#define CHIP_PITCH 84
#define CHIP_CY    497         // chip centre
#define CHIP_LIFT  4           // the selected chip rises this much
#define STRIP_Y0   456         // strip band: lifted ring top 459 .. shadow bottom 537
#define STRIP_H    88
#define TEXT_Y     549         // name line text top
#define TEXT_PAD   16
#define LINE_Y     (TEXT_Y - 1)          // + outline above
#define LINE_H     (TEXT_H + 4)          // + outline and the (1, 2) drop shadow below
#define BATT_K     2                     // battery icon drawn 2x
#else
#define DIVIDER_Y  (DISPLAY_W + 3)   // top of the strip area (no line drawn)
#define CHIP       24          // chip square
#define CHIP_PITCH 36          // chip + frame + spacing
#define CHIP_Y     (DIVIDER_Y + 9) // frame ring starts 4 px above
#define STRIP_Y0   (CHIP_Y - 4)
#define STRIP_H    (CHIP + 8)
#define TEXT_Y     (CHIP_Y + CHIP + 12)
#define TEXT_PAD   12
// The line is 1 px taller than the text above and below, for the text outline.
#define LINE_H     (TEXT_H + 2)
#define LINE_Y     (TEXT_Y - 1)
#define BATT_K     1
#endif

_Static_assert(LINE_Y + LINE_H <= DISPLAY_H, "strip text (+ outline) must fit on screen");

static int s_cell;   // cell size in px
static int s_gap;    // px between cells (depends on the board variant)
static int s_ox, s_oy;
static const display_ops_t *D;

static void layout(void)
{
    const int n = UI.n;
    s_gap = board_gap(n);
    s_cell = (DISPLAY_W - (n + 1) * s_gap) / n;
    int used = n * s_cell + (n + 1) * s_gap;
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
#if UI_HIRES
// Motion (stage 4, render_motion.inc) draws its in-between frames through the normal
// drawing code by setting these overrides; with all of them off (their resting
// values) every screen is exactly the static one.
static int s_lvl = -1;          // strip brightness 0..256 (128 = dimmed), -1 = from focus
static int s_cur_px = -1, s_cur_py;   // cursor ring at this cell position (px), -1 = UI.cur_x/y
static int s_acell = -1;        // cell (y * GRID_MAX + x) drawn as a growing/shrinking bead
static bool s_lift_on;          // chip lifts from s_lift[] instead of "selected = lifted"
static int s_lift[SLOTS];
static int s_ring_y0 = -1, s_ring_y1;   // menu selection ring at this screen y range
static inline uint16_t scale565(uint16_t c, int k)   // each channel * k / 256
{
    return (uint16_t)((((c >> 11) * k) >> 8) << 11 | ((((c >> 5) & 0x3F) * k) >> 8) << 5 | (((c & 0x1F) * k) >> 8));
}
static uint16_t strip_col(uint16_t c)
{
    if (s_lvl >= 0) return s_lvl >= 256 ? c : scale565(c, s_lvl);   // 128 == dim()
    return palette_focus() ? c : dim(c);
}
#else
static uint16_t strip_col(uint16_t c) { return palette_focus() ? c : dim(c); }
#endif

static int count_color(int idx)
{
    int n = 0;
    for (int i = 0; i < UI.n * UI.n; i++) n += (UI.cells[i] == idx);
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

#define TILE_MAX (DISPLAY_W / GRID_MIN + 2)  // largest tile edge (cell + gap), 9x9
#if !UI_HIRES
static uint16_t s_tile[TILE_MAX * TILE_MAX];
#endif

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
#if UI_HIRES
#include "render_hires.inc"
#else
static void draw_cell(int x, int y, uint16_t frame)
{
    const int h = s_gap / 2, n = s_cell, w = n + s_gap;
    int px = s_ox + x * (n + s_gap);
    int py = s_oy + y * (n + s_gap);
    uint8_t idx = UI.cells[y * UI.n + x];
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
#endif

static void draw_grid(void)
{
#if UI_HIRES
    // Tile by tile (each includes the cursor's ring/halo where they overlap).
    hires_layout();
    const int h = s_gap / 2, pitch = s_cell + s_gap;
    for (int y = 0; y < UI.n; y++)
        for (int x = 0; x < UI.n; x++) {
            int X0 = s_ox + x * pitch - h, Y0 = s_oy + y * pitch - h;
            grid_rect(X0, Y0, X0 + pitch, Y0 + pitch);
        }
    return;
#endif
    // Tiles never overlap; the cursor cell is drawn with its frame.
    for (int y = 0; y < UI.n; y++)
        for (int x = 0; x < UI.n; x++)
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
#if UI_HIRES
static uint16_t *s_strip;           // DISPLAY_W x STRIP_H, in PSRAM (79 KB); menus and margins (allocated in render_full)
#else
static uint16_t s_strip[DISPLAY_W * STRIP_H];
#endif
#if UI_HIRES
static uint16_t *s_line;            // DISPLAY_W x LINE_H, internal RAM (also the strip's slices)
static uint8_t *s_mark;             // ink markers of the line (NONE/NAME/COUNT), internal RAM
#else
static uint16_t s_line[DISPLAY_W * LINE_H];
#endif

static void strip_rect(int x0, int y0, int x1, int y1, uint16_t c)
{
    uint16_t b = be16(c);
    if (b == 0 && x0 == 0 && x1 == DISPLAY_W && y0 < y1) {          // black full rows: one memset
        memset(s_strip + y0 * DISPLAY_W, 0, (size_t)(y1 - y0) * DISPLAY_W * sizeof *s_strip);
        return;
    }
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) s_strip[y * DISPLAY_W + x] = b;
}

#if UI_HIRES
// Chip shadows (spec section 3): resting (0, +2, blur 2, 55 %); selected, lifted
// CHIP_LIFT px, (0, +6, blur 4, 75 %).
static shadow_t s_chip_sh, s_chip_sh_sel;

// Chip decorations around a chip at (0, 0), in a box from (-CD, -CD): 1 = the 1 px
// black edge (r CHIP_R + 1), 2 = the selection ring (2 px white at +2..+3).
#define CD 4
#define CBOX (CHIP + 2 * CD)
static uint8_t s_chip_cls[CHIP * CHIP], s_chip_deco[CBOX * CBOX];

static void chip_maps(void)
{
    bead_classes(s_chip_cls, CHIP, CHIP_R);
    for (int v = 0; v < CBOX; v++)
        for (int u = 0; u < CBOX; u++) {
            int X = u - CD, Y = v - CD;
            uint8_t k = 0;
            if (in_rrect(X, Y, -1, -1, CHIP + 2, CHIP + 2, CHIP_R + 1)) k = 1;
            else
                for (int t = 2; t <= 3; t++) {
                    int x0 = -1 - t, w = CHIP + 2 + 2 * t, r = CHIP_R + 1 + t;
                    if (in_rrect(X, Y, x0, x0, w, w, r) && !in_rrect(X, Y, x0 + 1, x0 + 1, w - 2, w - 2, r - 1)) k = 2;
                }
            s_chip_deco[v * CBOX + u] = k;
        }
}

// Chip at (x, y) with colour c (already focus-dimmed): 1 px black edge and the
// Soft-bead art; the selected one also gets the 2 px white ring at +2..+3. Only
// rows [wy0, wy0 + rows) of the screen are drawn, into s_line.
#define STRIP_SUB 22
_Static_assert(STRIP_SUB <= LINE_H, "strip slices are composed in s_line");
static void strip_chip(int wy0, int rows, int x, int y, uint16_t c, bool sel)
{
    uint16_t tone[BT__N];
    bead_tones(c, UI.bevel, tone);
    const uint16_t ring = be16(strip_col(COL_FRAME)), edge = be16(COL_BG);
    for (int v = 0; v < CBOX; v++) {
        int Y = y - CD + v, by = Y - wy0;
        if (by < 0 || by >= rows) continue;
        for (int u = 0; u < CBOX; u++) {
            int X = x - CD + u, cx = u - CD, cy = v - CD;
            if (X < 0 || X >= DISPLAY_W) continue;
            uint16_t *o = &s_line[by * DISPLAY_W + X];
            int k = cx >= 0 && cy >= 0 && cx < CHIP && cy < CHIP ? s_chip_cls[cy * CHIP + cx] : BT_OUT;
            if (k != BT_OUT) *o = be16(tone[k]);
            else if (s_chip_deco[v * CBOX + u] == 1) *o = edge;
            else if (sel && s_chip_deco[v * CBOX + u] == 2) *o = ring;
        }
    }
}

// Background of each chip slot (texture + the chip's shadow, resting or lifted),
// CHIP_PITCH wide and STRIP_H tall, cached in PSRAM per texture and layout: the
// shadows only change with the texture, and blending them per draw cost ~14 ms.
#define SLOT_W CHIP_PITCH
#define SLOT_MAX ((DISPLAY_W - 16) / CHIP_PITCH)
static uint16_t *s_slot_bg[2];                    // [0 rest, 1 lifted][slot][row][col], big-endian
static int s_slot_key = -1;

static int slot_x(int x0, int k) { return x0 + k * CHIP_PITCH - (CHIP_PITCH - CHIP) / 2; }

static void slot_bg_build(int x0, int vis)
{
    int key = UI.texture << 8 | vis << 4 | (x0 & 15);
    if (key == s_slot_key) return;
    s_slot_key = key;
    for (int st = 0; st < 2; st++) {
        if (!s_slot_bg[st]) s_slot_bg[st] = heap_caps_malloc(SLOT_MAX * SLOT_W * STRIP_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (!s_slot_bg[st]) { ESP_LOGE("render", "no memory for the strip cache"); abort(); }
        const shadow_t *sh = st ? &s_chip_sh_sel : &s_chip_sh;
        for (int k = 0; k < vis; k++) {
            int cx = x0 + k * CHIP_PITCH, cy = CHIP_CY - CHIP / 2 - (st ? CHIP_LIFT : 0);
            int shy = cy + (st ? CHIP_LIFT + 2 : 2), sx = slot_x(x0, k);
            uint16_t *t = s_slot_bg[st] + k * SLOT_W * STRIP_H;
            for (int r = 0; r < STRIP_H; r++)
                for (int c = 0; c < SLOT_W; c++) {
                    int X = sx + c, Y = STRIP_Y0 + r;
                    t[r * SLOT_W + c] = be16(shade565(tex_px(X, Y), shadow_at(sh, cx, shy, X, Y)));
                }
        }
    }
}

// How far chip i is raised: the selected one CHIP_LIFT, unless motion animates it.
// Its shadow is the lifted one from half way up.
static int chip_lift(int i, int sel)
{
    if (s_lift_on && !UI.browsing && i < SLOTS) return s_lift[i];
    return i == sel ? CHIP_LIFT : 0;
}

static void draw_strip(void)
{
    strip_scroll_to(strip_sel());
    if (!s_chip_sh.a) {
        shadow_make(&s_chip_sh, CHIP, CHIP, CHIP_R, 2, 55);
        shadow_make(&s_chip_sh_sel, CHIP, CHIP, CHIP_R, 4, 75);
        chip_maps();
    }
    int vis = strip_visible(), sel = strip_sel();
    int x0 = (DISPLAY_W - vis * CHIP_PITCH) / 2 + (CHIP_PITCH - CHIP) / 2;
    slot_bg_build(x0, vis);
    // Composed in internal RAM (s_line, the name line's buffer) in slices of
    // STRIP_SUB rows, then blitted.
    for (int sb = 0; sb < STRIP_H; sb += STRIP_SUB) {
        int rows = STRIP_H - sb < STRIP_SUB ? STRIP_H - sb : STRIP_SUB;
        for (int y = 0; y < rows; y++) {
            uint16_t *row = s_line + y * DISPLAY_W;
            // Texture only outside the chip slots: the slots' cached backgrounds
            // cover the rest.
            int s0 = slot_x(x0, 0), s1 = slot_x(x0, vis - 1) + SLOT_W;
            if (s0 < 0) s0 = 0;
            if (s1 > DISPLAY_W) s1 = DISPLAY_W;
            if (s0 > 0) texture_row(row, 0, s0, STRIP_Y0 + sb + y);
            if (s1 < DISPLAY_W) texture_row(row + s1, s1, DISPLAY_W - s1, STRIP_Y0 + sb + y);
            for (int k = 0; k < vis; k++) {
                const uint16_t *t = s_slot_bg[chip_lift(s_first + k, sel) * 2 >= CHIP_LIFT] + (k * STRIP_H + sb + y) * SLOT_W;
                int sx = slot_x(x0, k), c0 = sx < 0 ? -sx : 0, c1 = sx + SLOT_W > DISPLAY_W ? DISPLAY_W - sx : SLOT_W;
                if (c1 > c0) memcpy(row + sx + c0, t + c0, (c1 - c0) * sizeof(uint16_t));
            }
        }
        // Unselected chips first: the lifted chip's shadow and ring go over them.
        for (int pass = 0; pass < 2; pass++)
            for (int k = 0; k < vis; k++) {
                int i = s_first + k;
                if ((i == sel) != (pass == 1)) continue;
                int x = x0 + k * CHIP_PITCH, y = CHIP_CY - CHIP / 2 - chip_lift(i, sel);
                strip_chip(STRIP_Y0 + sb, rows, x, y, strip_col(pal()->colors[strip_item(i)].rgb565), i == sel);
            }
        D->blit(0, STRIP_Y0 + sb, DISPLAY_W, rows, s_line);
    }
}
#else
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
#endif

// "S93 Peach ...... 13": code + name left, bead count right. The whole line is
// composed off-screen and sent in one blit, so it never shows half updated.
// A label wider than its space is shown shortened with "..."; after
// MARQUEE_DELAY_US unchanged it scrolls once right-to-left (one blit per frame),
// then shows the shortened form again and waits another MARQUEE_DELAY_US.
#define MARQUEE_DELAY_US 5000000
#define MARQUEE_FRAME_MS 40
#define MARQUEE_STEP     2          // px per frame (50 px/s)
#define MARQUEE_GAP      48         // px between the end and the repeated start

static char s_info_label[48];
static bool s_info_long;            // label wider than its space
static bool s_marquee;              // currently scrolling
static int s_marquee_off;
static int64_t s_info_since_us;     // label unchanged since

// Selected menu row marquee (e.g. "Preciosa Ornela (Czech beads)" in Brand): the row
// is drawn shortened, then after MARQUEE_DELAY_US scrolls one full pass and rests.
static char s_mq_text[56];          // the selected row's line being scrolled
static int s_mq_row = -1;
static bool s_mq_long;              // selected row's line is wider than its space
static bool s_mq_on;                // scrolling now
static int s_mq_off;
static int64_t s_mq_since_us;
static void menu_marquee_frame(void);   // menu section below

// PowerBook-style battery icon, 15 x 8 icon px (body 13 x 8 + terminal nub), 3
// segments, on a 1 px black margin, each icon px BATT_K x BATT_K screen px.
// Big-endian pixels into a row buffer; BATT_W/BATT_H are in screen px.
#define BATT_W (15 * BATT_K)
#define BATT_H (8 * BATT_K)
static void batt_px(uint16_t *buf, int stride, int x, int y, int xx, int yy, uint16_t v)
{
    for (int j = 0; j < BATT_K; j++)
        for (int i = 0; i < BATT_K; i++) buf[(y + yy * BATT_K + j) * stride + x + xx * BATT_K + i] = v;
}
static void draw_battery(uint16_t *buf, int stride, int x, int y, int pct)
{
    uint16_t ink = be16(COL_TEXT), bg = be16(COL_BG);
    for (int yy = -BATT_K; yy < BATT_H + BATT_K; yy++)
        for (int xx = -BATT_K; xx < BATT_W + BATT_K; xx++) buf[(y + yy) * stride + x + xx] = bg;
    for (int xx = 0; xx < 13; xx++) { batt_px(buf, stride, x, y, xx, 0, ink); batt_px(buf, stride, x, y, xx, 7, ink); }
    for (int yy = 0; yy < 8; yy++) { batt_px(buf, stride, x, y, 0, yy, ink); batt_px(buf, stride, x, y, 12, yy, ink); }
    for (int yy = 2; yy < 6; yy++) { batt_px(buf, stride, x, y, 13, yy, ink); batt_px(buf, stride, x, y, 14, yy, ink); }
    int segs = pct > 66 ? 3 : pct > 33 ? 2 : pct > 10 ? 1 : 0;
    for (int k = 0; k < segs; k++)
        for (int yy = 2; yy < 6; yy++)
            for (int xx = 0; xx < 2; xx++) batt_px(buf, stride, x, y, 2 + 3 * k + xx, yy, ink);
}

#if UI_HIRES
static int s_span[2][2];            // columns of the two texts in the last composed line
static void info_recolour(void);
#endif

static void compose_info(void)
{
    int idx = ui_color();
    char num[8];
    snprintf(num, sizeof num, "%d", count_color(idx));
    // Pass 1: render the glyphs as markers (NAME/COUNT ink, 0 = no ink) ...
    enum { NONE, NAME, COUNT };
    uint16_t *text = s_line + DISPLAY_W;          // text rows start 1 px down
    memset(s_line, 0, DISPLAY_W * LINE_H * sizeof(uint16_t));
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
#if UI_HIRES
    // ... pass 2 (mockup name_line): ink -> colour; next to ink -> 1 px black
    // outline; else board texture, darkened 60 % where the outlined text's drop
    // shadow (+1, +2) falls. Only the columns around the two texts need this
    // (spans); the rest of the line is plain texture.
    // s_mark: bit 0..1 = ink marker, bit 2 = within 1 px of ink (3x3 dilation, done
    // as a horizontal then a vertical pass).
    int name_end = s_marquee ? x1 : x0 + text_width(s_info_label);
    if (name_end > x1) name_end = x1;
    int (*span)[2] = s_span;
    span[0][0] = x0 - 1; span[0][1] = name_end + 2; span[1][0] = cx - 1; span[1][1] = cx + cw + 2;
    static uint8_t hrow[3][DISPLAY_W];
    for (int k = 0; k < 2; k++) {
        int a = span[k][0] < 0 ? 0 : span[k][0], b = span[k][1] > DISPLAY_W ? DISPLAY_W : span[k][1];
        span[k][0] = a; span[k][1] = b;
        for (int y = 0; y < LINE_H; y++)
            for (int x = a; x < b; x++) s_mark[y * DISPLAY_W + x] = (uint8_t)s_line[y * DISPLAY_W + x];
        for (int y = 0; y < LINE_H + 1; y++) {
            if (y < LINE_H) {                             // horizontal dilation of row y
                const uint8_t *m = s_mark + y * DISPLAY_W;
                uint8_t *h = hrow[y % 3];
                for (int x = a; x < b; x++)
                    h[x] = (m[x] & 3) || (x > a && (m[x - 1] & 3)) || (x + 1 < b && (m[x + 1] & 3));
            }
            int r = y - 1;                                // row r is complete now
            if (r < 0) continue;
            uint8_t *m = s_mark + r * DISPLAY_W;
            for (int x = a; x < b; x++)
                if (hrow[r % 3][x] || (r > 0 && hrow[(r - 1) % 3][x]) || (r + 1 < LINE_H && hrow[(r + 1) % 3][x]))
                    m[x] |= 4;
        }
    }
    info_recolour();
    if (batt) draw_battery(s_line, DISPLAY_W, bx, (LINE_H - BATT_H) / 2, battery_pct());
    D->blit(0, LINE_Y, DISPLAY_W, LINE_H, s_line);
}

// Pass 2 alone: paint the line from the ink markers in s_mark (kept from the last
// compose_info) with the current strip brightness. Motion's focus fade only changes
// the colours, so its frames skip the text rendering (~7 ms -> ~2 ms).
static void info_recolour(void)
{
    const int (*span)[2] = s_span;
    const uint16_t inkh[3] = { 0, be16(strip_col(COL_NAME)), be16(strip_col(COL_COUNT)) };
    for (int y = 0; y < LINE_H; y++) {
        uint16_t *row = s_line + y * DISPLAY_W;
        texture_row(row, 0, DISPLAY_W, LINE_Y + y);
        for (int k = 0; k < 2; k++)
            for (int x = span[k][0]; x < span[k][1]; x++) {
                int m = s_mark[y * DISPLAY_W + x];
                if (m & 3) { row[x] = inkh[m & 3]; continue; }
                if (m & 4) { row[x] = be16(COL_BG); continue; }       // outline
                if (x - 1 >= span[k][0] && y >= 2 && (s_mark[(y - 2) * DISPLAY_W + x - 1] & 4))
                    row[x] = be16(shade565(tex_px(x, LINE_Y + y), 153));
            }
    }
}

// Motion frame of the focus fade: recolour the composed line and send it.
static void info_fade_frame(void)
{
    info_recolour();
    if (battery_low()) {
        char num[8];
        snprintf(num, sizeof num, "%d", count_color(ui_color()));
        int bx = DISPLAY_W - TEXT_PAD - text_width(num) - 8 - BATT_W;
        draw_battery(s_line, DISPLAY_W, bx, (LINE_H - BATT_H) / 2, battery_pct());
    }
    D->blit(0, LINE_Y, DISPLAY_W, LINE_H, s_line);
}
#else
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
#endif

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
    if (UI.mode == MODE_MENU) {
        if (!s_mq_long) return -1;
        if (s_mq_on || input_touch_busy()) return MARQUEE_FRAME_MS;   // touching: keep the delay reset
        int64_t left = MARQUEE_DELAY_US - (esp_timer_get_time() - s_mq_since_us);
        return left <= 0 ? 1 : (int)(left / 1000) + 1;
    }
    if (UI.mode != MODE_DRAW || !s_info_long) return -1;
    if (s_marquee) return MARQUEE_FRAME_MS;
    int64_t left = MARQUEE_DELAY_US - (esp_timer_get_time() - s_info_since_us);
    return left <= 0 ? 1 : (int)(left / 1000) + 1;
}

void render_tick(void)
{
    if (UI.mode == MODE_MENU) { menu_marquee_frame(); return; }
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
// band buffer (s_strip) and sent in one blit. Switching screens replaces the old
// content band by band; the screen never goes blank in between.
// Native 450x600: 56 px rows, Chicago 3x, a System 7 title bar (racing stripes,
// close box, centred title), a rounded selection frame, submenu triangles and
// "..." on rows that open a dialog. The 240x320 Freenove keeps the plain layout.
#define ROW_H        MENU_ROW_H
#define MENU_VISIBLE MENU_VISIBLE_ROWS                // 9; menu.c VISIBLE_ROWS
_Static_assert(STRIP_H >= ROW_H, "menu bands reuse the strip buffer");
#define COL_TEXT_DIM 0xAD55
#if UI_HIRES
#define M_PAD   16          // title margins
#define M_X     22          // row text x (spec section 5)
#define M_SELX  6           // selection frame inset
#define M_SELY  2
#define M_SEL_R 6           // selection frame corner radius
#define M_GAP   12          // text to right-hand value
#define SW      40          // colour swatch
#else
#define M_PAD   8
#define M_X     12
#define M_SELX  3
#define M_SELY  1
#define M_GAP   6
#define SW      20
#endif
#define TEXT_OFF ((ROW_H - TEXT_H) / 2)               // 3 on 240x320, 8 on 450x600

static void band_text(int x, const char *str, int max_w, uint16_t fg)
{
    text_render(s_strip + TEXT_OFF * DISPLAY_W, DISPLAY_W, x, str, max_w, be16(fg), be16(COL_BG));
}

#if UI_HIRES
// System 7 title bar (spec section 5, on a full black screen): 2 on / 2 off stripes
// in #585858, the title centred in a 12 px black gap, the battery icon on the right,
// and a 1 px #808080 rule under the bar. (No close box: removed by the user,
// 2026-11-21.)
#define TB_STRIPE   0x5ACB      // #585858
#define TB_RULE     0x8410      // #808080
#define TB_SX       8           // stripes from x 8 to W - 8
#define TB_SY0      15          // stripe rows 15..40 (7 stripes), centred like the text
#define TB_SY1      41
#define TB_BOX_X    18          // battery icon inset from the right edge
#define TB_MARGIN   3           // black around the battery
#define TB_GAP      12          // black around the title
static void draw_title(const menu_view_t *v)
{
    strip_rect(0, 0, DISPLAY_W, ROW_H, COL_BG);
    for (int y = TB_SY0; y < TB_SY1; y += 4)
        strip_rect(TB_SX, y, DISPLAY_W - TB_SX, y + 2, TB_STRIPE);
    // Title: centred; pushed sideways if a long title would hit the battery icon.
    // The full title wins over the icon (e.g. "Scan with phone").
    const int left = TB_SX + TB_GAP;
    int pct = battery_pct();
    int tw = text_width(v->title);
    bool icon = pct >= 0;
    int right = DISPLAY_W - TB_SX;
    if (icon) {
        right = DISPLAY_W - TB_BOX_X - BATT_W - TB_MARGIN - TB_GAP;
        if (tw > right - left) { icon = false; right = DISPLAY_W - TB_SX; }
    }
    char fit[sizeof v->title + 4];
    text_fit(v->title, right - left, fit, sizeof fit);
    tw = text_width(fit);
    int tx = (DISPLAY_W - tw) / 2;
    if (tx < left) tx = left;
    if (tx + tw > right) tx = right - tw;
    strip_rect(tx - TB_GAP, 6, tx + tw + TB_GAP < DISPLAY_W ? tx + tw + TB_GAP : DISPLAY_W, ROW_H - 6, COL_BG);
    band_text(tx, fit, tw, COL_TEXT);
    if (icon) {
        const int ix = DISPLAY_W - TB_BOX_X - BATT_W, iy = (ROW_H - BATT_H) / 2;
        strip_rect(ix - TB_MARGIN, iy - TB_MARGIN, ix + BATT_W + TB_MARGIN, iy + BATT_H + TB_MARGIN, COL_BG);
        draw_battery(s_strip, DISPLAY_W, ix, iy, pct);
    }
    strip_rect(0, ROW_H - 1, DISPLAY_W, ROW_H, TB_RULE);
    D->blit(0, 0, DISPLAY_W, ROW_H, s_strip);
}
#else
static void draw_title(const menu_view_t *v)
{
    strip_rect(0, 0, DISPLAY_W, ROW_H, COL_BG);
    // Battery icon in menu titles, unless the title needs the room (e.g. the QR
    // screen's "Scan with phone"): the full title wins over the icon there.
    int pct = battery_pct();
    bool icon = pct >= 0 && text_width(v->title) <= DISPLAY_W - 2 * M_PAD - (BATT_W + 8);
    band_text(M_PAD, v->title, DISPLAY_W - 2 * M_PAD - (icon ? BATT_W + 8 : 0), COL_TEXT);
    if (icon) draw_battery(s_strip, DISPLAY_W, DISPLAY_W - M_PAD - BATT_W, (ROW_H - BATT_H) / 2 - 1, pct);
    strip_rect(M_PAD, ROW_H - 2, DISPLAY_W - M_PAD, ROW_H - 1, COL_DIVIDER);
    D->blit(0, 0, DISPLAY_W, ROW_H, s_strip);
}
#endif

// Rectangle in row coordinates, clipped to band b (rows are 1 or 2 bands tall).
static void row_rect(int b, int x0, int y0, int x1, int y1, uint16_t c)
{
    y0 -= b * ROW_H; y1 -= b * ROW_H;
    if (y0 < 0) y0 = 0;
    if (y1 > ROW_H) y1 = ROW_H;
    if (y0 < y1) strip_rect(x0, y0, x1, y1, c);
}

#if UI_HIRES
// 1 px ring of a rounded rectangle [x0, x1) x [y0, y1) with corner radius r (the
// mockups' ring_rrect), in row coordinates, clipped to band b.
static bool in_corner(int i, int j, int r)          // i, j: px in from the outer edges
{
    if (i >= r || j >= r) return true;
    int a = 2 * (r - i) - 1, c = 2 * (r - j) - 1;   // doubled distances to the centre
    return a * a + c * c <= 4 * r * r + 1;
}
static void row_ring(int b, int x0, int y0, int x1, int y1, int r, uint16_t col)
{
    const uint16_t v = be16(col);
    for (int yy = 0; yy < ROW_H; yy++) {
        int y = yy + b * ROW_H;
        if (y < y0 || y >= y1) continue;
        uint16_t *row = s_strip + yy * DISPLAY_W;
        int j = y - y0 < y1 - 1 - y ? y - y0 : y1 - 1 - y;
        if (j >= r) { row[x0] = row[x1 - 1] = v; continue; }
        for (int x = x0; x < x1; x++) {
            int i = x - x0 < x1 - 1 - x ? x - x0 : x1 - 1 - x;
            bool in = in_corner(i, j, r);
            bool inner = i >= 1 && j >= 1 && in_corner(i - 1, j - 1, r - 1);
            if (in && !inner) row[x] = v;
        }
    }
}

// System 7 hierarchical-menu triangle, 8 x 15, pointing right; (x, y) = top left.
#define TRI_W 8
#define TRI_H 15
static void band_tri(int x, int y, uint16_t col)
{
    for (int i = 0; i < TRI_W; i++) strip_rect(x + i, y + i, x + i + 1, y + TRI_H - i, col);
}
#endif

// Pattern thumbnail (saved slots, grid sizes): a 9x9 pattern at 5 px per bead (45
// px; 10 px = 90 px on 450x600), larger grids at the most whole px per bead that
// fit the same area, centred; a 1 px black gap and a 1 px white border (dim and
// empty for an empty slot), centred in the double-height row.
#define THUMB_PX   (UI_HIRES ? 10 : 5)
#define THUMB_X    M_X
#define THUMB_AREA (GRID_MIN * THUMB_PX)
#define THUMB_BOX  (THUMB_AREA + 4)                   // with gap + border: 49
static void draw_slot_thumb(int b, int rh, const menu_row_t *r)
{
    const int x0 = THUMB_X, y0 = (rh - THUMB_BOX) / 2;
    row_rect(b, x0, y0, x0 + THUMB_BOX, y0 + THUMB_BOX, r->thumb ? COL_FRAME : COL_DIVIDER);
    row_rect(b, x0 + 1, y0 + 1, x0 + THUMB_BOX - 1, y0 + THUMB_BOX - 1, COL_BG);
    if (!r->thumb) return;
    const int n = r->thumb_n, k = THUMB_AREA / n, o = (THUMB_AREA - n * k) / 2;
    const int tx = x0 + 2 + o, ty = y0 + 2 + o;
    const bead_color_t *pc = PALETTES[r->thumb_pal].colors;
    for (int cy = 0; cy < n; cy++)
        for (int cx = 0; cx < n; cx++) {
            uint8_t c = r->thumb[cy * n + cx];
            row_rect(b, tx + cx * k, ty + cy * k, tx + (cx + 1) * k,
                     ty + (cy + 1) * k, c == CELL_EMPTY ? 0x2104 : pc[c].rgb565);
        }
}

// Position bar of a touch-scrolled list (Browse colours): a dim #585858 thumb in the
// strip right of the selection frame, over the list area, no track and not touchable.
// Its length is the visible share of the list (at least BAR_MIN); it reaching the
// end of its travel is the only end-of-list cue (no bounce, no flash).
#define BAR_X    (DISPLAY_W - M_SELX + 1)
#define BAR_W    (M_SELX - 2 > 1 ? M_SELX - 2 : 1)   // 4 px on 450x600, 1 px on 240x320
#define BAR_MIN  (UI_HIRES ? 24 : 12)
#define BAR_COL  0x5ACB
static void band_bar(const menu_view_t *v, int band_y)
{
    if (!v->bar) return;
    const int y0 = ROW_H, len = MENU_VISIBLE * ROW_H, max_top = v->count - MENU_VISIBLE;
    int h = len * MENU_VISIBLE / v->count;
    if (h < BAR_MIN) h = BAR_MIN;
    int top = v->top < 0 ? 0 : v->top > max_top ? max_top : v->top;
    int ty = y0 + (len - h) * top / max_top;
    int a = ty - band_y, b = ty + h - band_y;
    if (a < 0) a = 0;
    if (b > ROW_H) b = ROW_H;
    if (a < b) strip_rect(BAR_X, a, BAR_X + BAR_W, b, BAR_COL);
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
#if UI_HIRES
    // Compose straight into the shadow framebuffer (same stride and byte order as the
    // strip): no band copy, and the band stays in the data cache while it is drawn.
    uint16_t *const fb = board_fb_draw(), *const strip = s_strip;
#endif
    for (int b = 0; b < units; b++) {
#if UI_HIRES
        if (fb) { board_fb_lock(true); s_strip = fb + (y + b * ROW_H) * DISPLAY_W; }
#endif
        strip_rect(0, 0, DISPLAY_W, ROW_H, COL_BG);
#if UI_HIRES
        if (s_ring_y0 >= 0) {                    // motion: the frame sliding between rows
            if (s_ring_y1 > y && s_ring_y0 < y + rh)
                row_ring(b, M_SELX, s_ring_y0 - y, DISPLAY_W - M_SELX, s_ring_y1 - y, M_SEL_R, COL_FRAME);
        } else if (i == v->sel) {                // 1 px white frame, like the cursor
            row_ring(b, M_SELX, M_SELY, DISPLAY_W - M_SELX, rh - M_SELY, M_SEL_R, COL_FRAME);
        }
#else
        if (i == v->sel) {                       // 1 px white frame, like the cursor
            row_rect(b, M_SELX, M_SELY, DISPLAY_W - M_SELX, rh - M_SELY, COL_FRAME);
            row_rect(b, M_SELX + 1, M_SELY + 1, DISPLAY_W - M_SELX - 1, rh - M_SELY - 1, COL_BG);
        }
#endif
        int x = M_X;
        if (r.slot) {
            draw_slot_thumb(b, rh, &r);
            x = THUMB_X + THUMB_BOX + M_X;
        } else if (r.swatch >= 0) {
            uint16_t c = (uint16_t)r.swatch;
            int luma = ((c >> 11) * 8 * 3 + ((c >> 5) & 0x3F) * 4 * 6 + (c & 0x1F) * 8) / 10;
            const int sx = M_X - 2, sy = (ROW_H - SW) / 2;
            if (luma < 48) strip_rect(sx - 1, sy - 1, sx + SW + 1, sy + SW + 1, COL_DIVIDER);
            strip_rect(sx, sy, sx + SW, sy + SW, c);
            x = sx + SW + (UI_HIRES ? 16 : 8);
        }
        // Text: first band carries the main line (+ right text), second band `sub`.
        const char *line = b == 0 ? r.left : r.sub;
        const char *right = b == 0 ? r.right : "";
        int rw = text_width(right);
        int re = DISPLAY_W - M_X;                            // right edge of the value
#if UI_HIRES
        char dots[sizeof r.left + 4];
        if (b == 0 && r.kind == ROW_SUB) {
            band_tri(re - TRI_W, (ROW_H - TRI_H) / 2, COL_TEXT_DIM);
            re -= TRI_W + 16;
        } else if (b == 0 && r.kind == ROW_DIALOG) {         // Mac rule: "Save..."
            snprintf(dots, sizeof dots, "%s...", r.left);
            line = dots;
        }
#endif
        int rx = re - rw;
        const int avail = rx - x - M_GAP;
        // The selected row's main line, if too wide, scrolls like the grid's name line
        // (same delay, speed and gap); otherwise long names end in "...".
        bool mq = b == 0 && i == v->sel && text_width(line) > avail;
        if (mq && (strcmp(line, s_mq_text) != 0 || s_mq_row != i)) {   // new text: rest first
            snprintf(s_mq_text, sizeof s_mq_text, "%s", line);
            s_mq_row = i; s_mq_on = false; s_mq_off = 0;
            s_mq_since_us = esp_timer_get_time();
        }
        if (b == 0 && i == v->sel) { s_mq_long = mq; if (!mq) s_mq_row = -1; }
        if (mq && s_mq_on) {
            uint16_t *tb = s_strip + TEXT_OFF * DISPLAY_W;
            int tw = text_width(line);
            text_render_clip(tb, DISPLAY_W, x - s_mq_off, x, x + avail, line, be16(COL_TEXT), be16(COL_BG));
            text_render_clip(tb, DISPLAY_W, x - s_mq_off + tw + MARQUEE_GAP, x, x + avail, line,
                             be16(COL_TEXT), be16(COL_BG));
        } else {
            char fit[52];
            text_fit(line, avail, fit, sizeof fit);
            band_text(x, fit, avail, b == 0 ? COL_TEXT : COL_TEXT_DIM);
        }
        if (rw) band_text(rx, right, rw, COL_TEXT_DIM);
        band_bar(v, y + b * ROW_H);
#if UI_HIRES
        if (fb) {
            board_fb_dirty(0, y + b * ROW_H, DISPLAY_W, ROW_H);
            board_fb_lock(false);
            s_strip = strip;
            continue;
        }
#endif
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
    s_mq_long = false; s_mq_row = -1;                    // selected row restarts its delay
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

#if UI_HIRES
static bool motion_hilite(const menu_view_t *old, const menu_view_t *now);
static bool motion_fade_held(void);
#endif

// Marquee frame for the selected menu row: rest, then one pass, then rest again. Only
// when the screen is settled (no pending change, no sliding selection frame).
static void menu_marquee_frame(void)
{
    if (!s_mq_long) return;
    menu_view_t v;
    menu_view(&v);
    if (v.qr || v.version != s_menu_shown.version || v.top != s_menu_shown.top ||
        v.count != s_menu_shown.count || v.sel != s_menu_shown.sel || v.sel != s_mq_row) return;
#if UI_HIRES
    if (s_ring_y0 >= 0) return;
#endif
    int64_t now = esp_timer_get_time();
    if (input_touch_busy()) {            // finger down or coasting: rest; 5 s from the lift
        s_mq_since_us = now;
        if (s_mq_on) { s_mq_on = false; s_mq_off = 0; draw_menu_row(&v, v.sel); }
        return;
    }
    if (!s_mq_on) {
        if (now - s_mq_since_us < MARQUEE_DELAY_US) return;
        s_mq_on = true;
        s_mq_off = 0;
    }
    s_mq_off += MARQUEE_STEP;
    if (s_mq_off >= text_width(s_mq_text) + MARQUEE_GAP) {
        s_mq_on = false;                                 // one full pass: rest again
        s_mq_off = 0;
        s_mq_since_us = now;
    }
    draw_menu_row(&v, v.sel);
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
#if UI_HIRES
        if (!motion_hilite(&s_menu_shown, &v))
#endif
        {
            draw_menu_row(&v, s_menu_shown.sel);
            draw_menu_row(&v, v.sel);
        }
    }
    s_menu_shown = v;
}

// ---- screen state -------------------------------------------------------------

// What is on screen now, so render_update() can redraw only what changed.
typedef struct {
    ui_mode_t mode;
    int x, y;
    bool browsing, palette_focus, bevel;
    int slot, pos, color, count, board, texture, n;
    uint8_t slots[SLOTS];
    uint8_t cell;                       // the cell under the cursor
} shown_t;
static shown_t s_shown;

static void snapshot(shown_t *v)
{
    memset(v, 0, sizeof *v);            // padding too, so memcmp is meaningful
    v->mode = UI.mode; v->x = UI.cur_x; v->y = UI.cur_y;
    v->browsing = UI.browsing; v->slot = UI.slot; v->pos = UI.browse_pos;
    v->palette_focus = palette_focus(); v->bevel = UI.bevel;
    v->board = UI.board; v->texture = UI.texture; v->n = UI.n;
    v->color = ui_color(); v->count = count_color(v->color);
    memcpy(v->slots, UI.slots, SLOTS);
    v->cell = UI.cells[UI.cur_y * UI.n + UI.cur_x];
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

#if UI_HIRES
#include "render_motion.inc"
#else
void render_anim_finish(void) {}
int  render_anim_ms(void) { return -1; }
void render_anim_frame(void) {}
void render_event(event_t ev) { (void)ev; }
#endif

void render_full(void)
{
#if UI_HIRES
    anim_cancel();                      // everything gets drawn: no animation left over
#endif
#if CONFIG_BEAD_PERF_LOG
    ESP_LOGW("perf", "FULL REDRAW");
#endif
    static int once = 0;
    if (!once) {
        D = board_display(); D->init(); once = 1;
#if UI_HIRES
        // Band buffers are too big for internal RAM at 450 px wide: PSRAM (blit
        // only copies them into the shadow framebuffer, also in PSRAM).
        s_strip = heap_caps_malloc(DISPLAY_W * STRIP_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        // The name line (and the strip, in slices) is composed in internal RAM:
        // per-pixel blending in PSRAM is several times slower (58 KB in total).
        s_line = heap_caps_malloc(DISPLAY_W * LINE_H * sizeof(uint16_t), MALLOC_CAP_INTERNAL);
        s_mark = heap_caps_malloc(DISPLAY_W * LINE_H, MALLOC_CAP_INTERNAL);
        s_clean = heap_caps_malloc(DISPLAY_W * DISPLAY_W * sizeof(uint16_t), MALLOC_CAP_SPIRAM);   // 405 KB; NULL = slow glide
        if (!s_strip || !s_line || !s_mark) { ESP_LOGE("render", "no memory for the band buffers"); abort(); }
#endif
    }
    if (grid_mode(UI.mode)) {
        // No full-screen clear (it would flash): cell tiles, strip and info line
        // each overwrite their area in one blit; only the margins between them
        // are filled, straight to their final content (the board texture).
        layout();                                   // gap depends on the variant
        const int h = s_gap / 2, pitch = s_cell + s_gap;
        const int gx0 = s_ox - h, gx1 = gx0 + UI.n * pitch;
        const int gy0 = s_oy - h, gy1 = gy0 + UI.n * pitch;
        const int sy0 = STRIP_Y0, sy1 = sy0 + STRIP_H;
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
        UI.texture != s_shown.texture || UI.bevel != s_shown.bevel || UI.n != s_shown.n) {
        render_full();
        return;
    }
    if (!grid_mode(UI.mode)) { render_menu_update(); return; }
    shown_t now;
    snapshot(&now);
#if UI_HIRES
    motion_draw_start(&s_shown, &now);  // overrides for the first frame, drawn below
#endif

    // Grid: old cursor cell loses its frame; the cursor cell is redrawn when it
    // moved, its bead changed (place/erase) or its frame's brightness did (focus).
    bool moved = now.x != s_shown.x || now.y != s_shown.y;
    if (moved) draw_cell(s_shown.x, s_shown.y, COL_BG);
    bool cell = moved || now.cell != s_shown.cell || now.palette_focus != s_shown.palette_focus;
#if UI_HIRES
    if (cell && !moved && now.cell == s_shown.cell && s_acell < 0) {   // only the ring's brightness
        const int h = s_gap / 2, pitch = s_cell + s_gap;
        int X0 = s_ox + UI.cur_x * pitch - h, Y0 = s_oy + UI.cur_y * pitch - h;
        grid_rect_ex(X0 - HALO_M, Y0 - HALO_M, X0 + pitch + HALO_M, Y0 + pitch + HALO_M, true);
    } else
#endif
    if (cell) draw_cell(UI.cur_x, UI.cur_y, cursor_frame());
    bool strip = now.browsing != s_shown.browsing || now.palette_focus != s_shown.palette_focus ||
                 now.slot != s_shown.slot ||
                 now.pos != s_shown.pos || memcmp(now.slots, s_shown.slots, SLOTS) != 0;
    bool info = now.palette_focus != s_shown.palette_focus || now.color != s_shown.color ||
                now.count != s_shown.count;
#if UI_HIRES
    // Motion holding the strip's old brightness until a glide ends: a change of
    // focus alone leaves the strip and the name line looking the same for now.
    if (s_lvl >= 0 && motion_fade_held()) {
        bool focus_only = now.browsing == s_shown.browsing && now.slot == s_shown.slot &&
                          now.pos == s_shown.pos && !memcmp(now.slots, s_shown.slots, SLOTS);
        if (focus_only) strip = false;
        if (now.color == s_shown.color && now.count == s_shown.count) info = false;
    }
    // Send the cursor area on its own: with the strip below it, one dirty rectangle
    // would span most of the screen and hold up the next animation frame.
    if ((cell || moved) && (strip || info) && D->present) D->present();
#endif

    // Strip: any change of list, selection, slot colours or focus -> one blit.
    if (strip) draw_strip();
#if UI_HIRES
    if (info && now.color == s_shown.color && now.count == s_shown.count) info_fade_frame();   // focus only: recolour
    else
#endif
    if (info) draw_info();
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
