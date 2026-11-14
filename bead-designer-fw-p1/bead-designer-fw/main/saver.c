// Screen saver: a halftone image made of bead-coloured dots of mixed sizes.
//
// Each cycle (SAVER_CYCLE_MS) picks
//   a shape   gradient field (waves, zigzag, rings, tunnel, stripes, fade, glow),
//             sampled on an 8 px lattice: dense = big dot, light = small or none
//   an origin a side, corner or the centre: where new dots come from (device tilt
//             would choose it via saver_tilt(); without a sensor it is random)
//   a motion  gravity pour, spring snap, swirl or drift
//   a colour  one bead colour used in the pattern (else the shortlist)
// and the dots of the previous image break up and flow into the new one.
//
// Drawing: no framebuffer. The screen is split into horizontal bands; a band is
// recomposed and blitted only if a dot in it moved, so a settled image costs nothing.
#include "saver.h"
#include "ui.h"
#include "board.h"
#include "palettes.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

static const char *TAG = "saver";

const uint16_t SAVER_TIMEOUT_S[SAVER_CHOICES] = { 0, 5, 30, 60, 300 };
const char *const SAVER_NAMES[SAVER_CHOICES] = { "Off", "5s", "30s", "1m", "5m" };

__attribute__((weak)) bool saver_tilt(float *gx, float *gy) { (void)gx; (void)gy; return false; }

#define W DISPLAY_W
#define H DISPLAY_H
#define PITCH 8                       // lattice step = largest dot
#define COLS (W / PITCH)
#define ROWS (H / PITCH)
#define NP (COLS * ROWS)
#define BAND_H 32
#define NBANDS ((H + BAND_H - 1) / BAND_H)
#define FRAME_MS 40                   // 25 fps target
#define TAU 6.2831853f

// ---- random -----------------------------------------------------------------------
static uint32_t s_rng = 1;
static uint32_t rnd32(void) { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
static float rnd(void) { return (rnd32() >> 8) * (1.0f / 16777216.0f); }   // [0, 1)
static int rndi(int n) { return (int)(rnd32() % (uint32_t)n); }
static float rnds(float a) { return (rnd() * 2 - 1) * a; }                   // [-a, a)

// ---- particles ----------------------------------------------------------------------
enum { ST_OFF, ST_WAIT, ST_MOVE, ST_FALL, ST_LEAVE, ST_DONE };

typedef struct {
    float x, y, vx, vy;   // centre, px and px/frame
    float s;              // dot diameter now
    int16_t tx, ty;       // target centre
    uint8_t ts;           // target diameter (0 = no target this cycle)
    uint8_t st;
    uint8_t vis;          // drawn
    uint8_t phase;        // drift flutter phase
    uint16_t delay;       // frames until launch
    uint16_t age;         // frames since launch
} part_t;

// Allocated while the saver runs (~55 KB), so it costs no RAM otherwise; the Wi-Fi
// export, which needs a lot of heap, never runs at the same time.
typedef struct {
    part_t p[NP];
    uint16_t band[W * BAND_H];
    uint16_t site[NP];
    uint8_t size[NP];
} saver_mem_t;
static saver_mem_t *s_mem;
#define s_p (s_mem->p)
#define s_band (s_mem->band)

// ---- cycle state ----------------------------------------------------------------
enum { M_GRAVITY, M_SPRING, M_SWIRL, M_DRIFT, M__COUNT };
static const char *const MOTION_NAMES[M__COUNT] = { "gravity", "spring", "swirl", "drift" };
// origins: sides L R T B, corners TL TR BL BR, centre
enum { O_L, O_R, O_T, O_B, O_TL, O_TR, O_BL, O_BR, O_C, O__COUNT };
static const char *const ORIGIN_NAMES[O__COUNT] = { "left", "right", "top", "bottom",
    "top-left", "top-right", "bottom-left", "bottom-right", "centre" };
enum { SH_WAVES, SH_ZIGZAG, SH_RINGS, SH_TUNNEL, SH_STRIPES, SH_FADE, SH_GLOW, SH__COUNT };
static const char *const SHAPE_NAMES[SH__COUNT] = { "waves", "zigzag", "rings", "tunnel",
    "stripes", "fade", "glow" };

static struct {
    int kind, levels;
    bool vertical, inv;
    float amp, freq, ph, lambda, cx, cy, ca, sa;
} s_sh;

static bool s_active;
static int s_origin, s_motion, s_cycle;
static float s_ox, s_oy, s_gx, s_gy;  // origin point and direction away from it
static float s_spin;                  // swirl direction
static uint16_t s_fg, s_bg;
static int s_last_color = -1;
static int64_t s_cycle_us, s_next_us;
static int s_frames, s_busy_max_us;   // per cycle, logged: real frame rate on the board
static bool s_dirty[NBANDS];
static uint8_t s_disc[9][8];          // dot masks, diameter 1..8: bit x of row y

static inline uint16_t be16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }
static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

static void make_discs(void)
{
    for (int d = 1; d <= 8; d++) {
        float r = d * 0.5f;
        for (int y = 0; y < d; y++) {
            uint8_t m = 0;
            for (int x = 0; x < d; x++) {
                float dx = x + 0.5f - r, dy = y + 0.5f - r;
                if (d <= 2 || dx * dx + dy * dy <= r * r + 0.3f) m |= 1u << x;
            }
            s_disc[d][y] = m;
        }
    }
}

// ---- shape field: 0 = no dot .. 1 = biggest dot -------------------------------------
static float posterize(float t, int n) { return clampf(floorf(t * n) / (n - 1), 0, 1); }

static float tri(float t) { return 4 * fabsf(t - floorf(t + 0.5f)) - 1; }   // -1..1

static float field(float x, float y)
{
    float u = x / W, v = y / H, f = 0;
    float a = s_sh.vertical ? u : v, b = s_sh.vertical ? v : u;
    switch (s_sh.kind) {
    case SH_WAVES:
        f = posterize(a + s_sh.amp * sinf(b * s_sh.freq * TAU + s_sh.ph), s_sh.levels); break;
    case SH_ZIGZAG:
        f = posterize(a + s_sh.amp * tri(b * s_sh.freq + s_sh.ph), s_sh.levels); break;
    case SH_RINGS: {
        float r = hypotf(x - s_sh.cx, y - s_sh.cy) / s_sh.lambda;
        f = posterize(0.5f + 0.5f * cosf(r * TAU + s_sh.ph), s_sh.levels); break;
    }
    case SH_TUNNEL: {
        float m = fmaxf(fabsf(x - s_sh.cx) / (W * 0.5f), fabsf(y - s_sh.cy) / (H * 0.5f));
        f = clampf((floorf((1 - m) * s_sh.levels) + 1) / s_sh.levels, 0, 1); break;
    }
    case SH_STRIPES: {
        float t = (x * s_sh.ca + y * s_sh.sa) / s_sh.lambda;
        f = powf(0.5f + 0.5f * sinf(t * TAU + s_sh.ph), 1.5f); break;
    }
    case SH_FADE: {
        float t = ((x - W * 0.5f) * s_sh.ca + (y - H * 0.5f) * s_sh.sa) / 300.0f + 0.5f;
        t = clampf(t, 0, 1);
        f = s_sh.amp > 0.5f ? fabsf(2 * t - 1) : t;   // amp doubles as "both edges"
        break;
    }
    case SH_GLOW: {
        float r = 2 * hypotf((x - s_sh.cx) / W, (y - s_sh.cy) / H);
        float t = clampf((r - 0.25f) / 0.8f, 0, 1);
        f = t * t * (3 - 2 * t); break;
    }
    }
    return s_sh.inv ? 1 - f : f;
}

static void pick_shape(void)
{
    memset(&s_sh, 0, sizeof s_sh);
    s_sh.kind = rndi(SH__COUNT);
    s_sh.vertical = rndi(3) == 0;
    s_sh.inv = rndi(2);
    s_sh.levels = 4 + rndi(3);
    s_sh.amp = 0.06f + rnd() * 0.12f;
    s_sh.freq = 1 + rnd() * 2;
    s_sh.ph = rnd() * TAU;
    s_sh.lambda = 40 + rnd() * 60;
    float spread = s_sh.kind == SH_TUNNEL ? 0.15f : 0.4f;   // tunnel: near the middle
    s_sh.cx = W * (0.5f + rnds(spread * 0.5f));
    s_sh.cy = H * (0.5f + rnds(spread * 0.5f));
    float ang = rnd() * TAU;
    s_sh.ca = cosf(ang); s_sh.sa = sinf(ang);
    if (s_sh.kind == SH_FADE) s_sh.amp = rnd();
}

// ---- origin / colour ----------------------------------------------------------------
static void set_origin(int o)
{
    static const float G[O__COUNT][2] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
        { 0.6f, 0.8f }, { -0.6f, 0.8f }, { 0.6f, -0.8f }, { -0.6f, -0.8f }, { 0, 0 } };
    static const float P[O__COUNT][2] = {
        { 0, 0 }, { W, 0 }, { 0, 0 }, { 0, H }, { 0, 0 }, { W, 0 }, { 0, H }, { W, H },
        { W / 2, H / 2 } };
    s_origin = o;
    s_gx = G[o][0]; s_gy = G[o][1];
    s_ox = P[o][0]; s_oy = P[o][1];
}

// Tilt: dots come from the raised side (gravity points away from the origin).
static int pick_origin(void)
{
    float gx, gy;
    if (saver_tilt(&gx, &gy)) {
        float m = hypotf(gx, gy);
        if (m < 0.25f) return O_C;                       // lying flat
        float ax = fabsf(gx) / m, ay = fabsf(gy) / m;
        if (ax > 0.38f && ay > 0.38f)
            return gy > 0 ? (gx > 0 ? O_TL : O_TR) : (gx > 0 ? O_BL : O_BR);
        if (ax > ay) return gx > 0 ? O_L : O_R;
        return gy > 0 ? O_T : O_B;
    }
    int o;
    do o = rndi(O__COUNT); while (o == s_origin && s_cycle > 0);
    return o;
}

static int luma(uint16_t c)
{
    int r = (c >> 11) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
    return (r * 3 + g * 6 + b) / 10;
}

// One colour per cycle: from the beads in the pattern; with fewer than two usable
// colours there, the shortlist joins in. Colours too dark to see on black are skipped.
static void pick_color(void)
{
    const palette_t *p = &PALETTES[UI.palette];
    static bool seen[256];
    int pool[256], n = 0;
    memset(seen, 0, sizeof seen);
    for (int i = 0; i < GRID_N * GRID_N; i++) {
        int c = UI.cells[i];
        if (c == CELL_EMPTY || c >= p->count || seen[c]) continue;
        seen[c] = true;
        if (luma(p->colors[c].rgb565) >= 50) pool[n++] = c;
    }
    if (n < 2)
        for (int i = 0; i < SLOTS; i++) {
            int c = UI.slots[i];
            if (c >= p->count || seen[c]) continue;
            seen[c] = true;
            if (luma(p->colors[c].rgb565) >= 50) pool[n++] = c;
        }
    int c = -1;
    if (n == 1) c = pool[0];
    else if (n > 1) do c = pool[rndi(n)]; while (c == s_last_color);
    s_last_color = c;
    s_fg = c >= 0 ? p->colors[c].rgb565 : 0xFFFF;
    s_bg = 0x0000;
}

// ---- per-particle geometry ----------------------------------------------------------
// Direction away from the origin for a dot heading to (tx, ty).
static void dir_for(const part_t *q, float *gx, float *gy)
{
    if (s_origin != O_C) { *gx = s_gx; *gy = s_gy; return; }
    float dx = q->tx - s_ox, dy = q->ty - s_oy, m = hypotf(dx, dy);
    if (m < 1) { *gx = 0; *gy = 1; return; }
    *gx = dx / m; *gy = dy / m;
}

// How far "downstream" of the origin a target lies (launch order).
static float origin_dist(const part_t *q)
{
    if (s_origin == O_C) return hypotf(q->tx - s_ox, q->ty - s_oy);
    return (q->tx - s_ox) * s_gx + (q->ty - s_oy) * s_gy;
}

static void spawn(part_t *q)
{
    float gx, gy;
    dir_for(q, &gx, &gy);
    bool corner = s_origin >= O_TL && s_origin <= O_BR;
    if (s_origin == O_C) {
        q->x = s_ox + rnds(6); q->y = s_oy + rnds(6);
    } else if (corner && s_motion != M_GRAVITY) {
        q->x = s_ox - gx * 6 + rnds(10); q->y = s_oy - gy * 6 + rnds(10);
    } else {                          // on a line through the origin, across the flow
        float back = origin_dist(q) + 6 + rnd() * 14;
        q->x = q->tx - gx * back; q->y = q->ty - gy * back;
    }
    q->vx = q->vy = 0;
    q->s = s_origin == O_C || (corner && s_motion != M_GRAVITY) ? 1 : q->ts;
    q->vis = 0;
}

static bool offscreen(const part_t *q)
{
    return q->x < -12 || q->y < -12 || q->x > W + 12 || q->y > H + 12;
}

// ---- dirty bands / drawing ----------------------------------------------------------
static int dot_d(float s) { int d = (int)(s + 0.5f); return d > 8 ? 8 : d; }

static void mark(const part_t *q)
{
    int d = dot_d(q->s);
    if (!q->vis || d < 1) return;
    int y0 = (int)floorf(q->y - d * 0.5f + 0.5f), y1 = y0 + d - 1;
    if (y1 < 0 || y0 >= H) return;
    if (y0 < 0) y0 = 0;
    if (y1 >= H) y1 = H - 1;
    for (int b = y0 / BAND_H; b <= y1 / BAND_H; b++) s_dirty[b] = true;
}

static void draw_band(const display_ops_t *D, int b)
{
    int by = b * BAND_H, bh = H - by < BAND_H ? H - by : BAND_H;
    uint16_t bg = be16(s_bg), fg = be16(s_fg);
    for (int i = 0; i < W * bh; i++) s_band[i] = bg;
    for (int i = 0; i < NP; i++) {
        const part_t *q = &s_p[i];
        int d = dot_d(q->s);
        if (!q->vis || d < 1) continue;
        int ix = (int)floorf(q->x - d * 0.5f + 0.5f), iy = (int)floorf(q->y - d * 0.5f + 0.5f);
        if (iy + d <= by || iy >= by + bh || ix + d <= 0 || ix >= W) continue;
        for (int y = 0; y < d; y++) {
            int sy = iy + y - by;
            if (sy < 0 || sy >= bh) continue;
            uint8_t m = s_disc[d][y];
            uint16_t *row = &s_band[sy * W];
            for (int x = 0; x < d; x++) {
                int sx = ix + x;
                if ((m >> x & 1) && sx >= 0 && sx < W) row[sx] = fg;
            }
        }
    }
    D->blit(0, by, W, bh, s_band);
}

// ---- cycles ----------------------------------------------------------------------------
static void new_cycle(void)
{
    pick_shape();
    s_motion = rndi(M__COUNT);
    set_origin(pick_origin());
    s_spin = rndi(2) ? 1 : -1;
    uint16_t old_fg = s_fg;
    pick_color();
    if (s_fg != old_fg) for (int b = 0; b < NBANDS; b++) s_dirty[b] = true;  // recolour

    // Targets: lattice sites with a dot, in random order.
    uint16_t *site = s_mem->site;
    uint8_t *size = s_mem->size;
    int m = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            float f = field(c * PITCH + PITCH / 2, r * PITCH + PITCH / 2);
            int d;
            if (f < 0.03f) continue;
            if (f < 0.15f) {                              // sparse stipple in light areas
                if (rnd() > f / 0.15f * 0.5f) continue;
                d = 1 + rndi(2);
            } else {
                d = (int)lroundf(f * 8 + rnds(0.7f));
                d = d < 1 ? 1 : d > 8 ? 8 : d;
            }
            site[m] = (uint16_t)(r * COLS + c); size[m] = (uint8_t)d; m++;
        }
    for (int i = m - 1; i > 0; i--) {                     // shuffle
        int j = rndi(i + 1);
        uint16_t t = site[i]; site[i] = site[j]; site[j] = t;
        uint8_t u = size[i]; size[i] = size[j]; size[j] = u;
    }

    float maxd = 1;
    for (int i = 0; i < m; i++) {
        part_t *q = &s_p[i];
        q->tx = (int16_t)(site[i] % COLS * PITCH + PITCH / 2);
        q->ty = (int16_t)(site[i] / COLS * PITCH + PITCH / 2);
        q->ts = size[i];
        float d = origin_dist(q);
        if (d > maxd) maxd = d;
    }
    static const int SPREAD[M__COUNT] = { 35, 25, 30, 45 };   // frames, first..last launch
    for (int i = 0; i < NP; i++) {
        part_t *q = &s_p[i];
        bool target = i < m;
        if (!target) q->ts = 0;
        bool shown = q->vis;
        if (target)
            q->delay = (uint16_t)(fmaxf(origin_dist(q), 0) / maxd * SPREAD[s_motion] + rndi(12));
        q->age = 0;
        q->phase = (uint8_t)rndi(256);
        if (!shown) {                   // not on screen (or not launched yet): enter anew
            if (target) { spawn(q); q->st = ST_WAIT; }
            else q->st = ST_OFF;
            continue;
        }
        // Dots on screen: the old image breaks up with the new motion.
        if (s_motion == M_GRAVITY) {
            q->st = ST_FALL;            // fall out downstream, then pour back in
            q->vx += rnds(0.8f); q->vy += rnds(0.8f);
        } else {
            float gx = s_gx, gy = s_gy;
            if (s_origin == O_C) {
                float dx = q->x - s_ox, dy = q->y - s_oy, mm = hypotf(dx, dy);
                gx = mm > 1 ? dx / mm : 0; gy = mm > 1 ? dy / mm : 1;
            }
            float a = rnd() * TAU, k = 1.5f + rnd() * 3;
            q->vx = cosf(a) * k + gx * 2; q->vy = sinf(a) * k + gy * 2;
            q->st = target ? ST_WAIT : ST_LEAVE;
        }
    }
    if (s_cycle > 0)
        ESP_LOGI(TAG, "cycle %d: %d frames (%.1f fps), slowest frame %d ms", s_cycle, s_frames,
                 s_frames * 1e6f / (esp_timer_get_time() - s_cycle_us), s_busy_max_us / 1000);
    s_frames = 0; s_busy_max_us = 0;
    s_cycle++;
    s_cycle_us = esp_timer_get_time();
    ESP_LOGI(TAG, "cycle %d: %s from %s, %s, colour %04x, %d dots", s_cycle,
             SHAPE_NAMES[s_sh.kind], ORIGIN_NAMES[s_origin], MOTION_NAMES[s_motion], s_fg, m);
}

// ---- physics ------------------------------------------------------------------------
static void cap(part_t *q, float vmax)
{
    float v = hypotf(q->vx, q->vy);
    if (v > vmax) { q->vx *= vmax / v; q->vy *= vmax / v; }
}

static void settle(part_t *q)
{
    q->x = q->tx; q->y = q->ty; q->vx = q->vy = 0; q->s = q->ts; q->st = ST_DONE;
}

static void launch(part_t *q)
{
    q->st = ST_MOVE; q->age = 0;
    bool fresh = !q->vis;
    q->vis = 1;
    if (!fresh) return;                         // scattered dots keep their momentum
    float gx, gy;
    dir_for(q, &gx, &gy);
    switch (s_motion) {
    case M_GRAVITY: q->vx = gx * 2; q->vy = gy * 2; break;
    case M_SPRING:  q->vx = (q->tx - q->x) * 0.09f + rnds(1.5f);
                    q->vy = (q->ty - q->y) * 0.09f + rnds(1.5f); break;
    default: break;
    }
}

static void step_move(part_t *q)
{
    float dx = q->tx - q->x, dy = q->ty - q->y;
    q->age++;
    switch (s_motion) {
    case M_GRAVITY: {
        float gx, gy;
        dir_for(q, &gx, &gy);
        float va = q->vx * gx + q->vy * gy + 0.55f;
        if (va > 16) va = 16;
        q->x += gx * va; q->y += gy * va;
        dx = q->tx - q->x; dy = q->ty - q->y;
        float al = dx * gx + dy * gy;                 // still to fall
        q->x += (dx - al * gx) * 0.2f;                // steer across the flow
        q->y += (dy - al * gy) * 0.2f;
        if (al < 0) {                                 // passed the target: bounce
            q->x += gx * al; q->y += gy * al;
            va = -va * 0.4f;
            if (fabsf(va) < 1.5f) va = 0;
        }
        q->vx = gx * va; q->vy = gy * va;
        q->s += (q->ts - q->s) * 0.2f;
        dx = q->tx - q->x; dy = q->ty - q->y;
        if (va == 0 && dx * dx + dy * dy < 0.5f) settle(q);
        break;
    }
    case M_SPRING:
        q->vx += dx * 0.03f - q->vx * 0.10f;
        q->vy += dy * 0.03f - q->vy * 0.10f;
        cap(q, 18);
        q->x += q->vx; q->y += q->vy;
        q->s += (q->ts - q->s) * 0.15f;
        if (dx * dx + dy * dy < 0.25f && q->vx * q->vx + q->vy * q->vy < 0.06f) settle(q);
        break;
    case M_SWIRL: {
        float cx = s_origin == O_C ? s_ox : W * 0.5f, cy = s_origin == O_C ? s_oy : H * 0.5f;
        float rx = q->x - cx, ry = q->y - cy, r = hypotf(rx, ry);
        float sw = expf(-q->age / 30.0f) * s_spin;
        float tx = r > 1 ? -ry / r : 0, ty = r > 1 ? rx / r : 0;
        q->vx = q->vx * 0.8f + dx * 0.03f + tx * 1.2f * sw;
        q->vy = q->vy * 0.8f + dy * 0.03f + ty * 1.2f * sw;
        cap(q, 14);
        q->x += q->vx; q->y += q->vy;
        q->s += (q->ts - q->s) * 0.1f;
        if (dx * dx + dy * dy < 0.25f && q->vx * q->vx + q->vy * q->vy < 0.09f) settle(q);
        break;
    }
    case M_DRIFT: {
        float gx, gy;
        dir_for(q, &gx, &gy);
        float dist = hypotf(dx, dy);
        float fl = sinf(q->age * 0.12f + q->phase * (TAU / 256)) * 1.5f * fminf(1, dist / 30);
        float w = 0.6f * fminf(1, dist / 40);
        float ex = dx * 0.06f + gx * w - gy * fl, ey = dy * 0.06f + gy * w + gx * fl;
        q->vx = q->vx * 0.9f + ex * 0.1f;
        q->vy = q->vy * 0.9f + ey * 0.1f;
        cap(q, 6);
        q->x += q->vx; q->y += q->vy;
        q->s += (q->ts - q->s) * 0.08f;
        if (dist < 0.5f && q->vx * q->vx + q->vy * q->vy < 0.04f) settle(q);
        break;
    }
    }
}

static void step(part_t *q)
{
    switch (q->st) {
    case ST_OFF: case ST_DONE: return;
    case ST_WAIT:
        if (q->vis) {                               // scattering: coast and slow down
            q->x += q->vx; q->y += q->vy;
            q->vx *= 0.93f; q->vy *= 0.93f;
        }
        if (q->delay) q->delay--;
        else launch(q);
        return;
    case ST_FALL: {                                 // gravity: fall out, then re-enter
        float gx = s_gx, gy = s_gy;
        if (s_origin == O_C) {
            float dx = q->x - s_ox, dy = q->y - s_oy, m = hypotf(dx, dy);
            gx = m > 1 ? dx / m : 0; gy = m > 1 ? dy / m : 1;
        }
        q->vx += gx * 0.55f; q->vy += gy * 0.55f;
        cap(q, 16);
        q->x += q->vx; q->y += q->vy;
        if (q->delay) q->delay--;
        if (offscreen(q)) {
            if (q->ts) { spawn(q); q->st = ST_WAIT; }
            else { q->st = ST_OFF; q->vis = 0; }
        }
        return;
    }
    case ST_LEAVE:
        q->x += q->vx; q->y += q->vy;
        q->vx *= 0.98f; q->vy *= 0.98f;
        q->s *= 0.94f;
        if (q->s < 0.5f || offscreen(q)) { q->st = ST_OFF; q->vis = 0; }
        return;
    case ST_MOVE:
        step_move(q);
        return;
    }
}

// ---- public -------------------------------------------------------------------------
bool saver_active(void) { return s_active; }

void saver_start(void)
{
    static bool once;
    if (!once) { make_discs(); once = true; }
    if (!s_mem) s_mem = calloc(1, sizeof *s_mem);
    if (!s_mem) { ESP_LOGE(TAG, "no memory for the screen saver"); return; }
    s_rng = esp_random() | 1;
    memset(s_p, 0, sizeof s_p);
    s_cycle = 0; s_origin = -1; s_fg = 0;
    s_active = true;
    new_cycle();
    for (int b = 0; b < NBANDS; b++) s_dirty[b] = true;   // clear the screen
    s_next_us = esp_timer_get_time();
}

void saver_stop(void)
{
    s_active = false;
    free(s_mem);
    s_mem = NULL;
    ESP_LOGI(TAG, "stopped");
}

int saver_next_ms(void)
{
    int64_t ms = (s_next_us - esp_timer_get_time()) / 1000;
    return ms < 0 ? 0 : ms > FRAME_MS ? FRAME_MS : (int)ms;
}

void saver_frame(void)
{
    if (!s_active) return;
    int64_t now = esp_timer_get_time();
    if (now < s_next_us) return;
    s_next_us = now + FRAME_MS * 1000;
    if (now - s_cycle_us >= (int64_t)SAVER_CYCLE_MS * 1000) new_cycle();
    for (int i = 0; i < NP; i++) {
        part_t *q = &s_p[i];
        if (q->st == ST_OFF || q->st == ST_DONE) continue;
        part_t was = *q;
        step(q);
        if (q->vis != was.vis || lroundf(q->x) != lroundf(was.x) ||
            lroundf(q->y) != lroundf(was.y) || dot_d(q->s) != dot_d(was.s)) {
            mark(&was);                             // where it was
            mark(q);                                // where it is
        }
    }
    const display_ops_t *D = board_display();
    for (int b = 0; b < NBANDS; b++)
        if (s_dirty[b]) { draw_band(D, b); s_dirty[b] = false; }
    int busy = (int)(esp_timer_get_time() - now);
    if (busy > s_busy_max_us) s_busy_max_us = busy;
    s_frames++;
}
