#pragma once
#include <stdint.h>

typedef struct {
    const char *name;   // "Magenta"
    const char *code;   // brand code, e.g. "S10"
    uint16_t rgb565;
} bead_color_t;

typedef struct {
    const char *brand;  // "Perler" / "Hama" / "Artkal"
    const bead_color_t *colors;
    int count;
} palette_t;

extern const palette_t PALETTES[];
extern const int PALETTE_COUNT;

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{ return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
