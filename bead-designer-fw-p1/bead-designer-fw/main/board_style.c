// Canvas look (see board_style.h). Tones were chosen from mockups:
//   background    texture #303030 / #606060 over the whole drawing screen
//   solid cells   checker #505050 / #585858 with a 4x4 peg dot #787878
//   open cells    the background texture with a 4x4 peg dot #808080
// (Kept as an alternative the user liked: light cells #707070/#787878, peg #989898.)
// Textures are our own fixed 8x8 one-bit tables (bit 7 = leftmost pixel).
// Each has as many light pixels on even columns as on odd ones, so it never beats
// with the panel's column inversion (set in board_freenove.c) into a full-screen
// shimmer. C and D are also balanced for dot inversion; keep new ones balanced.
#include "board_style.h"
#include "palettes.h"   // RGB565()
#include "ui.h"

const char *const BOARD_NAMES[BOARD__COUNT] = { "Classic", "Solid", "Pegs", "Big pegs" };

static const struct { uint8_t gap; bool open; } VARIANT[BOARD__COUNT] = {
    [BOARD_CLASSIC]  = { 2, false },
    [BOARD_SOLID]    = { 4, false },
    [BOARD_PEGS]     = { 4, true  },
    [BOARD_BIG_PEGS] = { 2, true  },
};

static const uint8_t TEX[TEXTURE_COUNT][8] = {
    { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },   // A 1 px checker
    { 0x33, 0x33, 0xCC, 0xCC, 0x33, 0x33, 0xCC, 0xCC },   // B 2 px checker
    { 0xAA, 0x00, 0x55, 0x00, 0xAA, 0x00, 0x55, 0x00 },   // C Bayer 25%
    { 0xAA, 0x44, 0x55, 0x22, 0xAA, 0x44, 0x55, 0x22 },   // D Bayer 37%
    { 0x88, 0x11, 0x22, 0x44, 0x88, 0x11, 0x22, 0x44 },   // E diagonal weave
    { 0x0E, 0xD6, 0xD8, 0xA6, 0x65, 0x1B, 0x6B, 0x70 },   // F pinwheel
    { 0x08, 0x20, 0x36, 0x9C, 0x39, 0x6C, 0x04, 0x10 },   // G pinwheel
    { 0x7A, 0x95, 0x51, 0x87, 0xE1, 0x8A, 0xA9, 0x5E },   // H pinwheel
};

#define G(v) RGB565(v, v, v)

static int variant(void) { return UI.board < BOARD__COUNT ? UI.board : BOARD_DEFAULT; }

int board_gap(void) { return VARIANT[variant()].gap; }

uint16_t board_px(int x, int y)
{
    int bit = (TEX[UI.texture % TEXTURE_COUNT][y & 7] >> (7 - (x & 7))) & 1;
    return bit ? G(0x60) : G(0x30);
}

uint16_t slot_px(int x, int y, int cx, int cy, int cell)
{
    int c0 = cell / 2 - 2;                                    // 4x4 peg in the centre
    bool peg = cx >= c0 && cx < c0 + 4 && cy >= c0 && cy < c0 + 4;
    if (VARIANT[variant()].open) return peg ? G(0x80) : board_px(x, y);
    if (peg) return G(0x78);
    return ((x + y) & 1) ? G(0x58) : G(0x50);
}

uint16_t bevel_px(uint16_t c, int dir)
{
    int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    if (dir > 0) {                                            // 12% toward white
        // (black -> ~#181C18, so even a black bead gets a faint lit edge)
        r += (31 - r) * 12 / 100; g += (63 - g) * 12 / 100; b += (31 - b) * 12 / 100;
    } else {                                                  // 12% toward black
        r -= r * 12 / 100; g -= g * 12 / 100; b -= b * 12 / 100;
    }
    return (uint16_t)((r << 11) | (g << 5) | b);
}
