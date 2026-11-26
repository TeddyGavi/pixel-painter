// Grid size changes for the working pattern (see grid.h).
#include "grid.h"
#include "ui.h"
#include "store.h"
#include <string.h>
#include <stdlib.h>

// Placement of the old board in the new one: new (x, y) = old (x + ox, y + oy).
// Growing: ox = oy = -(n - old) / 2 (centred). Shrinking: the n x n window at
// (ox, oy) of the old board that keeps the most beads.
static int window_beads(const uint8_t *c, int on, int wx, int wy, int n)
{
    int k = 0;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) k += c[(wy + y) * on + wx + x] != CELL_EMPTY;
    return k;
}

static int place(int n, int *ox, int *oy)            // returns beads lost
{
    const int on = UI.n;
    if (n >= on) { *ox = *oy = -((n - on) / 2); return 0; }
    int total = window_beads(UI.cells, on, 0, 0, on), c = (on - n) / 2;
    int best = -1, bd = 0;
    for (int wy = 0; wy <= on - n; wy++)
        for (int wx = 0; wx <= on - n; wx++) {
            int k = window_beads(UI.cells, on, wx, wy, n);
            int d = abs(wx - c) + abs(wy - c);
            if (k > best || (k == best && d < bd)) { best = k; bd = d; *ox = wx; *oy = wy; }
        }
    return total - best;
}

static void resized(int n, int ox, int oy, uint8_t *dst)
{
    memset(dst, CELL_EMPTY, GRID_MAX * GRID_MAX);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            int sx = x + ox, sy = y + oy;
            if (sx >= 0 && sy >= 0 && sx < UI.n && sy < UI.n) dst[y * n + x] = UI.cells[sy * UI.n + sx];
        }
}

// Before the last resize (bk_*) and right after it (post_*). The backup is good while
// the working pattern is still exactly the resized one: any bead change, load, reset
// or brand change makes them differ.
static struct {
    bool valid;
    int n, pal, cx, cy;
    uint8_t cells[GRID_MAX * GRID_MAX];
    int post_n;
    uint8_t post[GRID_MAX * GRID_MAX];
} s_bk;

static bool can_restore(int n)
{
    return s_bk.valid && s_bk.n == n && s_bk.post_n == UI.n && s_bk.pal == UI.palette &&
           memcmp(s_bk.post, UI.cells, sizeof s_bk.post) == 0;
}

grid_plan_t grid_plan(int n)
{
    grid_plan_t p = { .n = n };
    if (n == UI.n) return p;
    if (can_restore(n)) { p.restore = true; return p; }
    int ox, oy;
    p.lost = place(n, &ox, &oy);
    return p;
}

void grid_preview(int n, uint8_t *dst)
{
    if (n == UI.n) { memcpy(dst, UI.cells, GRID_MAX * GRID_MAX); return; }
    if (can_restore(n)) { memcpy(dst, s_bk.cells, GRID_MAX * GRID_MAX); return; }
    int ox, oy;
    place(n, &ox, &oy);
    resized(n, ox, oy, dst);
}

static int clamp(int v, int n) { return v < 0 ? 0 : v >= n ? n - 1 : v; }

void grid_apply(int n)
{
    if (n == UI.n || grid_size(grid_size_index(n)) != n) return;
    uint8_t next[GRID_MAX * GRID_MAX];
    int cx, cy;
    if (can_restore(n)) {
        memcpy(next, s_bk.cells, sizeof next);
        cx = s_bk.cx; cy = s_bk.cy;
    } else {
        int ox, oy;
        place(n, &ox, &oy);
        resized(n, ox, oy, next);
        cx = clamp(UI.cur_x - ox, n); cy = clamp(UI.cur_y - oy, n);
    }
    // The board as it is now becomes the way back.
    s_bk.valid = true; s_bk.n = UI.n; s_bk.pal = UI.palette;
    s_bk.cx = UI.cur_x; s_bk.cy = UI.cur_y;
    memcpy(s_bk.cells, UI.cells, sizeof s_bk.cells);
    memcpy(UI.cells, next, sizeof next);
    UI.n = n; UI.cur_x = cx; UI.cur_y = cy;
    s_bk.post_n = n;
    memcpy(s_bk.post, UI.cells, sizeof s_bk.post);
    UI.unsaved = true;
    store_mark_dirty();
}
