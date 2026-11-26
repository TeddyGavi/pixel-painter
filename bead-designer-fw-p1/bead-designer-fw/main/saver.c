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
// Timing: physics runs in fixed 40 ms ticks (every speed and delay is per tick);
// frames are drawn every 15 ms with each dot blended between its last two ticks,
// so motion is smooth at ~60 fps without changing how fast anything moves.
#include "saver.h"
#include "sdkconfig.h"
#include "ui.h"
#include "board.h"
#include "palettes.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "saver";

const uint16_t SAVER_TIMEOUT_S[SAVER_CHOICES] = { 0, 5, 30, 60, 300 };
const char *const SAVER_NAMES[SAVER_CHOICES] = { "Off", "5s", "30s", "1m", "5m" };

__attribute__((weak)) bool saver_tilt(float *gx, float *gy) { (void)gx; (void)gy; return false; }

#define W SAVER_W               // the saver's own grid: 225x300 on the 2.41" (shown 2x2), else the screen
#define H SAVER_H
#if CONFIG_IDF_TARGET_ESP32S3
// 2.41" (225x300 bitmap shown 2x2): twice the density (4 px lattice, ~4x the dots) and
// square dots, each logical px a 2x2 panel block, so dots are 2..8 panel px squares.
#define PITCH 4                       // lattice step = largest dot
#define SQUARE_DOTS 1
#else
#define PITCH 8                       // lattice step = largest dot
#define SQUARE_DOTS 0
#endif
#define COLS (W / PITCH)
#define ROWS (H / PITCH)
#define NP (COLS * ROWS)
#define BAND_H 32
#define NBANDS ((H + BAND_H - 1) / BAND_H)
#define FRAME_MS 15                   // drawing interval: >60 fps (a full-screen send takes ~16 ms on the 2.41")
#define PHYS_MS 40                    // physics tick (all speeds are per 40 ms tick)
#define TAU 6.2831853f

// ---- random -----------------------------------------------------------------------
static uint32_t s_rngs[2] = { 1, 7 };   // one per core: the physics also runs on a core-1 worker
static uint32_t rnd32(void)
{
    uint32_t *r = &s_rngs[xPortGetCoreID() & 1];
    *r ^= *r << 13; *r ^= *r >> 17; *r ^= *r << 5; return *r;
}
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
    uint16_t delay;       // ticks until launch
    uint16_t age;         // ticks since launch
    float px, py, ps;     // state at the previous physics tick (frames blend px -> x)
    int16_t rx, ry;       // as drawn: top-left of the dot
    uint8_t rd, rvis;     // as drawn: diameter, shown
} part_t;

// Stored dot, packed (26 B), so the ~4200 dots of the 2.41" fit in internal RAM:
// every per-frame loop is several times slower over PSRAM. The physics still runs
// on a float part_t: unpack, step, pack.
typedef struct __attribute__((packed)) {
    int16_t x, y, vx, vy;     // centre in 1/32 px; velocity in 1/256 px per tick
    int16_t tx, ty;           // target centre
    uint8_t s;                // diameter now, 1/16 px
    uint8_t ts, st, flags, phase, delay, age;
    int8_t dx, dy;            // displacement over the last tick, 1/4 px (frame blend)
    int8_t ds;                // size change over the last tick, 1/16 px
    uint16_t rx;              // as drawn: left + 32 (bits 0..9), diameter (bits 10..13)
    uint16_t ry;              // as drawn: top + 32
} pk_t;
#define F_VIS  1              // shown (physics)
#define F_RVIS 2              // drawn on screen now

// Allocated while the saver runs, so it costs no RAM otherwise; the Wi-Fi export,
// which needs a lot of heap, never runs at the same time.
// The dots live in chunks of 1024: internal RAM has room for all of them but no
// single free block that large.
#define PK_SHIFT 10
#define PK_CHUNK (1 << PK_SHIFT)
#define PK_N ((NP + PK_CHUNK - 1) / PK_CHUNK)
typedef struct {
#if !CONFIG_IDF_TARGET_ESP32S3
    uint16_t band[W * BAND_H];      // boards without a framebuffer compose bands here
#endif
    uint16_t site[NP];
    uint8_t size[NP];
} saver_mem_t;
static saver_mem_t *s_mem;
static pk_t *s_pkc[PK_N];
#define PK(i) (&s_pkc[(i) >> PK_SHIFT][(i) & (PK_CHUNK - 1)])
#define s_band (s_mem->band)

static inline int rnd_i(float f) { return (int)(f >= 0 ? f + 0.5f : f - 0.5f); }   // fast lroundf
static inline float hyp(float a, float b) { return sqrtf(a * a + b * b); }           // fast hypotf (no overflow risk here)
static inline int16_t q16(float v, float scale)
{
    float f = v * scale;
    return (int16_t)(f > 32767.f ? 32767 : f < -32767.f ? -32767 : rnd_i(f));
}

static void unpk(const pk_t *p, part_t *q)
{
    q->x = p->x * (1.0f / 32); q->y = p->y * (1.0f / 32);
    q->vx = p->vx * (1.0f / 256); q->vy = p->vy * (1.0f / 256);
    q->s = p->s * (1.0f / 16);
    q->tx = p->tx; q->ty = p->ty; q->ts = p->ts; q->st = p->st;
    q->vis = p->flags & F_VIS; q->phase = p->phase; q->delay = p->delay; q->age = p->age;
}

static void pk(pk_t *p, const part_t *q)
{
    p->x = q16(q->x, 32); p->y = q16(q->y, 32);
    p->vx = q16(q->vx, 256); p->vy = q16(q->vy, 256);
    float s = q->s * 16; p->s = (uint8_t)(s < 0 ? 0 : s > 255 ? 255 : rnd_i(s));
    p->tx = q->tx; p->ty = q->ty; p->ts = q->ts; p->st = q->st;
    p->flags = (uint8_t)((p->flags & ~F_VIS) | (q->vis ? F_VIS : 0));
    p->phase = q->phase;
    p->delay = (uint8_t)(q->delay > 255 ? 255 : q->delay);
    p->age = (uint8_t)(q->age > 255 ? 255 : q->age);
}

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
static int s_frames, s_busy_max_us; static int64_t s_busy_sum;   // per cycle, logged: real frame rate on the board
static bool s_dirty[NBANDS];
static uint8_t s_disc[9][8];
static uint8_t s_span0[9][8], s_spanw[9][8];   // disc row as a span: first x, width          // dot masks, diameter 1..8: bit x of row y

static inline uint16_t be16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }
static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

#define NGROUP 3                      // staggered physics groups
#define ERASE_MAX NP                  // old dot squares to erase this frame
static uint32_t *s_erase;             // packed x+32 | (y+32) << 10 | d << 20
static int s_nerase = -1;             // -1 = clear whole dirty bands this frame
static int64_t s_tick_g[NGROUP];      // next tick time per group
static volatile int s_w0, s_w1;       // worker's dot range for this tick
static TaskHandle_t s_worker;
static void worker(void *arg);
static SemaphoreHandle_t s_worker_done;
static float s_swirl_decay[256];       // expf(-age / 30): swirl strength by age (ticks)
static float s_sin256[256];            // sin(i * 2pi / 256)

static void make_discs(void)
{
    for (int a = 0; a < 256; a++) { s_swirl_decay[a] = expf(-a / 30.0f); s_sin256[a] = sinf(a * TAU / 256); }
    for (int d = 1; d <= PITCH; d++) {
        float r = d * 0.5f;
        for (int y = 0; y < d; y++) {
            uint8_t m = 0;
            for (int x = 0; x < d; x++) {
                float dx = x + 0.5f - r, dy = y + 0.5f - r;
                if (SQUARE_DOTS || d <= 2 || dx * dx + dy * dy <= r * r + 0.3f) m |= 1u << x;
            }
            s_disc[d][y] = m;
            // Disc rows are contiguous: keep each as a span for fast drawing.
            int x0 = 0; while (x0 < d && !(m >> x0 & 1)) x0++;
            int x1 = d; while (x1 > x0 && !(m >> (x1 - 1) & 1)) x1--;
            s_span0[d][y] = (uint8_t)x0; s_spanw[d][y] = (uint8_t)(x1 - x0);
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
    for (int i = 0; i < UI.n * UI.n; i++) {
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
    float dx = q->tx - s_ox, dy = q->ty - s_oy, m = hyp(dx, dy);
    if (m < 1) { *gx = 0; *gy = 1; return; }
    *gx = dx / m; *gy = dy / m;
}

// How far "downstream" of the origin a target lies (launch order).
static float origin_dist(const part_t *q)
{
    if (s_origin == O_C) return hyp(q->tx - s_ox, q->ty - s_oy);
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
static int dot_d(float s) { int d = (int)(s + 0.5f); return d > PITCH ? PITCH : d; }

static void mark_rect(int iy, int d)
{
    int y0 = iy, y1 = iy + d - 1;
    if (y1 < 0 || y0 >= H) return;
    if (y0 < 0) y0 = 0;
    if (y1 >= H) y1 = H - 1;
    for (int b = y0 / BAND_H; b <= y1 / BAND_H; b++) s_dirty[b] = true;
}

// Fill rows [y0, y1) of a dot (top-left ix, iy, diameter d) into a row buffer that
// starts at screen row `oy`.
static inline void dot_rows(uint16_t *base, int oy, int ix, int iy, int d, int y0, int y1, uint16_t fg)
{
    for (int y = y0; y < y1; y++) {
        int x0 = ix + s_span0[d][y], x1 = x0 + s_spanw[d][y];
        if (x0 < 0) x0 = 0;
        if (x1 > W) x1 = W;
        uint16_t *row = &base[(iy + y - oy) * W];
        for (int x = x0; x < x1; x++) row[x] = fg;
    }
}

#define RX(p) ((int)((p)->rx & 1023) - 32)
#define RD(p) ((int)((p)->rx >> 10))
#define RY(p) ((int)(p)->ry - 32)

// Repaint every dirty band. With a shadow framebuffer (2.41"): clear the dirty
// bands in place, then draw each drawn dot once, clipped to dirty rows.
// Without (Freenove): compose each dirty band in a buffer and blit it.
static uint8_t *s_mono;                // two-colour bitmap (2.41"), else NULL
#define MSTRIDE ((W + 7) / 8)

static void draw_dirty(void)
{
    uint16_t bg = be16(s_bg), fg = be16(s_fg);
    uint16_t *fb = (uint16_t *)board_framebuffer();
    if (s_mono) {
        // Bitmap: clearing whole dirty bands is cheap (29 bytes a row), then set
        // the bits of every dot that touches a dirty band.
        board_fb_lock(true);
        for (int b = 0; b < NBANDS; b++) {
            if (!s_dirty[b]) continue;
            int by = b * BAND_H, bh = H - by < BAND_H ? H - by : BAND_H;
            memset(s_mono + by * MSTRIDE, 0, bh * MSTRIDE);
            board_fb_dirty(0, by, W, bh);
        }
        for (int i = 0; i < NP; i++) {
            const pk_t *p = PK(i);
            if (!(p->flags & F_RVIS)) continue;
            int ix = RX(p), iy = RY(p), d = RD(p);
            int y0 = iy < 0 ? 0 : iy, y1 = iy + d > H ? H : iy + d;
            int x0 = ix < 0 ? 0 : ix, x1 = ix + d > W ? W : ix + d;
            for (int y = y0; y < y1; y++) {
                if (!s_dirty[y / BAND_H]) continue;
                uint8_t *row = s_mono + y * MSTRIDE;
                for (int x = x0; x < x1; x++) row[x >> 3] |= (uint8_t)(1u << (x & 7));
            }
        }
        board_fb_lock(false);
    } else if (fb) {
        // Erase only where moved dots were (not whole bands: far less PSRAM
        // traffic), then redraw every dot in the dirty bands, which also repairs
        // any neighbour an erase clipped (its band is dirty too).
        board_fb_lock(true);
        if (s_nerase < 0) {                                  // overflowed or first frame: clear all dirty bands
            for (int b = 0; b < NBANDS; b++) {
                if (!s_dirty[b]) continue;
                int by = b * BAND_H, bh = H - by < BAND_H ? H - by : BAND_H;
                uint16_t *row = fb + by * W;
                for (int i = 0; i < W * bh; i++) row[i] = bg;
            }
        } else {
            for (int k = 0; k < s_nerase; k++) {
                uint32_t e = s_erase[k];
                int ex = (int)(e & 1023) - 32, ey = (int)(e >> 10 & 1023) - 32, ed = (int)(e >> 20);
                int x0 = ex < 0 ? 0 : ex, x1 = ex + ed > W ? W : ex + ed;
                int y0 = ey < 0 ? 0 : ey, y1 = ey + ed > H ? H : ey + ed;
                for (int y = y0; y < y1; y++) {
                    uint16_t *row = fb + y * W;
                    for (int x = x0; x < x1; x++) row[x] = bg;
                }
            }
        }
        s_nerase = 0;
        for (int b = 0; b < NBANDS; b++) {
            if (!s_dirty[b]) continue;
            int by = b * BAND_H, bh = H - by < BAND_H ? H - by : BAND_H;
            board_fb_dirty(0, by, W, bh);
        }
        for (int i = 0; i < NP; i++) {
            const pk_t *p = PK(i);
            if (!(p->flags & F_RVIS)) continue;
            int ix = RX(p), iy = RY(p), d = RD(p);
            int y0 = iy < 0 ? -iy : 0, y1 = iy + d > H ? H - iy : d;
            // most dots lie in one band: test the first and last row only
            int b0 = (iy + y0) / BAND_H, b1 = (iy + y1 - 1) / BAND_H;
            if (!s_dirty[b0] && !s_dirty[b1]) continue;
            if (b0 == b1 || (s_dirty[b0] && s_dirty[b1])) { dot_rows(fb, 0, ix, iy, d, y0, y1, fg); continue; }
            int split = b1 * BAND_H - iy;                  // first row in band b1
            if (s_dirty[b0]) dot_rows(fb, 0, ix, iy, d, y0, split, fg);
            else             dot_rows(fb, 0, ix, iy, d, split, y1, fg);
        }
        board_fb_lock(false);
    } else {
#if !CONFIG_IDF_TARGET_ESP32S3
        const display_ops_t *D = board_display();
        for (int b = 0; b < NBANDS; b++) {
            if (!s_dirty[b]) continue;
            int by = b * BAND_H, bh = H - by < BAND_H ? H - by : BAND_H;
            for (int i = 0; i < W * bh; i++) s_band[i] = bg;
            for (int i = 0; i < NP; i++) {
                const pk_t *p = PK(i);
                if (!(p->flags & F_RVIS)) continue;
                int ix = RX(p), iy = RY(p), d = RD(p);
                if (iy + d <= by || iy >= by + bh) continue;
                int y0 = by - iy > 0 ? by - iy : 0, y1 = by + bh - iy < d ? by + bh - iy : d;
                dot_rows(s_band, by, ix, iy, d, y0, y1, fg);
            }
            D->blit(0, by, W, bh, s_band);
        }
#endif
    }
    for (int b = 0; b < NBANDS; b++) s_dirty[b] = false;
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
    if (s_fg != old_fg) {                                                     // recolour
        for (int b = 0; b < NBANDS; b++) s_dirty[b] = true;
        if (s_mono) board_mono_color(s_fg, s_bg);
    }

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
                d = PITCH >= 8 ? 1 + rndi(2) : 1;
            } else {
                d = (int)lroundf(f * PITCH + rnds(0.7f * PITCH / 8));
                d = d < 1 ? 1 : d > PITCH ? PITCH : d;
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
        pk_t *p = PK(i);
        p->tx = (int16_t)(site[i] % COLS * PITCH + PITCH / 2);
        p->ty = (int16_t)(site[i] / COLS * PITCH + PITCH / 2);
        p->ts = size[i];
        part_t q; unpk(p, &q);
        float d = origin_dist(&q);
        if (d > maxd) maxd = d;
    }
    static const int SPREAD[M__COUNT] = { 35, 25, 30, 45 };   // frames, first..last launch
    for (int i = 0; i < NP; i++) {
        part_t qq, *q = &qq;
        unpk(PK(i), q);
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
            pk(PK(i), q); PK(i)->dx = PK(i)->dy = PK(i)->ds = 0;
            continue;
        }
        // Dots on screen: the old image breaks up with the new motion.
        if (s_motion == M_GRAVITY) {
            q->st = ST_FALL;            // fall out downstream, then pour back in
            q->vx += rnds(0.8f); q->vy += rnds(0.8f);
        } else {
            float gx = s_gx, gy = s_gy;
            if (s_origin == O_C) {
                float dx = q->x - s_ox, dy = q->y - s_oy, mm = hyp(dx, dy);
                gx = mm > 1 ? dx / mm : 0; gy = mm > 1 ? dy / mm : 1;
            }
            float a = rnd() * TAU, k = 1.5f + rnd() * 3;
            q->vx = cosf(a) * k + gx * 2; q->vy = sinf(a) * k + gy * 2;
            q->st = target ? ST_WAIT : ST_LEAVE;
        }
        pk(PK(i), q);
    }
    if (s_cycle > 0)
        ESP_LOGI(TAG, "cycle %d: %d frames (%.1f fps), slowest frame %d ms, average %lld us", s_cycle, s_frames,
                 s_frames * 1e6f / (esp_timer_get_time() - s_cycle_us), s_busy_max_us / 1000,
                 (long long)(s_busy_sum / (s_frames ? s_frames : 1)));
    s_busy_sum = 0;
    s_frames = 0; s_busy_max_us = 0;
    s_cycle++;
    s_cycle_us = esp_timer_get_time();
    ESP_LOGI(TAG, "cycle %d: %s from %s, %s, colour %04x, %d dots", s_cycle,
             SHAPE_NAMES[s_sh.kind], ORIGIN_NAMES[s_origin], MOTION_NAMES[s_motion], s_fg, m);
}

// ---- physics ------------------------------------------------------------------------
static void cap(part_t *q, float vmax)
{
    float v2 = q->vx * q->vx + q->vy * q->vy;
    if (v2 > vmax * vmax) { float k = vmax / sqrtf(v2); q->vx *= k; q->vy *= k; }   // sqrt only when capping
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
        float rx = q->x - cx, ry = q->y - cy, r = hyp(rx, ry);
        float sw = s_swirl_decay[q->age > 255 ? 255 : q->age] * s_spin;
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
        float dist = hyp(dx, dy);
        // sin(age * 0.12 + phase * 2pi/256) from a 256-entry table (age*0.12 rad = age*4.889 steps)
        float fl = s_sin256[(((q->age * 1252) >> 8) + q->phase) & 255] * 1.5f * fminf(1, dist / 30);
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
            float dx = q->x - s_ox, dy = q->y - s_oy, m = hyp(dx, dy);
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

static void saver_free(void)
{
    for (int k = 0; k < PK_N; k++) { free(s_pkc[k]); s_pkc[k] = NULL; }
    free(s_erase); s_erase = NULL;
    free(s_mem);
    s_mem = NULL;
}

void saver_start(void)
{
    static bool once;
    if (!once) {
        make_discs();
        once = true;
#if CONFIG_IDF_TARGET_ESP32S3
        s_worker_done = xSemaphoreCreateBinary();
        xTaskCreatePinnedToCore(worker, "saverphys", 4096, NULL, 11, &s_worker, 1);
#endif
    }
    // Two-colour bitmap first (small, internal), where the board has one.
    s_mono = board_mono_begin(0xFFFF, 0x0000);
    // Dots in internal RAM: the per-frame loops over every dot are several times
    // slower in PSRAM. The rest (only used when a cycle starts) may go anywhere.
    int in_psram = 0;
    for (int k = 0; k < PK_N; k++) {
        size_t n = (k == PK_N - 1 ? NP - k * PK_CHUNK : PK_CHUNK) * sizeof(pk_t);
        if (!s_pkc[k]) s_pkc[k] = heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!s_pkc[k]) { s_pkc[k] = calloc(1, n); in_psram++; }
        if (!s_pkc[k]) { ESP_LOGE(TAG, "no memory for the screen saver"); saver_free(); return; }
        memset(s_pkc[k], 0, n);
    }
    if (!s_mem) s_mem = calloc(1, sizeof *s_mem);
    if (!s_mem) { ESP_LOGE(TAG, "no memory for the screen saver"); saver_free(); return; }
#if SAVER_W != DISPLAY_W
    // The framebuffer and band paths draw at screen size; this board only supports
    // the (2x2-expanded) bitmap.
    if (!s_mono) { ESP_LOGE(TAG, "no two-colour bitmap for the screen saver"); saver_free(); return; }
#endif
    if (!s_mono && board_framebuffer() && !s_erase) {
        s_erase = heap_caps_malloc(ERASE_MAX * sizeof *s_erase, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!s_erase) s_erase = malloc(ERASE_MAX * sizeof *s_erase);
    }
    s_nerase = -1;                                    // first frame clears the screen
    ESP_LOGI(TAG, "%d dots (%d B each), %d of %d chunks in PSRAM", NP, (int)sizeof(pk_t), in_psram, PK_N);
    s_rngs[0] = esp_random() | 1; s_rngs[1] = esp_random() | 1;
    s_cycle = 0; s_origin = -1; s_fg = 0;
    s_active = true;
    new_cycle();
    if (s_mono) board_mono_color(s_fg, s_bg);
    ESP_LOGI(TAG, "drawing: %s", s_mono ? "two-colour bitmap" : board_framebuffer() ? "framebuffer" : "bands");
    for (int b = 0; b < NBANDS; b++) s_dirty[b] = true;   // clear the screen
    s_next_us = esp_timer_get_time();
    for (int g = 0; g < NGROUP; g++) s_tick_g[g] = s_next_us + (int64_t)g * PHYS_MS * 1000 / NGROUP;
}

void saver_stop(void)
{
    s_active = false;
    if (s_mono) { board_mono_end(); s_mono = NULL; }
    saver_free();
    ESP_LOGI(TAG, "stopped");
}

int saver_next_ms(void)
{
    int64_t ms = (s_next_us - esp_timer_get_time()) / 1000;
    // At least 1 ms: the UI task must block every frame, or the idle task on its
    // core starves (task watchdog) whenever a frame runs long.
    return ms < 1 ? 1 : ms > FRAME_MS ? FRAME_MS : (int)ms;
}

// One physics tick for dots [i0, i1); also records each dot's displacement for
// the frame blend.
static void tick_range(int i0, int i1)
{
    for (int i = i0; i < i1; i++) {
        pk_t *p = PK(i);
        p->dx = p->dy = p->ds = 0;
        if (p->st == ST_OFF || p->st == ST_DONE) continue;
        if (p->st == ST_WAIT && !(p->flags & F_VIS) && p->delay) { p->delay--; continue; }  // not launched yet
        part_t q; unpk(p, &q);
        float ox = q.x, oy = q.y, os = q.s;
        step(&q);
        pk(p, &q);
        float ddx = (q.x - ox) * 4, ddy = (q.y - oy) * 4, dds = (q.s - os) * 16;
        if (fabsf(ddx) < 127 && fabsf(ddy) < 127) {   // else jumped (respawn): no blend
            p->dx = (int8_t)rnd_i(ddx); p->dy = (int8_t)rnd_i(ddy);
        }
        p->ds = (int8_t)(dds > 127 ? 127 : dds < -127 ? -127 : rnd_i(dds));
    }
}

// Physics worker on core 1 (below the display task): steps the second half.
__attribute__((unused)) static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        tick_range(s_w0, s_w1);
        xSemaphoreGive(s_worker_done);
    }
}

void saver_frame(void)
{
    if (!s_active) return;
    int64_t now = esp_timer_get_time();
    if (now < s_next_us) return;
    // Fixed cadence: wake-up slack (1 ms ticks) must not add up frame after frame.
    s_next_us += FRAME_MS * 1000;
    if (s_next_us <= now) s_next_us = now + FRAME_MS * 1000;   // fell behind: resync
    if (now - s_cycle_us >= (int64_t)SAVER_CYCLE_MS * 1000) new_cycle();
    // Physics: the dots are in NGROUP groups whose 40 ms ticks are staggered, so
    // each frame steps about one group instead of every frame-and-a-half stepping
    // all of them at once. Catch up on whole ticks (at most 3; else resync). Each
    // group's tick is split between this core and the worker on the other core.
    for (int g = 0; g < NGROUP; g++) {
        int i0 = g * NP / NGROUP, i1 = (g + 1) * NP / NGROUP, mid = (i0 + i1) / 2;
        for (int t = 0; t < 3 && now >= s_tick_g[g]; t++) {
            if (s_worker) {
                s_w0 = mid; s_w1 = i1;
                xTaskNotifyGive(s_worker);
                tick_range(i0, mid);
                xSemaphoreTake(s_worker_done, portMAX_DELAY);
            } else {
                tick_range(i0, i1);
            }
            s_tick_g[g] += PHYS_MS * 1000;
        }
        if (now >= s_tick_g[g]) s_tick_g[g] = now + PHYS_MS * 1000;
    }
    float bg_[NGROUP];                                       // 1 - blend, per group
    for (int g = 0; g < NGROUP; g++) {
        float v = (float)(s_tick_g[g] - now) / (PHYS_MS * 1000.0f);
        bg_[g] = v < 0 ? 0 : v > 1 ? 1 : v;
    }
    // Where each dot is drawn this frame; mark the bands whose dots changed.
    int g = 0, gend = NP / NGROUP;
    float b = bg_[0];
    for (int i = 0; i < NP; i++) {
        if (i >= gend) { g++; gend = (g + 1) * NP / NGROUP; b = bg_[g]; }
        pk_t *p = PK(i);
        int rvis = p->flags & F_RVIS;
        if (!(p->flags & F_VIS) && !rvis) continue;                 // hidden, and nothing drawn
        if (p->st == ST_DONE && rvis && !p->dx && !p->dy && !p->ds) continue;   // settled
        float x = p->x * (1.0f / 32) - b * p->dx * 0.25f;
        float y = p->y * (1.0f / 32) - b * p->dy * 0.25f;
        int d = dot_d(p->s * (1.0f / 16) - b * p->ds * (1.0f / 16));
        // floor via an offset cast (positions stay well above -1024)
        int ix = (int)(x - d * 0.5f + 0.5f + 1024.0f) - 1024;
        int iy = (int)(y - d * 0.5f + 0.5f + 1024.0f) - 1024;
        int vis = (p->flags & F_VIS) && d >= 1 && ix + d > 0 && ix < W && iy + d > 0 && iy < H;
        if (vis && rvis && ix == RX(p) && iy == RY(p) && d == RD(p)) continue;
        if (!vis && !rvis) continue;
        if (rvis) {                                   // where it was: mark, and erase on the 2.41"
            mark_rect(RY(p), RD(p));
            if (s_nerase >= 0) {
                if (s_erase && s_nerase < ERASE_MAX) s_erase[s_nerase++] = ((uint32_t)p->rx & 1023) | ((uint32_t)p->ry << 10) | ((uint32_t)RD(p) << 20);
                else s_nerase = -1;                   // too many: clear the bands instead
            }
        }
        if (vis) {
            mark_rect(iy, d);                         // where it is
            p->rx = (uint16_t)((ix + 32) | d << 10); p->ry = (uint16_t)(iy + 32);
            p->flags |= F_RVIS;
        } else {
            p->flags &= ~F_RVIS;
        }
    }
    draw_dirty();
    int busy = (int)(esp_timer_get_time() - now);
    if (busy > s_busy_max_us) s_busy_max_us = busy;
    s_busy_sum += busy;
    s_frames++;
}
