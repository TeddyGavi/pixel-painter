#pragma once
#include <stdint.h>

// Text in the Chicago bitmap font (proportional) at 2x scale.
#define TEXT_SCALE 2
#define TEXT_H     (13 * TEXT_SCALE)   // CHICAGO_H rows x scale

int text_width(const char *s);

// Renders s into an off-screen RGB565 buffer (big-endian pixels, as blit expects)
// that is TEXT_H rows tall with the given stride, starting at column x and stopping
// before column x + max_w. Only glyph cells are written; fill the background first.
// Returns the width rendered. The caller sends the whole buffer in one blit, so
// the panel never shows text half updated.
int text_render(uint16_t *buf, int stride, int x, const char *s, int max_w,
                uint16_t fg_be, uint16_t bg_be);
