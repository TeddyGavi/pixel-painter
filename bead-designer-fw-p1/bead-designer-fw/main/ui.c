// Mode state machine. Mapping = spec Rev A.5 section 7 (see README table).
// Cursor wraps in every mode.
#include "ui.h"
#include "input.h"
#include "render.h"
#include "store.h"
#include "menu.h"
#include "palettes.h"
#include "board_style.h"
#include "board.h"
#include "saver.h"
#include "battery.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ui";   // one log line per input event (BENCH.md)
#define AUTOSAVE_QUIET_US 1000000   // no input for 1 s before an autosave may run
ui_state_t UI = { .cur_x = 4, .cur_y = 4, .palette = 1 /* Artkal */,
                  .board = BOARD_DEFAULT, .texture = 0, .bevel = true,
                  .saver = SAVER_DEFAULT };

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
    if (*c != v) { *c = v; UI.unsaved = true; store_mark_dirty(); }
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
    switch (ev) {                      // focus follows the control used last
    case EV_ENC_CW: case EV_ENC_CCW: case EV_BROWSE_CW: case EV_BROWSE_CCW:
        UI.focus = FOCUS_PALETTE; break;
    case EV_LEFT: case EV_RIGHT:
        if (!UI.browsing) UI.focus = FOCUS_BOARD;    // while browsing: family jump
        break;
    case EV_UP: case EV_DOWN: case EV_CENTER: case EV_CENTER_LONG: case EV_ENC_PRESS: case EV_PAINT:
        UI.focus = FOCUS_BOARD; break;
    default: break;
    }
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
    case EV_PAINT:     set_cell((uint8_t)ui_color());         break;   // brush: paint the cell moved onto
    case EV_BROWSE_CW:  browse(+1); break;
    case EV_BROWSE_CCW: browse(-1); break;
    case EV_BROWSE_END: UI.browsing = false; break;
    case EV_MENU: UI.browsing = false; menu_open_main(); UI.mode = MODE_MENU; break;
    case EV_SIDE: UI.browsing = false; menu_open_used(); UI.mode = MODE_MENU; break;
    default: break;
    }
}

// ---- power: sleep, shut down, battery policy ------------------------------------------
#define IDLE_SLEEP_US  (10LL * 60 * 1000000)   // no input this long (saver or not): sleep
#define SLEEP_OFF_US   (60LL * 60 * 1000000)   // asleep this long: save and switch off
#define LOW_BRIGHTNESS 60                      // % while the battery is low

static const display_ops_t *s_disp;
static bool s_sleeping, s_alert, s_alerted, s_was_low;
static int64_t s_sleep_us;
static int s_batt_segs = -1;

static void present(void) { if (s_disp->present) s_disp->present(); }

// Sleep: pattern saved, screen off. Any button wakes (see ui_run).
static void go_sleep(void)
{
    // Panel off FIRST: stopping the saver switches the display back to the normal
    // framebuffer (the pre-saver screen), which must not be sent to a lit panel:
    // a PWR press during the saver flashed the old screen before going dark.
    // Whatever is drawn while asleep only reaches the panel's RAM; wake redraws.
    board_display_sleep(true);
    if (saver_active()) saver_stop();
    store_flush();
    s_sleeping = true;
    s_sleep_us = esp_timer_get_time();
    ESP_LOGI(TAG, "sleep (battery %d mV)", battery_mv());
}

static void wake_up(void)
{
    board_display_sleep(false);
    s_sleeping = false;
    render_full();
    ESP_LOGI(TAG, "wake from sleep");
}

// Shut Down: message, save, power off (soft off when USB keeps the board powered).
static void shut_down(const char *why)
{
    ESP_LOGW(TAG, "shut down: %s", why ? why : "PWR held");
    if (saver_active()) saver_stop();
    if (s_sleeping) { board_display_sleep(false); s_sleeping = false; }
    render_message(why ? why : "Shutting down", "Pattern saved");
    present();
    store_flush();
    vTaskDelay(pdMS_TO_TICKS(900));
    board_power_off();
}

// After each battery reading: power saving below 20 %, one alert at 10 %, switch
// off at the critical level, and keep the battery icon current.
static void battery_policy(void)
{
    int pct = battery_pct();
    if (battery_critical()) shut_down("Battery empty");
    bool low = battery_low();
    if (low != s_was_low) {
        s_was_low = low;
        if (s_disp->set_brightness) s_disp->set_brightness(low ? LOW_BRIGHTNESS : 100);
        ESP_LOGI(TAG, "battery %s (%d%%)", low ? "low: power saving" : "ok", pct);
        if (!s_sleeping && !saver_active() && !s_alert) render_full();
    }
    if (pct >= 0 && pct <= 10 && !s_alerted && !s_sleeping) {
        s_alerted = true;
        if (saver_active()) saver_stop();
        render_message("Battery low", "Plug in soon");
        s_alert = true;
        ESP_LOGW(TAG, "battery low alert (%d%%)", pct);
    }
    if (pct > 15) s_alerted = false;
    int segs = pct < 0 ? -1 : pct > 66 ? 3 : pct > 33 ? 2 : pct > 10 ? 1 : 0;
    if (segs != s_batt_segs) {                 // battery icon changed: redraw menu titles
        s_batt_segs = segs;
        if (UI.mode == MODE_MENU && !s_sleeping && !saver_active() && !s_alert) render_full();
    }
}

void ui_run(void)
{
    for (unsigned i = 0; i < GRID_N * GRID_N; i++) UI.cells[i] = CELL_EMPTY;
    default_slots();
    store_load_autosave();
    s_disp = board_display();
    battery_init();
    s_was_low = battery_low();
    if (s_was_low && s_disp->set_brightness) s_disp->set_brightness(LOW_BRIGHTNESS);
    render_full();
    int64_t last_input_us = esp_timer_get_time();
    int64_t swallow_us = 0;          // after a wake: input arriving before this is ignored
    int64_t next_batt_us = last_input_us + BATTERY_POLL_MS * 1000LL;
    esp_task_wdt_add(NULL);                 // a stuck UI task panics with a backtrace
    for (;;) {
        event_t ev;
        esp_task_wdt_reset();
        present();                            // show everything drawn in the last pass at once
        int64_t now = esp_timer_get_time();
        if (now >= next_batt_us) {
            next_batt_us = now + BATTERY_POLL_MS * 1000LL;
            battery_poll();
            battery_policy();
        }
        if (s_sleeping) {                     // asleep: wait for any input; switch off after an hour
            if (now - s_sleep_us >= SLEEP_OFF_US) shut_down(NULL);
            if (!input_wait(&ev, 1000)) continue;
            if (ev == EV_TICK) { battery_policy(); continue; }
            if (ev == EV_PWR_HOLD) shut_down(NULL);
            wake_up();
            last_input_us = esp_timer_get_time();
            swallow_us = last_input_us + 300000;
            continue;
        }
        // Poll faster while a save is pending so it lands ~1.5 s after the last edit,
        // and wake for marquee frames when the name line is animating.
        int wait_ms = store_pending() ? 200 : 1000;
        int tick_ms = render_tick_ms();
        if (tick_ms >= 0 && tick_ms < wait_ms) wait_ms = tick_ms;
        int64_t saver_us = (int64_t)SAVER_TIMEOUT_S[UI.saver] * 1000000;
        bool saver_may = saver_us > 0 && !s_alert && (UI.mode != MODE_MENU || menu_saver_allowed());
        if (saver_active()) {
            wait_ms = saver_next_ms();
        } else if (saver_may) {
            int64_t left = (last_input_us + saver_us - esp_timer_get_time()) / 1000;
            if (left < wait_ms) wait_ms = left < 0 ? 0 : (int)left;
        }
        if (!input_wait(&ev, wait_ms)) {
            now = esp_timer_get_time();
            if (now - last_input_us >= IDLE_SLEEP_US) { go_sleep(); continue; }
            if (saver_active()) {
                saver_frame();
                if (now - last_input_us >= AUTOSAVE_QUIET_US) store_autosave_if_idle();
                continue;
            }
            if (saver_may && now - last_input_us >= saver_us) {
                if (battery_low()) {          // saving power: a black screen instead of the saver
                    ESP_LOGI(TAG, "battery low: sleep instead of the screen saver");
                    go_sleep();
                    continue;
                }
                ESP_LOGI(TAG, "screen saver after %u s idle", SAVER_TIMEOUT_S[UI.saver]);
                UI.browsing = false;
                saver_start();
                if (!saver_active()) last_input_us = esp_timer_get_time();   // retry later
                continue;
            }
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
        if (ev == EV_TICK) { battery_policy(); continue; }   // bench battery simulation
        if (ev == EV_PWR_HOLD) shut_down(NULL);
        if (ev == EV_PWR) { go_sleep(); continue; }
        last_input_us = esp_timer_get_time();
        if (s_alert) {                        // the low-battery notice: any button dismisses
            s_alert = false;
            render_full();
            swallow_us = last_input_us + 300000;
            continue;
        }
        // Wake from the screen saver: the input only wakes. Input that keeps coming
        // (a held D-pad repeating, a fast encoder turn) is ignored until a pause.
        if (saver_active() || last_input_us < swallow_us) {
            if (saver_active()) {
                saver_stop();
                render_full();
                ESP_LOGI(TAG, "wake (ev=%s ignored)", (unsigned)ev < EV__COUNT ? EVENT_NAMES[ev] : "?");
            }
            swallow_us = last_input_us + 300000;
            continue;
        }
        ESP_LOGI(TAG, "ev=%s mode=%d cur=%d,%d slot=%d",
                 (unsigned)ev < EV__COUNT ? EVENT_NAMES[ev] : "?",
                 (int)UI.mode, UI.cur_x, UI.cur_y, UI.slot);
        if (ev == EV_TEST_BARS) {           // bench colour check, any mode
            render_test_bars();
            present();
            vTaskDelay(pdMS_TO_TICKS(3000));
            render_full();
            continue;
        }
        switch (UI.mode) {
        case MODE_DRAW:     draw_mode(ev);     break;
        case MODE_MENU:
            if (menu_event(ev) == MENU_CLOSE) { UI.mode = MODE_DRAW; UI.focus = FOCUS_BOARD; }
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
