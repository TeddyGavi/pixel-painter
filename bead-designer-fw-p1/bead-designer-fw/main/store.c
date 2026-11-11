// Pattern storage in NVS: one blob per slot + an autosave blob so the
// working pattern survives power-off (spec: single working pattern persists;
// multiple named slots via Settings).
#include "store.h"
#include "ui.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include <stdio.h>

static nvs_handle_t s_nvs;

void store_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    nvs_open("beads", NVS_READWRITE, &s_nvs);
}

static bool save_key(const char *key)
{
    if (nvs_set_blob(s_nvs, key, UI.cells, sizeof UI.cells) != ESP_OK) return false;
    return nvs_commit(s_nvs) == ESP_OK;
}

static bool load_key(const char *key)
{
    size_t len = sizeof UI.cells;
    return nvs_get_blob(s_nvs, key, UI.cells, &len) == ESP_OK && len == sizeof UI.cells;
}

bool store_save_slot(int slot)
{ char k[8]; snprintf(k, sizeof k, "s%d", slot); return save_key(k); }

bool store_load_slot(int slot)
{ char k[8]; snprintf(k, sizeof k, "s%d", slot); return load_key(k); }

void store_load_autosave(void) { load_key("auto"); }

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
