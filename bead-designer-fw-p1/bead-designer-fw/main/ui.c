// Mode state machine. Mapping = spec Rev A.5 section 7 (see README table).
// Cursor wraps in every mode.
#include "ui.h"
#include "input.h"
#include "render.h"
#include "store.h"
#include "export.h"
#include "palettes.h"
#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ui";
#define AUTOSAVE_QUIET_US 1000000   // no input for 1 s before an autosave may run
// Bench debugging: one log line per input event (see BENCH.md).
static const char *const EV_NAME[] = {
    "ENC_L_CW", "ENC_L_CCW", "ENC_R_CW", "ENC_R_CCW",
    "PUSH_L", "PUSH_R", "BTN_MENU", "BTN_SIDE", "TICK", "TEST_BARS",
};

ui_state_t UI = { .cur_x = 4, .cur_y = 4, .palette = 2 /* Artkal */ };

static inline int wrap(int v) { return (v + GRID_N) % GRID_N; }

// Change the cell under the cursor; only a real change schedules an autosave.
static void set_cell(uint8_t v)
{
    uint8_t *c = &UI.cells[UI.cur_y * GRID_N + UI.cur_x];
    if (*c != v) { *c = v; store_mark_dirty(); }
}

static void draw_mode(event_t ev)
{
#ifdef SINGLE_ENCODER
    // Interim board: one knob walks all 81 cells row by row.
    if (ev == EV_ENC_L_CW || ev == EV_ENC_L_CCW) {
        int i = UI.cur_y * GRID_N + UI.cur_x + (ev == EV_ENC_L_CW ? 1 : -1);
        i = (i + GRID_N * GRID_N) % (GRID_N * GRID_N);
        UI.cur_x = i % GRID_N; UI.cur_y = i / GRID_N;
        return;
    }
#endif
    switch (ev) {
    case EV_ENC_L_CW:  UI.cur_x = wrap(UI.cur_x + 1); break;
    case EV_ENC_L_CCW: UI.cur_x = wrap(UI.cur_x - 1); break;
    case EV_ENC_R_CW:  UI.cur_y = wrap(UI.cur_y + 1); break;
    case EV_ENC_R_CCW: UI.cur_y = wrap(UI.cur_y - 1); break;
    case EV_PUSH_R: set_cell(UI.color);   break;
    case EV_PUSH_L: set_cell(CELL_EMPTY); break;
    case EV_BTN_MENU: UI.mode = MODE_PALETTE; UI.pick = UI.color;         break;
    case EV_BTN_SIDE: UI.mode = MODE_SETTINGS; UI.sel = 0;                break;
    default: break;
    }
}

static void palette_mode(event_t ev)
{
    // Inline picker on the strip below the grid: either knob steps chip by chip
    // (wrapping), place or MENU confirms, erase cancels. The grid stays visible.
    int n = PALETTES[UI.palette].count;
    switch (ev) {
    case EV_ENC_L_CW:  case EV_ENC_R_CW:  UI.pick = (UI.pick + 1) % n;     break;
    case EV_ENC_L_CCW: case EV_ENC_R_CCW: UI.pick = (UI.pick + n - 1) % n; break;
    case EV_PUSH_R:
    case EV_BTN_MENU: UI.color = UI.pick; UI.mode = MODE_DRAW; break;
    case EV_PUSH_L:   UI.mode = MODE_DRAW; break;          // cancel: keep old colour
    case EV_BTN_SIDE: UI.mode = MODE_SETTINGS; UI.sel = 0; break;
    default: break;
    }
}

static void settings_mode(event_t ev)
{
    // P1 TODO: real menu tree (brand select, save slots, export, brightness).
    switch (ev) {
    case EV_ENC_L_CW: case EV_ENC_R_CW:  UI.sel++; break;
    case EV_ENC_L_CCW: case EV_ENC_R_CCW: if (UI.sel) UI.sel--; break;
    case EV_PUSH_R:
        // selection 0 = Export (placeholder order)
        if (UI.sel == 0) { store_flush(); export_start(); UI.mode = MODE_EXPORT; }
        break;
    case EV_PUSH_L:
    case EV_BTN_SIDE: UI.mode = MODE_DRAW; break;
    default: break;
    }
}

static void export_mode(event_t ev)
{
    switch (ev) {
    case EV_PUSH_L:  export_stop(); UI.mode = MODE_SETTINGS; break;
    case EV_BTN_SIDE: export_stop(); UI.mode = MODE_DRAW;    break;
    default: break;
    }
}

void ui_run(void)
{
    for (unsigned i = 0; i < GRID_N * GRID_N; i++) UI.cells[i] = CELL_EMPTY;
    store_load_autosave();
    render_full();
    int64_t last_input_us = 0;
    for (;;) {
        event_t ev;
        // Poll faster while a save is pending so it lands ~1.5 s after the last edit.
        if (!input_wait(&ev, store_pending() ? 200 : 1000)) {
#if CONFIG_BEAD_PERF_LOG
            bool was = store_pending();
            int64_t ts = esp_timer_get_time();
#endif
            // Save only once the user has paused (no input for AUTOSAVE_QUIET_US),
            // not merely stopped editing: e.g. still browsing the colour picker.
            if (esp_timer_get_time() - last_input_us >= AUTOSAVE_QUIET_US)
                store_autosave_if_idle();
#if CONFIG_BEAD_PERF_LOG
            if (was && !store_pending())
                ESP_LOGI("perf", "autosave (idle) %lld us",
                         (long long)(esp_timer_get_time() - ts));
#endif
            render_idle_tick();
            continue;
        }
        last_input_us = esp_timer_get_time();
        ESP_LOGI(TAG, "ev=%s mode=%d cur=%d,%d",
                 (unsigned)ev < sizeof EV_NAME / sizeof *EV_NAME ? EV_NAME[ev] : "?",
                 (int)UI.mode, UI.cur_x, UI.cur_y);
        if (ev == EV_TEST_BARS) {           // bench colour check, any mode
            render_test_bars();
            vTaskDelay(pdMS_TO_TICKS(3000));
            render_full();
            continue;
        }
        switch (UI.mode) {
        case MODE_DRAW:     draw_mode(ev);     break;
        case MODE_PALETTE:  palette_mode(ev);  break;
        case MODE_SETTINGS: settings_mode(ev); break;
        case MODE_EXPORT:   export_mode(ev);   break;
        }
#if CONFIG_BEAD_PERF_LOG
        int64_t t0 = esp_timer_get_time();
#endif
        render_update();   // redraws only what changed; full redraw on screen switches
#if CONFIG_BEAD_PERF_LOG
        ESP_LOGI("perf", "render %lld us", (long long)(esp_timer_get_time() - t0));
#endif
    }
}
