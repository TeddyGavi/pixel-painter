// Text rendering into an off-screen buffer (see text.h). Nothing here talks to the
// display: the caller composes a whole line and pushes it with one blit.
// With TEXT_SMOOTH the string is first laid out at 1x in a bit mask, then scaled 3x
// with Scale3x (AdvMAME3x) over the whole string, as in the mockups
// (docs/ui-update-2.41/scripts/gfx.py); otherwise each font pixel is a plain block.
#include "text.h"
#include "font_chicago.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static_assert(TEXT_H == CHICAGO_H * TEXT_SCALE, "TEXT_H must match the font");

static const chicago_glyph_t *glyph(char ch)
{
    unsigned c = (unsigned char)ch;
    return &CHICAGO_GLYPHS[(c >= 32 && c <= 126 ? c : '?') - 32];
}

int text_width(const char *s)
{
    int w = 0;
    for (; *s; s++) w += glyph(*s)->adv * TEXT_SCALE;
    return w;
}

#if TEXT_SMOOTH
// 1x mask of the string being drawn: CHICAGO_H rows of MASK_W bits.
#define MASK_W 1024
static uint8_t s_mask[CHICAGO_H][MASK_W / 8];
static int s_mask_w;

static void mask_build(const char *s, int max_glyph_px)
{
    memset(s_mask, 0, sizeof s_mask);
    int x = 0;
    for (; *s; s++) {
        const chicago_glyph_t *g = glyph(*s);
        if (x + g->adv > max_glyph_px || x + g->adv > MASK_W) break;
        for (int r = 0; r < CHICAGO_H; r++)
            for (int c = 0; c < g->adv; c++)
                if ((g->rows[r] >> c) & 1) s_mask[r][(x + c) >> 3] |= (uint8_t)(1u << ((x + c) & 7));
        x += g->adv;
    }
    s_mask_w = x;
}

static inline int M(int x, int y)
{
    if (x < 0 || y < 0 || x >= s_mask_w || y >= CHICAGO_H) return 0;
    return (s_mask[y][x >> 3] >> (x & 7)) & 1;
}

// Write output columns [ox0, ox1) of the 3x string, placed at buffer column x.
// Scale3x works per font pixel E (neighbours A B C / D E F / G H I) and yields its
// 3x3 output block e[0..8].
static void put3(uint16_t *buf, int stride, int x, int ox0, int ox1, uint16_t fg_be, uint16_t bg_be)
{
    if (ox0 >= ox1) return;
    for (int y = 0; y < CHICAGO_H; y++)
        for (int sx = ox0 / 3; sx <= (ox1 - 1) / 3; sx++) {
            int E = M(sx, y), B = M(sx, y - 1), D = M(sx - 1, y), F = M(sx + 1, y), H = M(sx, y + 1);
            uint8_t e[9];
            if (B != H && D != F) {
                int A = M(sx - 1, y - 1), C = M(sx + 1, y - 1), G = M(sx - 1, y + 1), I = M(sx + 1, y + 1);
                e[0] = D == B ? D : E;
                e[1] = ((D == B && E != C) || (B == F && E != A)) ? B : E;
                e[2] = B == F ? F : E;
                e[3] = ((D == B && E != G) || (D == H && E != A)) ? D : E;
                e[4] = E;
                e[5] = ((B == F && E != I) || (H == F && E != C)) ? F : E;
                e[6] = D == H ? D : E;
                e[7] = ((D == H && E != I) || (H == F && E != G)) ? H : E;
                e[8] = H == F ? F : E;
            } else {
                memset(e, E, sizeof e);
            }
            for (int j = 0; j < 3; j++) {
                uint16_t *row = buf + (y * 3 + j) * stride;
                for (int i = 0; i < 3; i++) {
                    int ox = sx * 3 + i;
                    if (ox >= ox0 && ox < ox1) row[x + ox] = e[j * 3 + i] ? fg_be : bg_be;
                }
            }
        }
}

int text_render(uint16_t *buf, int stride, int x, const char *s, int max_w,
                uint16_t fg_be, uint16_t bg_be)
{
    mask_build(s, max_w / TEXT_SCALE);
    int w = s_mask_w * TEXT_SCALE;
    put3(buf, stride, x, 0, w, fg_be, bg_be);
    return w;
}

int text_render_clip(uint16_t *buf, int stride, int x, int clip_x0, int clip_x1,
                     const char *s, uint16_t fg_be, uint16_t bg_be)
{
    mask_build(s, MASK_W);
    int w = s_mask_w * TEXT_SCALE;
    int ox0 = clip_x0 - x, ox1 = clip_x1 - x;
    if (ox0 < 0) ox0 = 0;
    if (ox1 > w) ox1 = w;
    if (ox0 < ox1) put3(buf, stride, x, ox0, ox1, fg_be, bg_be);
    return text_width(s);
}
#else
int text_render(uint16_t *buf, int stride, int x, const char *s, int max_w,
                uint16_t fg_be, uint16_t bg_be)
{
    int drawn = 0;
    for (; *s; s++) {
        const chicago_glyph_t *g = glyph(*s);
        int gw = g->adv * TEXT_SCALE;
        if (drawn + gw > max_w) break;
        for (int r = 0; r < TEXT_H; r++) {
            uint16_t bits = g->rows[r / TEXT_SCALE];
            uint16_t *row = buf + r * stride + x + drawn;
            for (int c = 0; c < gw; c++)          // bit 0 = leftmost pixel
                row[c] = ((bits >> (c / TEXT_SCALE)) & 1) ? fg_be : bg_be;
        }
        drawn += gw;
    }
    return drawn;
}

int text_render_clip(uint16_t *buf, int stride, int x, int clip_x0, int clip_x1,
                     const char *s, uint16_t fg_be, uint16_t bg_be)
{
    int pen = x;
    for (; *s; s++) {
        const chicago_glyph_t *g = glyph(*s);
        int gw = g->adv * TEXT_SCALE;
        if (pen + gw > clip_x0 && pen < clip_x1) {
            for (int r = 0; r < TEXT_H; r++) {
                uint16_t bits = g->rows[r / TEXT_SCALE];
                uint16_t *row = buf + r * stride;
                for (int c = 0; c < gw; c++) {
                    int px = pen + c;
                    if (px < clip_x0 || px >= clip_x1) continue;
                    row[px] = ((bits >> (c / TEXT_SCALE)) & 1) ? fg_be : bg_be;
                }
            }
        }
        pen += gw;
    }
    return pen - x;
}
#endif

bool text_fit(const char *s, int max_w, char *out, int out_size)
{
    if (text_width(s) <= max_w) {
        snprintf(out, out_size, "%s", s);
        return false;
    }
    int dots = text_width("...");
    int w = 0, n = 0;
    while (s[n] && n < out_size - 4) {
        int gw = glyph(s[n])->adv * TEXT_SCALE;
        if (w + gw + dots > max_w) break;
        w += gw; n++;
    }
    while (n > 0 && s[n - 1] == ' ') n--;            // no "Light ..."
    memcpy(out, s, n);
    memcpy(out + n, "...", 4);
    return true;
}
