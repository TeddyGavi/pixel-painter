// Bead palettes, const data in flash, all generated from palettes/*.tsv (see
// palettes/README.md): Hama midi (88), Artkal S (225), Miyuki Delica (254 curated),
// Toho Round 11/0 + 8/0 (207, card 111-1), Preciosa Ornela rocailles (254 curated).
#include "palettes.h"
#include "palette_hama.h"            // generated: palettes/hama.tsv
#include "palette_artkal_s.h"        // generated: palettes/artkal_s.tsv
#include "palette_miyuki_delica.h"   // generated: palettes/miyuki_delica.tsv
#include "palette_toho_round.h"      // generated: palettes/toho_round.tsv
#include "palette_preciosa_rocailles.h" // generated: palettes/preciosa_rocailles.tsv
#include <string.h>
#include <stddef.h>

// Same list as palettes/colorname.py FAMILIES.
const char *const FAMILY_NAMES[FAMILY_COUNT] = {
    "Neutrals", "Browns", "Reds", "Oranges", "Yellows", "Greens", "Teals",
    "Blues", "Purples", "Pinks",
};

// Index order is stored in saved patterns (store.c): append new brands at the end,
// and bump PATTERN_VERSION with a remap if this order ever changes.
const palette_t PALETTES[] = {
    { "Hama",   "Hama",   "hama",   HAMA,          HAMA_COUNT,          HAMA_ORDER },
    { "Artkal", "Artkal", "artkal", ARTKAL_S,      ARTKAL_S_COUNT,      ARTKAL_S_ORDER },
    { "Miyuki", "Miyuki", "miyuki", MIYUKI_DELICA, MIYUKI_DELICA_COUNT, MIYUKI_DELICA_ORDER },
    { "Toho",   "Toho",   "toho",   TOHO_ROUND,    TOHO_ROUND_COUNT,    TOHO_ROUND_ORDER },
    // Czech seed beads: the common name in brackets, since buyers search for "Czech beads"
    { "Preciosa", "Preciosa Ornela (Czech beads)", "preciosa",
      PRECIOSA_ROCAILLES, PRECIOSA_ROCAILLES_COUNT, PRECIOSA_ROCAILLES_ORDER },
};
const int PALETTE_COUNT = sizeof PALETTES / sizeof *PALETTES;

int palette_browse_index(const palette_t *p, int pos)
{
    return p->order ? p->order[pos] : pos;
}

int palette_browse_pos(const palette_t *p, int idx)
{
    if (!p->order) return idx;
    for (int i = 0; i < p->count; i++) if (p->order[i] == idx) return i;
    return 0;
}

int palette_family_jump(const palette_t *p, int pos, int dir)
{
    const int n = p->count;
    int fam = p->colors[palette_browse_index(p, pos)].family;
    if (dir > 0) {                     // forward to the first colour of a new family
        for (int k = 1; k < n; k++) {
            int q = (pos + k) % n;
            if (p->colors[palette_browse_index(p, q)].family != fam) return q;
        }
        return pos;
    }
    // backward: to the start of this family, or of the previous one if already there
    int start = pos;
    while (p->colors[palette_browse_index(p, (start - 1 + n) % n)].family == fam &&
           (start - 1 + n) % n != pos) start = (start - 1 + n) % n;
    if (start != pos) return start;
    int q = (pos - 1 + n) % n, f2 = p->colors[palette_browse_index(p, q)].family;
    while (p->colors[palette_browse_index(p, (q - 1 + n) % n)].family == f2 &&
           (q - 1 + n) % n != pos) q = (q - 1 + n) % n;
    return q;
}

int palette_find(const palette_t *p, const char *code)
{
    for (int i = 0; i < p->count; i++) if (strcmp(p->colors[i].code, code) == 0) return i;
    return -1;
}

static void rgb888(uint16_t c, int *r, int *g, int *b)
{
    *r = ((c >> 11) & 0x1F) * 255 / 31;
    *g = ((c >> 5) & 0x3F) * 255 / 63;
    *b = (c & 0x1F) * 255 / 31;
}

int palette_nearest(const palette_t *p, uint16_t rgb565)
{
    int r, g, b, best = 0;
    long bestd = -1;
    rgb888(rgb565, &r, &g, &b);
    for (int i = 0; i < p->count; i++) {
        int r2, g2, b2;
        rgb888(p->colors[i].rgb565, &r2, &g2, &b2);
        // weighted RGB distance (rough perceptual weighting, cheap)
        long d = 3L * (r - r2) * (r - r2) + 4L * (g - g2) * (g - g2) + 2L * (b - b2) * (b - b2);
        if (bestd < 0 || d < bestd) { bestd = d; best = i; }
    }
    return best;
}
