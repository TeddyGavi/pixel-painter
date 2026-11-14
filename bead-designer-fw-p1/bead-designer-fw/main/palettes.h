#pragma once
#include <stdint.h>

typedef struct {
    const char *name;   // "Magenta" (descriptive; unofficial where the brand has none)
    const char *code;   // brand code, e.g. "S10", "DB-1472-1": what the order report lists
    uint16_t rgb565;
    uint8_t approx;     // 1 = colour sampled from a photo, not manufacturer RGB
    uint8_t family;     // index into FAMILY_NAMES (browse family jump)
} bead_color_t;

#define FAMILY_COUNT 10
extern const char *const FAMILY_NAMES[FAMILY_COUNT];

// Compile-time RGB565 (for generated palette tables).
#define RGB565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

typedef struct {
    const char *brand;  // "Hama" / "Artkal" / "Miyuki"
    const bead_color_t *colors;
    int count;
    const uint8_t *order;   // browse order (grouped by family); NULL = table order
} palette_t;

// Browse helpers: position <-> palette index along the palette's browse order.
int palette_browse_index(const palette_t *p, int pos);   // pos -> colour index
int palette_browse_pos(const palette_t *p, int idx);     // colour index -> pos
// Position of the first colour of the next (dir=+1) / previous (dir=-1) family.
int palette_family_jump(const palette_t *p, int pos, int dir);
// Colour in palette p nearest to an RGB565 colour (RGB distance), for brand switch.
int palette_nearest(const palette_t *p, uint16_t rgb565);
// Colour index for a code ("S01"), or -1.
int palette_find(const palette_t *p, const char *code);

extern const palette_t PALETTES[];
extern const int PALETTE_COUNT;

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{ return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
