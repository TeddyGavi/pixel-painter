// Included by render.c when UI_HIRES: motion, UI update stage 4 (docs/ui-update-2.41,
// motion.md). The user's bar: completely smooth at 60 fps, no visual artifacts. So
// there are no outline effects and no blinks: the System 7 zoom rects, the menu
// commit blink and the one-frame ring flickers were tried and removed (2026-11-21).
// Screen changes (menu open/close, submenus) are instant.
//
// How it works: an animation sets the overrides at the top of render.c (cursor ring
// position/colour, a growing bead, chip lifts, strip brightness, the menu frame)
// and redraws its area through the normal drawing code, once per frame (time
// based: a late frame jumps ahead rather than slowing down). The last frame clears
// the overrides and redraws once more, so every animation ends on exactly the
// static screen (the board goldens check this).
//
// Input always wins: ui.c calls render_anim_finish() before handling any event,
// which jumps to the final frame at once. Menu > Motion: Off = no animation at all.
//
// What moves (numbers from motion.md):
//   cursor glide   83 ms out-cubic; snaps when moves come faster (held D-pad) and
//                  across a wrap
//   bead drop     100 ms, 70 % -> 106 % -> 100 %, peg gone at once, shadow fades in
//   bead pop       67 ms in-cubic 100 % -> 0 over the empty cell
//   chip select   100 ms: ring jumps at once; the new chip rises, the old one settles
//   focus change   67 ms: strip and name line fade 50 % <-> 100 % (the cursor ring
//                  takes its new brightness at once: redrawing it every frame as
//                  well kept the fade under 60 fps); after a glide, the fade starts
//                  when the glide ends
//   eyedropper    the strip is bright while the picked chip rises (117 ms), then
//                 fades back to dim
//   highlight      67 ms out-cubic slide to the next row (snaps when scrolling,
//                  wrapping, or turning faster than that)
// The screen saver's brightness ramps live in ui.c.

#define FRAME_MS      16
#define GLIDE_MS      83
#define DROP_MS       100
#define POP_MS        67
#define CHIP_MS       100
#define FADE_MS       67
#define EYEDROP_MS    117
#define HILITE_MS     67
#define OVERRUN_US    40000        // two frames this late in a row (25 fps): cut to the end
#ifndef MOTION_SLOW
#define MOTION_SLOW   1            // debug: N > 1 plays every animation N times slower
#endif                             // (screen dumps of the in-between frames)

enum { G_NONE, G_GLIDE, G_DROP, G_POP };
enum { Z_NONE, Z_HILITE };

static struct {
    bool on;
    int64_t t0, last_us;
    int late;                       // consecutive late frames
    // grid
    int g, gdur;
    int fx, fy, tx, ty, lx, ly;     // glide: from / to / last drawn cell position (px)
    // strip
    bool chips; int cdur; int8_t lf[SLOTS], lt[SLOTS];   // chip lifts from -> to
    bool fade; int fdelay, fdur, lfrom, lto;             // strip level
    // screen
    int z, zdur;
    int hrow[2], hy0[2], hy1[2];    // highlight: rows, ring y range from [0] to [1]
    // What the last frame drew from (skip redraws that would come out the same).
    bool drawn;
    int cell_sig[5], strip_sig[SLOTS + 2], info_sig;
} A;

static event_t s_ev = EV__COUNT;    // event behind this render_update
static int64_t s_last_move_us, s_last_hilite_us;

static inline float clamp01(float t) { return t < 0 ? 0 : t > 1 ? 1 : t; }
static inline float out_cubic(float t) { t = 1 - clamp01(t); return 1 - t * t * t; }
static inline float in_cubic(float t)  { t = clamp01(t); return t * t * t; }
static inline float out_back(float t)  // overshoots to 1.2 of the range: 70 % -> 106 % -> 100 %
{
    const float s = 2.6f;
    t = clamp01(t) - 1;
    return 1 + (s + 1) * t * t * t + s * t * t;
}
static inline int lerp(int a, int b, float f) { return a + (int)((b - a) * f + (b >= a ? 0.5f : -0.5f)); }

static void cell_pos(int x, int y, int *px, int *py)
{
    const int pitch = s_cell + s_gap;
    *px = s_ox + x * pitch; *py = s_oy + y * pitch;
}

// The cursor's tile plus its halo margin with the cell's top left at (px, py);
// the beads come from the cursor-free copy (only the cursor moves while gliding).
static void cursor_rect(int px, int py)
{
    const int h = s_gap / 2, pitch = s_cell + s_gap;
    grid_rect_ex(px - h - HALO_M, py - h - HALO_M, px - h + pitch + HALO_M, py - h + pitch + HALO_M, true);
}


// ---- one frame -------------------------------------------------------------------
// Set the overrides for `ms` after the start; channels whose time is up get their
// resting values. Returns true while anything still moves.
static bool anim_set(int ms)
{
    bool more = false;
    float t;
    s_cur_px = -1; s_acell = -1;
    if (A.g == G_GLIDE && ms < A.gdur) {
        float f = out_cubic((float)ms / A.gdur);
        s_cur_px = lerp(A.fx, A.tx, f); s_cur_py = lerp(A.fy, A.ty, f);
        more = true;
    } else if ((A.g == G_DROP || A.g == G_POP) && ms < A.gdur) {
        t = (float)ms / A.gdur;
        float sc = A.g == G_DROP ? 0.70f + 0.30f * out_back(t) : 1 - in_cubic(t);
        s_am = (int)(s_cell * sc + 0.5f);
        s_ash = A.g == G_DROP ? (int)(256 * clamp01((t - 0.5f) * 2)) : (int)(256 * (1 - t));
        s_apeg = A.g == G_POP;
        s_acell = UI.cur_y * GRID_MAX + UI.cur_x;
        more = true;
    }
    s_lift_on = false;
    if (A.chips && ms < A.cdur) {
        float f = out_cubic((float)ms / A.cdur);
        for (int i = 0; i < SLOTS; i++) s_lift[i] = lerp(A.lf[i], A.lt[i], f);
        s_lift_on = true;
        more = true;
    }
    s_lvl = -1;
    if (A.fade && ms < A.fdelay + A.fdur) {
        s_lvl = ms < A.fdelay ? A.lfrom : lerp(A.lfrom, A.lto, (float)(ms - A.fdelay) / A.fdur);
        more = true;
    }
    s_ring_y0 = -1;
    if (A.z && ms < A.zdur) more = true;
    if (A.z == Z_HILITE && ms < A.zdur) {
        float f = out_cubic((float)ms / A.zdur);
        s_ring_y0 = lerp(A.hy0[0], A.hy0[1], f); s_ring_y1 = lerp(A.hy1[0], A.hy1[1], f);
    }
    return more;
}

// Redraw everything the running channels touch, with the current overrides; parts
// whose overrides did not change since the last frame are skipped unless `force`.
static void anim_draw(int ms, bool force)
{
    bool grid = false;
    int cs[5] = { 0, s_acell, s_am, s_ash >> 3, cursor_frame() };
    if (A.g == G_GLIDE) {
        int px = s_cur_px >= 0 ? s_cur_px : A.tx, py = s_cur_px >= 0 ? s_cur_py : A.ty;
        cs[1] = px; cs[2] = py;
        if (force || !A.drawn || memcmp(cs, A.cell_sig, sizeof cs)) {
            cursor_rect(A.lx, A.ly);
            if (px != A.lx || py != A.ly) cursor_rect(px, py);
            A.lx = px; A.ly = py;
            grid = true;
        }
    } else if (A.g && (force || !A.drawn || memcmp(cs, A.cell_sig, sizeof cs))) {
        draw_cell(UI.cur_x, UI.cur_y, 0);
        grid = true;
    }
    memcpy(A.cell_sig, cs, sizeof cs);
    int ss[SLOTS + 2] = { s_lvl, s_lift_on };
    for (int i = 0; i < SLOTS && s_lift_on; i++) ss[2 + i] = s_lift[i];
    bool strip = (A.chips || A.fade) && (force || !A.drawn || memcmp(ss, A.strip_sig, sizeof ss));
    bool info = A.fade && (force || !A.drawn || s_lvl != A.info_sig);
    memcpy(A.strip_sig, ss, sizeof ss); A.info_sig = s_lvl; A.drawn = true;
    // The panel takes one dirty rectangle per present: send the cursor area on its
    // own before the strip, or the two make one tall rectangle.
    if (grid && (strip || info) && D->present) D->present();
    if (strip) draw_strip();
    if (info) info_fade_frame();
    if (A.z == Z_HILITE)
        for (int k = 0; k < 2; k++) draw_menu_row(&s_menu_shown, A.hrow[k]);
}

static void anim_clear_overrides(void)
{
    s_cur_px = -1; s_acell = -1; s_lift_on = false; s_lvl = -1; s_ring_y0 = -1;
}

// Forget the animation without drawing (render_full is about to draw everything).
static void anim_cancel(void)
{
#if MOTION_SLOW > 1 || CONFIG_BEAD_PERF_LOG
    if (A.on) ESP_LOGI("motion", "cancelled by a full redraw");
#endif
    anim_clear_overrides();
    memset(&A, 0, sizeof A);
}

static void anim_end(void)
{
#if MOTION_SLOW > 1 || CONFIG_BEAD_PERF_LOG
    ESP_LOGI("motion", "end g=%d z=%d after %lld ms, late %d", A.g, A.z,
             (long long)((esp_timer_get_time() - A.t0) / 1000), A.late);
#endif
    anim_clear_overrides();
    anim_draw(1 << 30, true);
    memset(&A, 0, sizeof A);
}

void render_anim_finish(void)
{
    if (A.on) anim_end();
}

int render_anim_ms(void)
{
    if (!A.on) return -1;
    int64_t left = FRAME_MS - (esp_timer_get_time() - A.last_us) / 1000;
    return left < 1 ? 1 : (int)left;
}

void render_anim_frame(void)
{
    if (!A.on) return;
    int64_t now = esp_timer_get_time();
    A.late = now - A.last_us > OVERRUN_US ? A.late + 1 : 0;
#if MOTION_SLOW > 1 || CONFIG_BEAD_PERF_LOG
    ESP_LOGI("motion", "frame +%lld us", (long long)(now - A.last_us));
#endif
    A.last_us = now;
    int ms = (int)((now - A.t0) / 1000 / MOTION_SLOW);
    if (A.late >= 2 || !anim_set(ms)) { anim_end(); return; }
    anim_draw(ms, false);
}

static void anim_go(void)
{
    A.on = true;
    A.last_us = esp_timer_get_time();
    A.t0 = A.last_us - FRAME_MS * 1000 * MOTION_SLOW;   // the first frame is already 1 frame in
#if MOTION_SLOW > 1 || CONFIG_BEAD_PERF_LOG
    ESP_LOGI("motion", "start g=%d z=%d chips=%d fade=%d", A.g, A.z, A.chips, A.fade);
#endif
    anim_set(FRAME_MS);
}

void render_event(event_t ev) { s_ev = ev; }

// The focus fade has not started yet (it waits for a glide).
static bool motion_fade_held(void) { return A.on && A.fade && A.fdelay > 0 && s_lvl == A.lfrom; }

// ---- starting ------------------------------------------------------------------------
// Drawing screen, called by render_update before it redraws what changed: set up the
// animations for this change and the overrides for their first frame, which
// render_update then draws.
static void motion_draw_start(const shown_t *old, const shown_t *now)
{
    memset(&A, 0, sizeof A);
    if (!UI.motion) return;
    const int64_t t = esp_timer_get_time();
    const event_t ev = s_ev;
    hires_layout();
    bool moved = old->x != now->x || old->y != now->y;
    if (moved && (ev == EV_UP || ev == EV_DOWN || ev == EV_LEFT || ev == EV_RIGHT)) {
        bool wrap = abs(now->x - old->x) > 1 || abs(now->y - old->y) > 1;   // snaps
        if (!wrap && t - s_last_move_us >= GLIDE_MS * 1000) {
            A.g = G_GLIDE; A.gdur = GLIDE_MS;
            cell_pos(old->x, old->y, &A.fx, &A.fy);
            cell_pos(now->x, now->y, &A.tx, &A.ty);
            A.lx = A.fx; A.ly = A.fy;
        }
        s_last_move_us = t;
    } else if (!moved && now->cell != old->cell) {
        if (ev == EV_ENC_PRESS && now->cell != CELL_EMPTY && now->cell < pal()->count) {
            A.g = G_DROP; A.gdur = DROP_MS;
            bead_tones(pal()->colors[now->cell].rgb565, UI.bevel, s_atones);
        } else if (ev == EV_CENTER && now->cell == CELL_EMPTY && old->cell < pal()->count) {
            A.g = G_POP; A.gdur = POP_MS;
            bead_tones(pal()->colors[old->cell].rgb565, UI.bevel, s_atones);
        }
    }
    bool slots_changed = memcmp(old->slots, now->slots, SLOTS) != 0;
    const int full = 256, half = 128;
    if (ev == EV_CENTER_LONG && slots_changed && !now->browsing) {
        A.chips = true; A.cdur = EYEDROP_MS;
        for (int i = 0; i < SLOTS; i++) A.lf[i] = A.lt[i] = i == now->slot ? CHIP_LIFT : 0;
        A.lf[now->slot] = 0;
        A.fade = true; A.fdelay = EYEDROP_MS; A.fdur = FADE_MS;
        A.lfrom = full; A.lto = now->palette_focus ? full : half;
    } else {
        if (!now->browsing && !old->browsing && now->slot != old->slot) {
            A.chips = true; A.cdur = CHIP_MS;
            for (int i = 0; i < SLOTS; i++) {
                A.lf[i] = i == old->slot ? CHIP_LIFT : 0;
                A.lt[i] = i == now->slot ? CHIP_LIFT : 0;
            }
        }
        if (now->palette_focus != old->palette_focus) {
            A.fade = true; A.fdur = FADE_MS;
            A.lfrom = old->palette_focus ? full : half; A.lto = now->palette_focus ? full : half;
        }
    }
    // Glide first, then the strip fades: both in one frame would not fit in 16 ms.
    if (A.g == G_GLIDE && A.fade) A.fdelay = GLIDE_MS;
    if (A.g || A.chips || A.fade) anim_go();
    if (A.g == G_GLIDE) { A.lx = s_cur_px; A.ly = s_cur_py; }   // where render_update draws it
}

// The highlight moved to the next/previous row on the same page: slide the frame.
// Returns true when it drew the first frame (the caller then draws nothing).
static bool motion_hilite(const menu_view_t *old, const menu_view_t *now)
{
    int64_t t = esp_timer_get_time();
    bool fast = t - s_last_hilite_us < HILITE_MS * 1000;
    s_last_hilite_us = t;
    if (!UI.motion || fast || old->sel < 0 || now->sel < 0 || abs(now->sel - old->sel) != 1) return false;
    int ya = row_y(now, old->sel), yb = row_y(now, now->sel);
    if (ya < 0 || yb < 0) return false;
    memset(&A, 0, sizeof A);
    A.z = Z_HILITE; A.zdur = HILITE_MS;
    A.hrow[0] = old->sel; A.hrow[1] = now->sel;
    A.hy0[0] = ya + M_SELY; A.hy1[0] = ya + menu_row_units(old->sel) * ROW_H - M_SELY;
    A.hy0[1] = yb + M_SELY; A.hy1[1] = yb + menu_row_units(now->sel) * ROW_H - M_SELY;
    anim_go();
    for (int k = 0; k < 2; k++) draw_menu_row(now, A.hrow[k]);
    return true;
}

