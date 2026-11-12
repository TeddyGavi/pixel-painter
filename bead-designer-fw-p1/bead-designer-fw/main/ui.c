// Mode state machine. Mapping = spec Rev A.5 section 7 (see README table).
// Cursor wraps in every mode.
#include "ui.h"
#include "input.h"
#include "render.h"
#include "store.h"
#include "menu.h"
#include "palettes.h"
#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ui";   // one log line per input event (BENCH.md)
#define AUTOSAVE_QUIET_US 1000000   // no input for 1 s before an autosave may run
ui_state_t UI = { .cur_x = 4, .cur_y = 4, .palette = 2 /* Artkal */ };

// Default shortlist: white, black, red, yellow, blue where the palette has them.
static const char *const DEFAULT_SLOTS[SLOTS] = { "S01", "S13", "S05", "S27", "S11" };

static void default_slots(void)
{
    const palette_t *p = &PALETTES[UI.palette];
    for (int i = 0; i < SLOTS; i++) {
        int idx = palette_find(p, DEFAULT_SLOTS[i]);
        UI.slots[i] = (uint8_t)(idx >= 0 ? idx : i % p->count);
    }
    UI.slot = 0;
}

static inline int wrap(int v) { return (v + GRID_N) % GRID_N; }

// Change the cell under the cursor; only a real change schedules an autosave.
static void set_cell(uint8_t v)
{
    uint8_t *c = &UI.cells[UI.cur_y * GRID_N + UI.cur_x];
    if (*c != v) { *c = v; store_mark_dirty(); }
}

// Change the selected slot's colour; the shortlist is saved with the pattern.
static void set_slot(uint8_t idx)
{
    if (UI.slots[UI.slot] != idx) { UI.slots[UI.slot] = idx; store_mark_dirty(); }
}

// Browse acceleration: fast turns step further (detents < 60 ms apart = x4,
// < 120 ms = x2). The first detent of a press-and-turn always steps by 1.
static int browse_step(bool first)
{
    static int64_t last;
    int64_t now = esp_timer_get_time(), dt = now - last;
    last = now;
    if (first) return 1;
    return dt < 60000 ? 4 : dt < 120000 ? 2 : 1;
}

static void browse(int dir)
{
    const palette_t *p = &PALETTES[UI.palette];
    bool first = !UI.browsing;
    if (first) {                        // first detent of a press-and-turn
        UI.browsing = true;
        UI.browse_pos = palette_browse_pos(p, ui_color());
    }
    int n = p->count;
    UI.browse_pos = ((UI.browse_pos + dir * browse_step(first)) % n + n) % n;
    set_slot((uint8_t)palette_browse_index(p, UI.browse_pos)); // live; release keeps it
}

static void family_jump(int dir)
{
    const palette_t *p = &PALETTES[UI.palette];
    UI.browse_pos = palette_family_jump(p, UI.browse_pos, dir);
    set_slot((uint8_t)palette_browse_index(p, UI.browse_pos));
}

static void draw_mode(event_t ev)
{
    switch (ev) {
    // D-pad: cursor (while browsing, left/right jump colour families instead)
    case EV_UP:    if (!UI.browsing) UI.cur_y = wrap(UI.cur_y - 1); break;
    case EV_DOWN:  if (!UI.browsing) UI.cur_y = wrap(UI.cur_y + 1); break;
    case EV_LEFT:  if (UI.browsing) family_jump(-1); else UI.cur_x = wrap(UI.cur_x - 1); break;
    case EV_RIGHT: if (UI.browsing) family_jump(+1); else UI.cur_x = wrap(UI.cur_x + 1); break;
    case EV_CENTER: set_cell(CELL_EMPTY); break;                     // erase
    case EV_CENTER_LONG: {                                           // eyedropper
        uint8_t c = UI.cells[UI.cur_y * GRID_N + UI.cur_x];
        if (c != CELL_EMPTY) set_slot(c);
        break;
    }
    // Encoder: shortlist slot / place / browse
    case EV_ENC_CW:  UI.slot = (UI.slot + 1) % SLOTS;         break;
    case EV_ENC_CCW: UI.slot = (UI.slot + SLOTS - 1) % SLOTS; break;
    case EV_ENC_PRESS: set_cell((uint8_t)ui_color());         break;
    case EV_BROWSE_CW:  browse(+1); break;
    case EV_BROWSE_CCW: browse(-1); break;
    case EV_BROWSE_END: UI.browsing = false; break;
    case EV_MENU: UI.browsing = false; menu_open_main(); UI.mode = MODE_MENU; break;
    case EV_SIDE: UI.browsing = false; menu_open_used(); UI.mode = MODE_MENU; break;
    default: break;
    }
}

void ui_run(void)
{
    for (unsigned i = 0; i < GRID_N * GRID_N; i++) UI.cells[i] = CELL_EMPTY;
    default_slots();
    store_load_autosave();
    render_full();
    int64_t last_input_us = 0;
    for (;;) {
        event_t ev;
        // Poll faster while a save is pending so it lands ~1.5 s after the last edit,
        // and wake for marquee frames when the name line is animating.
        int wait_ms = store_pending() ? 200 : 1000;
        int tick_ms = render_tick_ms();
        if (tick_ms >= 0 && tick_ms < wait_ms) wait_ms = tick_ms;
        if (!input_wait(&ev, wait_ms)) {
            render_tick();
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
        ESP_LOGI(TAG, "ev=%s mode=%d cur=%d,%d slot=%d",
                 (unsigned)ev < EV__COUNT ? EVENT_NAMES[ev] : "?",
                 (int)UI.mode, UI.cur_x, UI.cur_y, UI.slot);
        if (ev == EV_TEST_BARS) {           // bench colour check, any mode
            render_test_bars();
            vTaskDelay(pdMS_TO_TICKS(3000));
            render_full();
            continue;
        }
        switch (UI.mode) {
        case MODE_DRAW:     draw_mode(ev);     break;
        case MODE_MENU:
            if (menu_event(ev) == MENU_CLOSE) UI.mode = MODE_DRAW;
            break;
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
