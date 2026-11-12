#pragma once
#include <stdint.h>
#include <stdbool.h>

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

// Like text_render, but x may lie outside the buffer: only columns in
// [clip_x0, clip_x1) are written (marquee scrolling). Returns the text width.
int text_render_clip(uint16_t *buf, int stride, int x, int clip_x0, int clip_x1,
                     const char *s, uint16_t fg_be, uint16_t bg_be);

// Copy s into out, shortened with "..." if it is wider than max_w px.
// Returns true if it had to be shortened.
bool text_fit(const char *s, int max_w, char *out, int out_size);
