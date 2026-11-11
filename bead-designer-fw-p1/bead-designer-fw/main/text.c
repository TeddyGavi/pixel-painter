// Text rendering into an off-screen buffer (see text.h). Nothing here talks to the
// display: the caller composes a whole line and pushes it with one blit.
#include "text.h"
#include "font_chicago.h"
#include <assert.h>

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
