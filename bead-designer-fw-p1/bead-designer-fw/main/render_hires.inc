// Included by render.c when UI_HIRES (native 450x600): the "Soft bead" art.
// Ported from the mockups (docs/ui-update-2.41/scripts/screens.py and gfx.py), which
// the user chose on 2026-11-18 (DECISIONS.md): option A, rounded corners, 2-px
// texture, cursor = 1 px black + 2 px white ring with a soft dark halo.
//
// Grid drawing works on screen rectangles: grid_rect() composes every pixel of a
// rectangle (texture, each overlapping cell's shadow and bead, then the cursor's
// halo and ring) and sends it in one blit. A cell is redrawn as its own tile, or,
// around the cursor, as the tile plus the halo margin, so the halo spilling onto
// the neighbouring cells appears and disappears cleanly.

// ---- colour helpers (8-bit maths, like the mockups) ---------------------------------
typedef struct { uint8_t r, g, b; } rgb_t;

static inline rgb_t from565(uint16_t c)
{
    int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    return (rgb_t){ (uint8_t)(r << 3 | r >> 2), (uint8_t)(g << 2 | g >> 4), (uint8_t)(b << 3 | b >> 2) };
}

static inline uint16_t to565(rgb_t c)
{
    return (uint16_t)(((c.r * 31 + 127) / 255) << 11 | ((c.g * 63 + 127) / 255) << 5 | ((c.b * 31 + 127) / 255));
}

static inline uint8_t mix8(int c, int t, int pct)       // c + (t - c) * pct / 100, rounded
{
    int d = (t - c) * pct;
    return (uint8_t)(c + (d >= 0 ? (d + 50) / 100 : -((-d + 50) / 100)));
}
static inline rgb_t lighten(rgb_t c, int pct) { return (rgb_t){ mix8(c.r, 255, pct), mix8(c.g, 255, pct), mix8(c.b, 255, pct) }; }
static inline rgb_t darken(rgb_t c, int pct)  { return (rgb_t){ mix8(c.r, 0, pct), mix8(c.g, 0, pct), mix8(c.b, 0, pct) }; }

// Blend black over an RGB565 colour with alpha a (0..255), on the 565 channels.
static inline uint16_t shade565(uint16_t c, int a)
{
    if (a <= 0) return c;
    unsigned k = 256 - (unsigned)a - (a > 127);           // 255 - a, scaled to /256
    unsigned r = ((c >> 11) * k + 128) >> 8, g = (((c >> 5) & 0x3F) * k + 128) >> 8, b = ((c & 0x1F) * k + 128) >> 8;
    return (uint16_t)(r << 11 | g << 5 | b);
}

// The board texture repeats every 16 px across (2-px dots, 8-wide table): one row
// period is cached per texture and screen row parity is folded in by board_px.
static uint16_t s_tex[16][16];                            // [y & 15][x & 15], native order
static int s_tex_id = -1;
static inline void tex_cache(void)
{
    if (s_tex_id == UI.texture) return;
    s_tex_id = UI.texture;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) s_tex[y][x] = board_px(x, y);
}
static inline uint16_t tex_px(int x, int y) { tex_cache(); return s_tex[y & 15][x & 15]; }
// Fill dst[0..w) (big-endian) with texture row y from screen column x0.
static void texture_row(uint16_t *dst, int x0, int w, int y)
{
    tex_cache();
    const uint16_t *t = s_tex[y & 15];
    for (int i = 0; i < w; i++) dst[i] = be16(t[(x0 + i) & 15]);
}

// ---- shapes -----------------------------------------------------------------------------
// i, j = distance into a corner square from its outer edges (gfx.corner_in, x4 integer).
static inline bool corner_in(int i, int j, int r)
{
    if (r <= 0 || i >= r || j >= r) return true;
    int a = 2 * r - 2 * i - 1, b = 2 * r - 2 * j - 1;
    return a * a + b * b <= 4 * r * r + 1;
}

static inline bool in_rrect(int x, int y, int x0, int y0, int w, int h, int r)
{
    if (x < x0 || y < y0 || x >= x0 + w || y >= y0 + h) return false;
    int i = x - x0 < x0 + w - 1 - x ? x - x0 : x0 + w - 1 - x;
    int j = y - y0 < y0 + h - 1 - y ? y - y0 : y0 + h - 1 - y;
    return corner_in(i, j, r);
}

// Soft shadow of a w x h rounded rect: box blur (radius blur, 2 passes) of its mask,
// times alpha, as 0..255. The map is (w + 2 pad) square-ish; map (pad, pad) = rect (0, 0).
typedef struct { uint8_t *a; int w, h, pad; } shadow_t;

static void shadow_make(shadow_t *s, int w, int h, int r, int blur, int alpha_pct)
{
    int pad = blur * 3 + 2, W = w + 2 * pad, H = h + 2 * pad;
    float *m = calloc((size_t)W * H, sizeof(float)), *t = calloc((size_t)W * H, sizeof(float));
    s->a = calloc((size_t)W * H, 1);
    s->w = W; s->h = H; s->pad = pad;
    if (!m || !t || !s->a) { free(m); free(t); return; }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (in_rrect(x, y, 0, 0, w, h, r)) m[(y + pad) * W + x + pad] = 1.0f;
    for (int pass = 0; blur > 0 && pass < 2; pass++) {
        for (int y = 0; y < H; y++)                       // horizontal
            for (int x = 0; x < W; x++) {
                float sum = 0;
                for (int k = x - blur; k <= x + blur; k++) if (k >= 0 && k < W) sum += m[y * W + k];
                t[y * W + x] = sum / (2 * blur + 1);
            }
        for (int y = 0; y < H; y++)                       // vertical
            for (int x = 0; x < W; x++) {
                float sum = 0;
                for (int k = y - blur; k <= y + blur; k++) if (k >= 0 && k < H) sum += t[k * W + x];
                m[y * W + x] = sum / (2 * blur + 1);
            }
    }
    for (int i = 0; i < W * H; i++) {
        float v = m[i] * alpha_pct * 2.55f;
        s->a[i] = (uint8_t)(v > 255 ? 255 : v + 0.5f);
    }
    free(m); free(t);
}

// Alpha of shadow s for a rect whose top-left is at (rx, ry), at screen (X, Y).
static inline int shadow_at(const shadow_t *s, int rx, int ry, int X, int Y)
{
    int u = X - rx + s->pad, v = Y - ry + s->pad;
    if (!s->a || u < 0 || v < 0 || u >= s->w || v >= s->h) return 0;
    return s->a[v * s->w + u];
}

// Speed: the shading of a bead depends only on its size, so it is computed once per
// size as a class map (which of 9 tones each pixel gets); drawing a bead then only
// needs its 9 tones (bead_tones) and a lookup per pixel.
enum { BT_OUT, BT_BASE, BT_D20, BT_D11, BT_L20, BT_L11, BT_S16, BT_S09, BT_S04, BT__N };

static void bead_classes(uint8_t *map, int n, int r)
{
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            uint8_t k = BT_BASE;
            if (!in_rrect(x, y, 0, 0, n, n, r)) k = BT_OUT;
            else {
                int db = n - 1 - x < n - 1 - y ? n - 1 - x : n - 1 - y, dt = x < y ? x : y;
                if (db < 2 && db <= dt) k = db == 0 ? BT_D20 : BT_D11;
                else if (dt < 2) k = dt == 0 ? BT_L20 : BT_L11;
                else {
                    int s100 = (x + y) * 10000, lim = 62 * n;
                    if (s100 < lim * 45) k = BT_S16;
                    else if (s100 < lim * 75) k = BT_S09;
                    else if (s100 < lim * 100) k = BT_S04;
                }
            }
            map[y * n + x] = k;
        }
}

// The 9 tones (RGB565) of a bead of colour c; flat beads (bevel off) use c throughout.
static void bead_tones(uint16_t c565, bool bevel, uint16_t t[BT__N])
{
    rgb_t c = from565(c565);
    for (int k = 0; k < BT__N; k++) t[k] = c565;
    if (!bevel) return;
    t[BT_D20] = to565(darken(c, 20));  t[BT_D11] = to565(darken(c, 11));
    t[BT_L20] = to565(lighten(c, 20)); t[BT_L11] = to565(lighten(c, 11));
    t[BT_S16] = to565(lighten(c, 16)); t[BT_S09] = to565(lighten(c, 9)); t[BT_S04] = to565(lighten(c, 4));
}

// ---- grid geometry by cell size (spec section 2) ----------------------------------------
static int s_bead_r, s_peg, s_sh_dx, s_sh_dy;
static shadow_t s_bead_sh, s_halo_sh;
static int s_sh_cell = -1;
static uint8_t s_cell_cls[TILE_MAX * TILE_MAX];          // bead_classes() for s_cell

static void hires_layout(void)
{
    if (s_sh_cell == s_cell) return;
    s_sh_cell = s_cell;
    s_bead_r = s_cell >= 40 ? 3 : 2;
    s_peg = (s_cell * 19 + 50) / 100;                     // 43 -> 8, 32 -> 6, 25 -> 5
    int blur = s_cell >= 28 ? 1 : 0;
    s_sh_dx = s_cell >= 28 ? 2 : 1; s_sh_dy = 2;
    free(s_bead_sh.a); free(s_halo_sh.a);
    shadow_make(&s_bead_sh, s_cell, s_cell, s_bead_r, blur, 85);
    // Cursor halo: the ring's outer square (cell + 6) with r 3, 1 px down, blur 2, 60 %.
    shadow_make(&s_halo_sh, s_cell + 6, s_cell + 6, 3, 2, 60);
    bead_classes(s_cell_cls, s_cell, s_bead_r);
}

#define HALO_M 12                                         // halo reach beyond the cell
#define REG_MAX ((TILE_MAX + 2 * HALO_M) * (TILE_MAX + 2 * HALO_M))
static uint16_t s_reg[REG_MAX];

// An empty cell's pixel (cx, cy) at screen (X, Y); k = its bead class; peg = show
// the peg (the bead-drop animation hides it at once).
static uint16_t empty_px(int X, int Y, int cx, int cy, int k, bool peg)
{
    const int n = s_cell, c0 = (n - s_peg) / 2;
    peg = peg && cx >= c0 && cx < c0 + s_peg && cy >= c0 && cy < c0 + s_peg;
    if (board_open()) return peg ? RGB565(0x80, 0x80, 0x80) : tex_px(X, Y);
    if (k == BT_OUT) return tex_px(X, Y);
    if (peg && UI.board != BOARD_SOLID) return RGB565(0x78, 0x78, 0x78);   // Solid: no peg (user, 2026-11-21)
    return ((X + Y) & 1) ? RGB565(0x58, 0x58, 0x58) : RGB565(0x50, 0x50, 0x50);
}

// Motion: the cell s_acell shows a bead of colour s_aidx at s_am px (centred, may
// overshoot the cell), over the empty cell, its contact shadow at s_ash / 256.
static int s_am, s_ash, s_am_cls = -1;
static bool s_apeg;
static uint16_t s_atones[BT__N];
#define ACLS_MAX (TILE_MAX + 8)
static uint8_t s_acls[ACLS_MAX * ACLS_MAX];
static uint16_t anim_cell_px(int px, int py, int X, int Y, int cx, int cy, int k)
{
    const int m = s_am < ACLS_MAX ? s_am : ACLS_MAX;
    if (m != s_am_cls && m > 0) { bead_classes(s_acls, m, s_bead_r); s_am_cls = m; }
    const int off = (s_cell - m) >> 1;                    // floor, also when m > s_cell
    const int bx = cx - off, by = cy - off;
    if (m > 0 && bx >= 0 && by >= 0 && bx < m && by < m && s_acls[by * m + bx] != BT_OUT)
        return s_atones[s_acls[by * m + bx]];
    uint16_t v = empty_px(X, Y, cx, cy, k, s_apeg);
    if (k == BT_OUT && s_ash > 0)
        v = shade565(v, shadow_at(&s_bead_sh, px + s_sh_dx, py + s_sh_dy, X, Y) * s_ash >> 8);
    return v;
}

// Cell (gx, gy)'s pixel at screen (X, Y), which lies inside its tile.
static int s_tone_cell = -1;                              // cell whose tones are in s_tones
static uint16_t s_tones[BT__N];
static uint16_t cell_px(int gx, int gy, int X, int Y)
{
    const int n = s_cell, pitch = n + s_gap;
    const int px = s_ox + gx * pitch, py = s_oy + gy * pitch;
    const int cx = X - px, cy = Y - py;
    uint8_t idx = UI.cells[gy * UI.n + gx];
    bool empty = idx == CELL_EMPTY || idx >= pal()->count;
    // The tile includes the cell's half-gaps: those are outside the bead.
    const int k = cx >= 0 && cy >= 0 && cx < n && cy < n ? s_cell_cls[cy * n + cx] : BT_OUT;
    if (gy * GRID_MAX + gx == s_acell) return anim_cell_px(px, py, X, Y, cx, cy, k);
    if (empty) return empty_px(X, Y, cx, cy, k, true);
    if (k != BT_OUT) {
        int key = (((gy * GRID_MAX + gx) << 8 | idx) << 1 | UI.bevel) << 4 | UI.palette;
        if (key != s_tone_cell) { bead_tones(pal()->colors[idx].rgb565, UI.bevel, s_tones); s_tone_cell = key; }
        return s_tones[k];
    }
    // Outside the bead: texture, darkened by the bead's own contact shadow.
    return shade565(tex_px(X, Y), shadow_at(&s_bead_sh, px + s_sh_dx, py + s_sh_dy, X, Y));
}

// The board without the cursor (screen coordinates, DISPLAY_W square, native byte
// order, PSRAM): every full grid_rect stores its pixels here, so a cursor-only
// redraw (motion's glide frames) just lays the ring and halo over these instead of
// rendering the beads again (~11 ms -> ~4 ms a glide frame).
static uint16_t *s_clean;

// Compose and send screen rectangle [X0, X1) x [Y0, Y1) of the grid area. With
// cursor_only, the beads come from s_clean (nothing but the cursor has moved since
// these pixels were last drawn).
static void grid_rect_ex(int X0, int Y0, int X1, int Y1, bool cursor_only)
{
    const int h = s_gap / 2, pitch = s_cell + s_gap;
    const int gx0 = s_ox - h, gy0 = s_oy - h;
    if (X0 < 0) X0 = 0;
    if (Y0 < 0) Y0 = 0;
    if (X1 > DISPLAY_W) X1 = DISPLAY_W;
    if (Y1 > DISPLAY_H) Y1 = DISPLAY_H;
    const int w = X1 - X0, hh = Y1 - Y0;
    if (w <= 0 || hh <= 0 || w * hh > REG_MAX) return;
    // Cursor ring box (outer edge of the 2 px white ring) and its colour.
    int cpx = s_ox + UI.cur_x * pitch, cpy = s_oy + UI.cur_y * pitch;
    if (s_cur_px >= 0) { cpx = s_cur_px; cpy = s_cur_py; }          // motion: gliding
    const uint16_t ring = cursor_frame();
    const int gx1 = gx0 + UI.n * pitch, gy1 = gy0 + UI.n * pitch;
    if (!s_clean) cursor_only = false;
    for (int Y = Y0; Y < Y1; Y++) {
        uint16_t *row = s_reg + (Y - Y0) * w;
        int ty = (Y - gy0) / pitch;
        bool iny = Y >= gy0 && ty < UI.n && Y < DISPLAY_W;
        uint16_t *clean = s_clean ? s_clean + Y * DISPLAY_W : NULL;
        for (int X = X0; X < X1; X++) {
            bool in = iny && X >= gx0 && X < gx1;
            uint16_t v;
            if (!in) v = tex_px(X, Y);
            else if (cursor_only) v = clean[X];
            else {
                v = cell_px((X - gx0) / pitch, ty, X, Y);
                if (clean) clean[X] = v;
            }
            // Cursor: halo outside the ring box, then 1 px black + 2 px white rings.
            int dx = X < cpx ? cpx - X : X - (cpx + s_cell - 1);   // > 0: outside the cell
            int dy = Y < cpy ? cpy - Y : Y - (cpy + s_cell - 1);
            int d = dx > dy ? dx : dy;
            if (d > 3) v = shade565(v, shadow_at(&s_halo_sh, cpx - 3, cpy - 3 + 1, X, Y));
            else if (d == 1) v = COL_BG;
            else if (d >= 2) v = ring;
            row[X - X0] = be16(v);
        }
    }
    D->blit(X0, Y0, w, hh, s_reg);
    (void)gy1;
}
static void grid_rect(int X0, int Y0, int X1, int Y1) { grid_rect_ex(X0, Y0, X1, Y1, false); }

// Redraw cell (x, y): its tile, plus the halo margin when the cursor is or was there
// (callers pass the old cursor cell to clear its halo). `frame` is unused: the
// cursor is drawn wherever it is now.
static void draw_cell(int x, int y, uint16_t frame)
{
    (void)frame;
    hires_layout();
    const int h = s_gap / 2, pitch = s_cell + s_gap;
    int X0 = s_ox + x * pitch - h, Y0 = s_oy + y * pitch - h;
    grid_rect(X0 - HALO_M, Y0 - HALO_M, X0 + pitch + HALO_M, Y0 + pitch + HALO_M);
}
