// Text rendering into an off-screen buffer (see text.h). Nothing here talks to the
// display: the caller composes a whole line and pushes it with one blit.
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
