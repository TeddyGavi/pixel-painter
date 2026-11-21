#pragma once
#include <stdint.h>
// Chicago Kare bitmap font (MIT, see font_chicago.c). Proportional, 1 bit per pixel.
#define CHICAGO_H        13   // rows per glyph
#define CHICAGO_BASELINE 10   // rows above the baseline
#define CHICAGO_MAX_W    12   // widest advance, px
typedef struct {
    uint8_t  adv;                 // advance width in px (ink + spacing)
    uint16_t rows[CHICAGO_H];     // top row first; bit 0 = leftmost pixel
} chicago_glyph_t;
extern const chicago_glyph_t CHICAGO_GLYPHS[95];  // ASCII 32..126
