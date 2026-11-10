// Mode state machine. Mapping = spec Rev A.5 section 7 (see README table).
// Cursor wraps in every mode.
#include "ui.h"
#include "input.h"
#include "render.h"
#include "store.h"
#include "export.h"
#include "palettes.h"

ui_state_t UI = { .cur_x = 4, .cur_y = 4, .palette = 2 /* Artkal */ };

static inline int wrap(int v) { return (v + GRID_N) % GRID_N; }

static void draw_mode(event_t ev)
{
    switch (ev) {
    case EV_ENC_L_CW:  UI.cur_x = wrap(UI.cur_x + 1); break;
    case EV_ENC_L_CCW: UI.cur_x = wrap(UI.cur_x - 1); break;
    case EV_ENC_R_CW:  UI.cur_y = wrap(UI.cur_y + 1); break;
    case EV_ENC_R_CCW: UI.cur_y = wrap(UI.cur_y - 1); break;
    case EV_PUSH_R: UI.cells[UI.cur_y * GRID_N + UI.cur_x] = UI.color;    break;
    case EV_PUSH_L: UI.cells[UI.cur_y * GRID_N + UI.cur_x] = CELL_EMPTY;  break;
    case EV_BTN_MENU: UI.mode = MODE_PALETTE; UI.pick_x = UI.pick_y = 0;  break;
    case EV_BTN_SIDE: UI.mode = MODE_SETTINGS; UI.sel = 0;                break;
    default: break;
    }
}

static void palette_mode(event_t ev)
{
    const palette_t *p = &PALETTES[UI.palette];
    int cols = render_palette_cols(p->count);
    int rows = (p->count + cols - 1) / cols;
    switch (ev) {
    case EV_ENC_L_CW:  UI.pick_x = (UI.pick_x + 1) % cols;        break;
    case EV_ENC_L_CCW: UI.pick_x = (UI.pick_x + cols - 1) % cols; break;
    case EV_ENC_R_CW:  UI.pick_y = (UI.pick_y + 1) % rows;        break;
    case EV_ENC_R_CCW: UI.pick_y = (UI.pick_y + rows - 1) % rows; break;
    case EV_PUSH_R: {
        int idx = UI.pick_y * cols + UI.pick_x;
        if (idx < p->count) { UI.color = idx; UI.mode = MODE_DRAW; }
        break;
    }
    case EV_PUSH_L:
    case EV_BTN_MENU: UI.mode = MODE_DRAW; break;
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
        if (UI.sel == 0) { export_start(); UI.mode = MODE_EXPORT; }
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
    for (;;) {
        event_t ev;
        if (!input_wait(&ev, 1000)) { render_idle_tick(); continue; }
        ui_mode_t before = UI.mode;
        switch (UI.mode) {
        case MODE_DRAW:     draw_mode(ev);     break;
        case MODE_PALETTE:  palette_mode(ev);  break;
        case MODE_SETTINGS: settings_mode(ev); break;
        case MODE_EXPORT:   export_mode(ev);   break;
        }
        if (UI.mode != before) render_full();
        else render_update();
        if (UI.mode == MODE_DRAW) store_autosave_lazy();
    }
}
