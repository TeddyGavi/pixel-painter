// Pattern storage in NVS: one blob per save slot + an autosave blob so the
// working pattern survives power-off (spec: single working pattern persists;
// multiple named slots via Settings).
//
// A pattern = brand (palette) + its 5-colour shortlist + the cells, versioned.
// Loading validates everything; anything else (older format, other size, indices
// out of range for the palette) is ignored and the caller's defaults stay.
#include "store.h"
#include "ui.h"
#include "palettes.h"
#include "board_style.h"
#include <stddef.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include <stdio.h>

static nvs_handle_t s_nvs;

static int count_beads(void)
{
    int n = 0;
    for (int i = 0; i < GRID_N * GRID_N; i++) n += UI.cells[i] != CELL_EMPTY;
    return n;
}

void store_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    nvs_open("beads", NVS_READWRITE, &s_nvs);
}

#define PATTERN_VERSION 4   // 1 = bare cells; 2 = + brand/shortlist; 3 = + canvas look;
                            // 4 = same layout, brand indices after Perler was removed
#define FLAG_BEVEL 0x01

typedef struct {
    uint8_t version;
    uint8_t palette;                  // index into PALETTES[]
    uint8_t slots[SLOTS];             // shortlist (palette colour indices)
    uint8_t slot;                     // selected slot
    uint8_t cells[GRID_N * GRID_N];   // palette colour index, CELL_EMPTY = none
    // v3:
    uint8_t board;                    // board_t layout
    uint8_t texture;                  // 0..TEXTURE_COUNT-1
    uint8_t flags;                    // FLAG_BEVEL
} pattern_t;
#define PATTERN_V2_SIZE offsetof(pattern_t, board)   // v2 records end before `board`

static bool save_key(const char *key)
{
    pattern_t pt = { .version = PATTERN_VERSION, .palette = (uint8_t)UI.palette,
                     .slot = (uint8_t)UI.slot, .board = UI.board, .texture = UI.texture,
                     .flags = UI.bevel ? FLAG_BEVEL : 0 };
    memcpy(pt.slots, UI.slots, sizeof pt.slots);
    memcpy(pt.cells, UI.cells, sizeof pt.cells);
    if (nvs_set_blob(s_nvs, key, &pt, sizeof pt) != ESP_OK) return false;
    return nvs_commit(s_nvs) == ESP_OK;
}

static bool valid(const pattern_t *pt)
{
    if (pt->palette >= PALETTE_COUNT || pt->slot >= SLOTS) return false;
    if (pt->board >= BOARD__COUNT || pt->texture >= TEXTURE_COUNT) return false;
    int n = PALETTES[pt->palette].count;
    for (int i = 0; i < SLOTS; i++) if (pt->slots[i] >= n) return false;
    for (int i = 0; i < GRID_N * GRID_N; i++)
        if (pt->cells[i] != CELL_EMPTY && pt->cells[i] >= n) return false;
    return true;
}

static bool load_key(const char *key)
{
    pattern_t pt;
    size_t len = sizeof pt;
    if (nvs_get_blob(s_nvs, key, &pt, &len) != ESP_OK) return false;
    if (pt.version == 2 && len == PATTERN_V2_SIZE) {        // migrate: default look
        pt.board = BOARD_DEFAULT; pt.texture = 0; pt.flags = FLAG_BEVEL;
    } else if (!((pt.version == 3 || pt.version == PATTERN_VERSION) && len == sizeof pt)) {
        return false;
    }
    if (pt.version < 4) {          // v2/v3 brands: 0 Perler (removed), 1 Hama, 2 Artkal
        if (pt.palette == 0) return false;
        pt.palette -= 1;
    }
    if (!valid(&pt)) return false;
    UI.palette = pt.palette;
    UI.slot = pt.slot;
    memcpy(UI.slots, pt.slots, sizeof UI.slots);
    memcpy(UI.cells, pt.cells, sizeof UI.cells);
    UI.board = pt.board;
    UI.texture = pt.texture;
    UI.bevel = (pt.flags & FLAG_BEVEL) != 0;
    return true;
}

bool store_save_slot(int slot)
{ char k[8]; snprintf(k, sizeof k, "s%d", slot); return save_key(k); }

bool store_load_slot(int slot)
{ char k[8]; snprintf(k, sizeof k, "s%d", slot); return load_key(k); }

void store_load_autosave(void)
{
    if (load_key("auto"))
        ESP_LOGI("store", "pattern restored: %s, %d beads, slot %d",
                 PALETTES[UI.palette].brand, count_beads(), UI.slot);
    else
        ESP_LOGI("store", "no valid saved pattern (new, or an older format): defaults");
}

#define AUTOSAVE_IDLE_US 1500000   // save 1.5 s after the last edit

static bool s_dirty;
static int64_t s_edit_us;

void store_mark_dirty(void) { s_dirty = true; s_edit_us = esp_timer_get_time(); }
bool store_pending(void)    { return s_dirty; }

void store_flush(void)
{
    if (s_dirty && save_key("auto")) s_dirty = false;
}

void store_autosave_if_idle(void)
{
    if (s_dirty && esp_timer_get_time() - s_edit_us >= AUTOSAVE_IDLE_US) store_flush();
}
