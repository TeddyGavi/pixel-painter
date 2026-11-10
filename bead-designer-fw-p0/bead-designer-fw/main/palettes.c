// Starter palettes only. P1 TODO: full Perler / Hama / Artkal tables from
// published bead colour charts (agent research task), stored here as const
// data in flash. RGB values below include the KandiPad tile colours so the
// reference pattern renders correctly from day one.
#include "palettes.h"

#define C(n, code, r, g, b) { n, code, (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3)) }

static const bead_color_t ARTKAL_S[] = {
    C("White",     "S01", 0xF5, 0xF5, 0xF5),
    C("Black",     "S02", 0x23, 0x23, 0x23),
    C("Dark Blue", "S35", 0x33, 0x46, 0xB0),
    C("Corn",      "S48", 0xF2, 0xC2, 0x30),
    C("Magenta",   "S24", 0xF0, 0x28, 0x7A),
    C("Baby Blue", "S10", 0x35, 0xB4, 0xE6),
};

static const bead_color_t PERLER[] = {
    C("White",       "P01", 0xF2, 0xF2, 0xF2),
    C("Black",       "P18", 0x2B, 0x28, 0x29),
    C("Dark Blue",   "P08", 0x2A, 0x3B, 0x8F),
    C("Yellow",      "P03", 0xF5, 0xC8, 0x2B),
    C("Magenta",     "P38", 0xE5, 0x3C, 0x7E),
    C("Light Blue",  "P09", 0x4F, 0xAD, 0xD9),
};

static const bead_color_t HAMA[] = {
    C("White",      "H01", 0xF4, 0xF4, 0xF4),
    C("Black",      "H18", 0x26, 0x26, 0x26),
    C("Blue",       "H08", 0x2F, 0x3E, 0x9E),
    C("Yellow",     "H03", 0xF6, 0xC4, 0x33),
    C("Pink",       "H06", 0xE8, 0x3E, 0x83),
    C("Light Blue", "H09", 0x46, 0xA8, 0xDE),
};

const palette_t PALETTES[] = {
    { "Perler", PERLER,   sizeof PERLER   / sizeof *PERLER },
    { "Hama",   HAMA,     sizeof HAMA     / sizeof *HAMA },
    { "Artkal", ARTKAL_S, sizeof ARTKAL_S / sizeof *ARTKAL_S },
};
const int PALETTE_COUNT = sizeof PALETTES / sizeof *PALETTES;
