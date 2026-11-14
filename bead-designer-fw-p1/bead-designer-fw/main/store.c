// Pattern storage in NVS.
//   "auto"     the working pattern, autosaved after edits (survives power-off)
//   "s0".."s7" save slots: snapshots of the working pattern (Pattern > Save / Load)
//   "look"     device-wide display settings (board style, texture, bevel, screen saver)
//
// A pattern = brand (palette) + its 5-colour shortlist + the cells, versioned. The
// working pattern also records whether its beads changed since the last Save/Load,
// so Load/Reset can warn about unsaved work even after a power cycle.
// Loading validates everything; anything else (unknown format, indices out of range
// for the palette) is ignored and the caller's defaults stay.
#include "store.h"
#include "ui.h"
#include "palettes.h"
#include "board_style.h"
#include "saver.h"
#include <stddef.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include <stdio.h>

static const char *TAG = "store";
static nvs_handle_t s_nvs;

static int count_beads(const uint8_t *cells)
{
    int n = 0;
    for (int i = 0; i < GRID_N * GRID_N; i++) n += cells[i] != CELL_EMPTY;
    return n;
}

// ---- record formats ---------------------------------------------------------------

#define PATTERN_VERSION 6   // 1 = bare cells; 2 = + brand/shortlist; 3 = + canvas look;
                            // 4 = brand indices after Perler was removed;
                            // 5 = Hama = full 88-colour chart (was 6);
                            // 6 = look moved to its own record; + unsaved flag
#define PFLAG_UNSAVED 0x01

typedef struct {
    uint8_t version;
    uint8_t palette;                  // index into PALETTES[]
    uint8_t slots[SLOTS];             // shortlist (palette colour indices)
    uint8_t slot;                     // selected slot
    uint8_t cells[GRID_N * GRID_N];   // palette colour index, CELL_EMPTY = none
    uint8_t flags;                    // PFLAG_UNSAVED (working pattern only)
} pattern_t;

typedef struct {                      // v2..v5 layout (v2 ends before `board`)
    uint8_t version, palette, slots[SLOTS], slot, cells[GRID_N * GRID_N];
    uint8_t board, texture, flags;    // v3+: canvas look, flags bit0 = bevel
} pattern_old_t;
#define PATTERN_V2_SIZE offsetof(pattern_old_t, board)

#define LOOK_VERSION 2   // 2 = + screen saver timeout
#define LFLAG_BEVEL 0x01
typedef struct { uint8_t version, board, texture, flags, saver; } look_t;
#define LOOK_V1_SIZE offsetof(look_t, saver)

// Look found in a pre-v6 autosave, used once if there is no "look" record yet.
static bool s_old_look_valid;
static look_t s_old_look;

static bool valid(const pattern_t *pt)
{
    if (pt->palette >= PALETTE_COUNT || pt->slot >= SLOTS) return false;
    int n = PALETTES[pt->palette].count;
    for (int i = 0; i < SLOTS; i++) if (pt->slots[i] >= n) return false;
    for (int i = 0; i < GRID_N * GRID_N; i++)
        if (pt->cells[i] != CELL_EMPTY && pt->cells[i] >= n) return false;
    return true;
}

// Before v5, Hama was a 6-colour starter table; map its indices to the full chart by
// code (the starter's "H01".. are the chart's "01"..).
static bool remap_old_hama(pattern_t *pt)
{
    static const char *const OLD[] = { "01", "18", "08", "03", "06", "09" };
    int map[6];
    for (int i = 0; i < 6; i++)
        if ((map[i] = palette_find(&PALETTES[0], OLD[i])) < 0) return false;
    for (int i = 0; i < SLOTS; i++) {
        if (pt->slots[i] >= 6) return false;
        pt->slots[i] = (uint8_t)map[pt->slots[i]];
    }
    for (int i = 0; i < GRID_N * GRID_N; i++) {
        if (pt->cells[i] == CELL_EMPTY) continue;
        if (pt->cells[i] >= 6) return false;
        pt->cells[i] = (uint8_t)map[pt->cells[i]];
    }
    return true;
}

// Read and migrate any known version into the current pattern_t.
static bool read_pattern(const char *key, pattern_t *pt)
{
    union { pattern_t now; pattern_old_t old; } b;
    size_t len = sizeof b;
    if (nvs_get_blob(s_nvs, key, &b, &len) != ESP_OK) return false;
    if (b.now.version == PATTERN_VERSION && len == sizeof(pattern_t)) {
        *pt = b.now;
        return valid(pt);
    }
    const pattern_old_t *o = &b.old;
    bool v2 = o->version == 2 && len == PATTERN_V2_SIZE;
    bool v35 = o->version >= 3 && o->version <= 5 && len == sizeof(pattern_old_t);
    if (!v2 && !v35) return false;
    memset(pt, 0, sizeof *pt);
    pt->version = PATTERN_VERSION;
    pt->palette = o->palette;
    pt->slot = o->slot;
    memcpy(pt->slots, o->slots, sizeof pt->slots);
    memcpy(pt->cells, o->cells, sizeof pt->cells);
    if (o->version < 4) {          // v2/v3 brands: 0 Perler (removed), 1 Hama, 2 Artkal
        if (pt->palette == 0) return false;
        pt->palette -= 1;
    }
    if (o->version < 5 && pt->palette == 0 && !remap_old_hama(pt)) return false;
    if (!valid(pt)) return false;
    // Older working patterns: beads count as unsaved (nothing was ever in a slot).
    if (count_beads(pt->cells)) pt->flags |= PFLAG_UNSAVED;
    if (v35 && strcmp(key, "auto") == 0 && o->board < BOARD__COUNT && o->texture < TEXTURE_COUNT) {
        s_old_look = (look_t){ LOOK_VERSION, o->board, o->texture, (uint8_t)(o->flags & 1), 0 };
        s_old_look_valid = true;
    }
    return true;
}

static bool write_blob(const char *key, const void *p, size_t n)
{
    if (nvs_set_blob(s_nvs, key, p, n) != ESP_OK) return false;
    return nvs_commit(s_nvs) == ESP_OK;
}

static void from_ui(pattern_t *pt)
{
    memset(pt, 0, sizeof *pt);
    pt->version = PATTERN_VERSION;
    pt->palette = (uint8_t)UI.palette;
    pt->slot = (uint8_t)UI.slot;
    memcpy(pt->slots, UI.slots, sizeof pt->slots);
    memcpy(pt->cells, UI.cells, sizeof pt->cells);
    pt->flags = UI.unsaved ? PFLAG_UNSAVED : 0;
}

static void to_ui(const pattern_t *pt)
{
    UI.palette = pt->palette;
    UI.slot = pt->slot;
    memcpy(UI.slots, pt->slots, sizeof UI.slots);
    memcpy(UI.cells, pt->cells, sizeof UI.cells);
    UI.unsaved = (pt->flags & PFLAG_UNSAVED) != 0;
}

// ---- save slots (cached in RAM so the slot lists draw without flash reads) -----------

static pattern_t s_slot[STORE_SLOTS];
static bool s_slot_used[STORE_SLOTS];

static void slot_key(int i, char *k) { snprintf(k, 8, "s%d", i); }

bool store_slot_info(int i, int *palette, const uint8_t **cells, int *beads)
{
    if (i < 0 || i >= STORE_SLOTS || !s_slot_used[i]) return false;
    if (palette) *palette = s_slot[i].palette;
    if (cells) *cells = s_slot[i].cells;
    if (beads) *beads = count_beads(s_slot[i].cells);
    return true;
}

bool store_save_slot(int i)
{
    if (i < 0 || i >= STORE_SLOTS) return false;
    char k[8]; slot_key(i, k);
    pattern_t pt;
    from_ui(&pt);
    pt.flags = 0;
    if (!write_blob(k, &pt, sizeof pt)) { ESP_LOGE(TAG, "save to slot %d failed", i + 1); return false; }
    s_slot[i] = pt; s_slot_used[i] = true;
    UI.unsaved = false;
    store_mark_dirty();                // persist the cleared flag with the working copy
    ESP_LOGI(TAG, "saved slot %d: %s, %d beads", i + 1, PALETTES[pt.palette].brand, count_beads(pt.cells));
    return true;
}

bool store_load_slot(int i)
{
    if (i < 0 || i >= STORE_SLOTS || !s_slot_used[i]) return false;
    pattern_t pt = s_slot[i];
    pt.flags = 0;
    to_ui(&pt);
    store_mark_dirty();
    ESP_LOGI(TAG, "loaded slot %d: %s, %d beads", i + 1, PALETTES[pt.palette].brand, count_beads(pt.cells));
    return true;
}

// ---- init / autosave ----------------------------------------------------------------

void store_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    nvs_open("beads", NVS_READWRITE, &s_nvs);
    for (int i = 0; i < STORE_SLOTS; i++) {
        char k[8]; slot_key(i, k);
        s_slot_used[i] = read_pattern(k, &s_slot[i]);
    }
}

static bool save_look(void)
{
    look_t l = { LOOK_VERSION, UI.board, UI.texture, (uint8_t)(UI.bevel ? LFLAG_BEVEL : 0), UI.saver };
    return write_blob("look", &l, sizeof l);
}

void store_load_autosave(void)
{
    pattern_t pt;
    if (read_pattern("auto", &pt)) {
        to_ui(&pt);
        ESP_LOGI(TAG, "pattern restored: %s, %d beads, slot %d%s", PALETTES[UI.palette].brand,
                 count_beads(UI.cells), UI.slot, UI.unsaved ? ", unsaved" : "");
    } else {
        ESP_LOGI(TAG, "no valid saved pattern (new, or an unknown format): defaults");
    }
    look_t l; size_t len = sizeof l;
    bool v2 = false, v1 = false;
    if (nvs_get_blob(s_nvs, "look", &l, &len) == ESP_OK) {
        v2 = l.version == LOOK_VERSION && len == sizeof l && l.saver < SAVER_CHOICES;
        v1 = l.version == 1 && len == LOOK_V1_SIZE;
    }
    if ((v1 || v2) && l.board < BOARD__COUNT && l.texture < TEXTURE_COUNT) {
        UI.board = l.board; UI.texture = l.texture; UI.bevel = (l.flags & LFLAG_BEVEL) != 0;
        if (v2) UI.saver = l.saver;
        else save_look();                           // v1: add the saver default
    } else if (s_old_look_valid) {                  // migrate from a pre-v6 autosave
        UI.board = s_old_look.board; UI.texture = s_old_look.texture;
        UI.bevel = (s_old_look.flags & LFLAG_BEVEL) != 0;
        save_look();
    }
}

#define AUTOSAVE_IDLE_US 1500000   // save 1.5 s after the last edit

static bool s_dirty, s_look_dirty;
static int64_t s_edit_us;

void store_mark_dirty(void) { s_dirty = true; s_edit_us = esp_timer_get_time(); }
void store_mark_look_dirty(void) { s_look_dirty = true; s_edit_us = esp_timer_get_time(); }
bool store_pending(void)    { return s_dirty || s_look_dirty; }

void store_flush(void)
{
    if (s_dirty) {
        pattern_t pt;
        from_ui(&pt);
        if (write_blob("auto", &pt, sizeof pt)) s_dirty = false;
    }
    if (s_look_dirty && save_look()) s_look_dirty = false;
}

void store_autosave_if_idle(void)
{
    if (store_pending() && esp_timer_get_time() - s_edit_us >= AUTOSAVE_IDLE_US) store_flush();
}
