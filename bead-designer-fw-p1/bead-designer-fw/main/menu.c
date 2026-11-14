// Menu screens (opened with MENU; SIDE opens "Colours in pattern" directly).
//
//   Main ── Pattern ─┬─ Save ...... 8 slots (thumbnail rows); occupied: confirm replace
//        │            ├─ Load ...... 8 slots; unsaved beads on the board: confirm discard
//        │            └─ Reset ..... clear the beads (same unsaved warning)
//        ├─ Brand ............ only while the board is empty (shortlist maps to the
//        │                      nearest colours); with beads: "locked" + Reset shortcut
//        ├─ Edit shortlist ─── slot ─┬─ Browse colours (list, family jump)
//        │                           └─ Enter code (dial characters, OK)
//        ├─ Colours used ...... counts; press = load into the selected slot
//        ├─ Board ............. Solid / Pegs / Big pegs / Classic (press cycles)
//        ├─ Texture ........... 1/8 .. 8/8 (press cycles)
//        ├─ Beads ............. Bevel / Flat (press toggles)
//        ├─ Screensaver ....... Off / 5s / 30s / 1m / 5m idle (press cycles)
//        └─ Export ............ QR code: phone opens the export page (device offline)
//                └─ Wi-Fi ..... fallback: SoftAP export running while shown
//
// Controls: up/down or encoder turn = move; encoder press or right = select;
// D-pad centre (or left, except in colour lists) = back; MENU = close.
// In colour lists left/right jump colour families.
#include "menu.h"
#include "ui.h"
#include "palettes.h"
#include "store.h"
#include "export.h"
#include "board_style.h"
#include "saver.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define VISIBLE_ROWS 9     // row units below the title band; must match render.c

typedef enum { S_MAIN, S_BRAND, S_SLOTS, S_SLOT, S_COLORS, S_CODE, S_USED, S_EXPORT, S_WIFI,
               S_PATTERN, S_SAVE, S_LOAD, S_CONFIRM, S_BRAND_LOCKED } screen_t;
typedef enum { C_REPLACE, C_LOAD, C_RESET } confirm_t;

static const uint8_t *s_qr;   // QR of the pattern while S_EXPORT is shown (NULL = too big)

static screen_t s_screen;
static int s_sel, s_top;
static unsigned s_version;
static int s_edit;                 // slot being edited (S_SLOT / S_COLORS / S_CODE)
static bool s_direct;              // opened straight into a screen (SIDE): back closes
static confirm_t s_confirm;        // what S_CONFIRM is asking about
static int s_confirm_slot;         // C_REPLACE / C_LOAD: the slot
static bool s_reset_from_brand;    // reset started from the locked Brand screen

// Code entry
#define CODE_MAX 9   // longest code: Miyuki "DB-1472-1"
static char s_code[CODE_MAX + 1];
static char s_cands[24];           // valid next characters; '\x01' = OK
static int s_cand;
#define OK_CHAR '\x01'

// Colours in pattern (sorted by count)
static uint8_t s_used[256];
static int s_used_n;
static int s_count[256];

static const palette_t *pal(void) { return &PALETTES[UI.palette]; }

// Board / texture / bead style changed in place: redraw the row; device-wide setting.
static void look_changed(void)
{
    s_version++;
    store_mark_look_dirty();
}

static int rows_fitting(int top);

static void go(screen_t sc, int sel)
{
    s_screen = sc; s_sel = sel; s_top = 0; s_version++;
    while (s_sel > 0 && s_sel >= s_top + rows_fitting(s_top)) s_top++;   // keep sel visible
}

// ---- data helpers -----------------------------------------------------------

static void count_used(void)
{
    memset(s_count, 0, sizeof s_count);
    for (int i = 0; i < GRID_N * GRID_N; i++)
        if (UI.cells[i] != CELL_EMPTY) s_count[UI.cells[i]]++;
    s_used_n = 0;
    for (int pos = 0; pos < pal()->count; pos++) {          // browse order, then
        int idx = palette_browse_index(pal(), pos);           // sort by count desc
        if (s_count[idx]) s_used[s_used_n++] = (uint8_t)idx;
    }
    for (int i = 1; i < s_used_n; i++)                        // stable insertion sort
        for (int j = i; j > 0 && s_count[s_used[j]] > s_count[s_used[j - 1]]; j--) {
            uint8_t t = s_used[j]; s_used[j] = s_used[j - 1]; s_used[j - 1] = t;
        }
}

static int total_beads(void)
{
    int n = 0;
    for (int i = 0; i < GRID_N * GRID_N; i++) n += UI.cells[i] != CELL_EMPTY;
    return n;
}

// Beads on the board that are not in any slot yet: Load and Reset warn first.
static bool unsaved_beads(void) { return UI.unsaved && total_beads() > 0; }

static void slot_row(int i, menu_row_t *r)
{
    int p, beads; const uint8_t *cells;
    r->slot = true;
    if (store_slot_info(i, &p, &cells, &beads)) {
        snprintf(r->left, sizeof r->left, "%d  %s", i + 1, PALETTES[p].brand);
        snprintf(r->sub, sizeof r->sub, "%d bead%s", beads, beads == 1 ? "" : "s");
        r->thumb = cells; r->thumb_pal = p;
    } else {
        snprintf(r->left, sizeof r->left, "%d  Empty", i + 1);
    }
}

static void reset_board(void)
{
    for (int i = 0; i < GRID_N * GRID_N; i++) UI.cells[i] = CELL_EMPTY;
    UI.unsaved = false;
    store_mark_dirty();
}

static void set_slot_color(int slot, int idx)
{
    UI.slot = slot;
    if (UI.slots[slot] != idx) { UI.slots[slot] = (uint8_t)idx; store_mark_dirty(); }
}

// Switch brand: every placed bead and every slot maps to the nearest colour.
static void set_brand(int b)
{
    if (b == UI.palette) return;
    const palette_t *from = pal(), *to = &PALETTES[b];
    for (int i = 0; i < GRID_N * GRID_N; i++)
        if (UI.cells[i] != CELL_EMPTY)
            UI.cells[i] = (uint8_t)palette_nearest(to, from->colors[UI.cells[i]].rgb565);
    for (int i = 0; i < SLOTS; i++)
        UI.slots[i] = (uint8_t)palette_nearest(to, from->colors[UI.slots[i]].rgb565);
    UI.palette = b;
    store_mark_dirty();
}

// Code entry: which characters can follow s_code, and is s_code a full code?
static void code_refresh(void)
{
    size_t len = strlen(s_code);
    bool exact = false;
    char set[24] = { 0 };
    int n = 0;
    for (int i = 0; i < pal()->count; i++) {
        const char *c = pal()->colors[i].code;
        if (strncmp(c, s_code, len) != 0) continue;
        if (c[len] == 0) { exact = true; continue; }
        if (len < CODE_MAX && !strchr(set, c[len]) && n < 22) set[n++] = c[len];
    }
    // letters before digits, each sorted
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0; j--) {
            char a = set[j - 1], b = set[j];
            bool swap = (a >= '0' && a <= '9' && !(b >= '0' && b <= '9')) ||
                        (((a >= '0' && a <= '9') == (b >= '0' && b <= '9')) && a > b);
            if (!swap) break;
            set[j - 1] = b; set[j] = a;
        }
    int k = 0;
    if (exact) s_cands[k++] = OK_CHAR;
    memcpy(s_cands + k, set, n);
    s_cands[k + n] = 0;
    if (s_cand >= (int)strlen(s_cands)) s_cand = 0;
    s_version++;
}

static int code_match(void)
{
    return palette_find(pal(), s_code);
}

// ---- screen definitions -----------------------------------------------------

static int row_count(void)
{
    switch (s_screen) {
    case S_MAIN:   return 9;
    case S_BRAND:  return PALETTE_COUNT;
    case S_SLOTS:  return SLOTS;
    case S_SLOT:   return 2;
    case S_COLORS: return pal()->count;
    case S_CODE: {                                   // input line + matching colours
        size_t len = strlen(s_code); int n = 1;
        for (int i = 0; i < pal()->count; i++)
            if (strncmp(pal()->colors[i].code, s_code, len) == 0) n++;
        return n;
    }
    case S_USED:   return s_used_n ? s_used_n : 1;
    case S_EXPORT: return s_qr ? 0 : 2;            // QR drawn by render.c
    case S_WIFI:   return 4;
    case S_PATTERN: return 3;
    case S_SAVE: case S_LOAD: return STORE_SLOTS;
    case S_CONFIRM: case S_BRAND_LOCKED: return 3;
    }
    return 0;
}

static void color_row(int idx, menu_row_t *r)
{
    const bead_color_t *c = &pal()->colors[idx];
    snprintf(r->left, sizeof r->left, "%s %s", c->code, c->name);
    r->swatch = c->rgb565;
}

void menu_row(int i, menu_row_t *r)
{
    r->left[0] = r->right[0] = 0;
    r->swatch = -1;
    r->thumb = NULL;
    r->slot = false;
    r->sub[0] = 0;
    switch (s_screen) {
    case S_MAIN: {
        static const char *const L[] = { "Pattern", "Brand", "Edit shortlist", "Colours used",
                                         "Board", "Texture", "Beads", "Screensaver", "Export" };
        snprintf(r->left, sizeof r->left, "%s", L[i]);
        if (i == 0 && unsaved_beads()) snprintf(r->right, sizeof r->right, "unsaved");
        if (i == 1) snprintf(r->right, sizeof r->right, "%s", pal()->brand);
        if (i == 3) { count_used(); snprintf(r->right, sizeof r->right, "%d", s_used_n); }
        if (i == 4) snprintf(r->right, sizeof r->right, "%s", BOARD_NAMES[UI.board]);
        if (i == 5) snprintf(r->right, sizeof r->right, "%d/%d", UI.texture + 1, TEXTURE_COUNT);
        if (i == 6) snprintf(r->right, sizeof r->right, "%s", UI.bevel ? "Bevel" : "Flat");
        if (i == 7) snprintf(r->right, sizeof r->right, "%s", SAVER_NAMES[UI.saver]);
        break;
    }
    case S_PATTERN: {
        static const char *const L[] = { "Save", "Load", "Reset board" };
        snprintf(r->left, sizeof r->left, "%s", L[i]);
        if (i == 0 && unsaved_beads()) snprintf(r->right, sizeof r->right, "unsaved");
        break;
    }
    case S_SAVE: case S_LOAD:
        slot_row(i, r);
        break;
    case S_CONFIRM:
        if (i == 0) {
            if (s_confirm == C_REPLACE) slot_row(s_confirm_slot, r);
            else snprintf(r->left, sizeof r->left, "Not saved yet");
        } else if (i == 1) {
            static const char *const A[] = { "Replace", "Discard + load", "Discard + reset" };
            snprintf(r->left, sizeof r->left, "%s", A[s_confirm]);
        } else {
            snprintf(r->left, sizeof r->left, "Cancel");
        }
        break;
    case S_BRAND_LOCKED: {
        static const char *const L[] = { "Has beads", "Reset board...", "Back" };
        snprintf(r->left, sizeof r->left, "%s", L[i]);
        break;
    }
    case S_BRAND:
        snprintf(r->left, sizeof r->left, "%s", PALETTES[i].brand);
        snprintf(r->right, sizeof r->right, "%s%d", i == UI.palette ? "* " : "",
                 PALETTES[i].count);
        break;
    case S_SLOTS:
        color_row(UI.slots[i], r);
        snprintf(r->right, sizeof r->right, "%s%d", i == UI.slot ? "* " : "", i + 1);
        break;
    case S_SLOT:
        snprintf(r->left, sizeof r->left, "%s", i == 0 ? "Browse colours" : "Enter code");
        break;
    case S_COLORS: {
        int pos = i, idx = palette_browse_index(pal(), pos);
        color_row(idx, r);           // full width; the family is shown in the title
        break;
    }
    case S_CODE:
        if (i == 0) {
            char cand[4];
            if (!s_cands[0]) snprintf(cand, sizeof cand, "-");
            else if (s_cands[s_cand] == OK_CHAR) snprintf(cand, sizeof cand, "OK");
            else snprintf(cand, sizeof cand, "%c", s_cands[s_cand]);
            snprintf(r->left, sizeof r->left, "Code: %s[%s]", s_code, cand);
        } else {
            size_t len = strlen(s_code); int k = 0;
            for (int j = 0; j < pal()->count; j++)
                if (strncmp(pal()->colors[j].code, s_code, len) == 0 && ++k == i) {
                    color_row(j, r); break;
                }
        }
        break;
    case S_USED:
        if (!s_used_n) { snprintf(r->left, sizeof r->left, "No beads yet"); break; }
        color_row(s_used[i], r);
        snprintf(r->right, sizeof r->right, "%d", s_count[s_used[i]]);
        break;
    case S_EXPORT: {                                 // only shown without a QR
        static const char *const L[] = { "Too many colours", "Press: Wi-Fi export" };
        snprintf(r->left, sizeof r->left, "%s", L[i]);
        break;
    }
    case S_WIFI: {
        static const char *const L[] = { "Wi-Fi", "Password", "Open", "Back stops it" };
        static const char *const R[] = { "BeadPad", "beads1234", "192.168.4.1", "" };
        snprintf(r->left, sizeof r->left, "%s", L[i]);
        snprintf(r->right, sizeof r->right, "%s", R[i]);
        break;
    }
    }
}

void menu_view(menu_view_t *v)
{
    v->count = row_count();
    v->sel = (s_screen == S_CODE || s_screen == S_EXPORT || s_screen == S_WIFI) ? -1 : s_sel;
    v->qr = s_screen == S_EXPORT ? s_qr : NULL;
    v->hint[0] = 0;
    v->top = s_top;
    v->version = s_version;
    switch (s_screen) {
    case S_MAIN:   snprintf(v->title, sizeof v->title, "Menu"); break;
    case S_BRAND:  snprintf(v->title, sizeof v->title, "Brand"); break;
    case S_SLOTS:  snprintf(v->title, sizeof v->title, "Shortlist"); break;
    case S_SLOT:   snprintf(v->title, sizeof v->title, "Slot %d", s_edit + 1); break;
    case S_COLORS: {                 // family of the highlighted colour
        int idx = palette_browse_index(pal(), s_sel);
        snprintf(v->title, sizeof v->title, "Slot %d: %s", s_edit + 1,
                 FAMILY_NAMES[pal()->colors[idx].family]);
        break;
    }
    case S_CODE:   snprintf(v->title, sizeof v->title, "Slot %d: code", s_edit + 1); break;
    case S_USED:   snprintf(v->title, sizeof v->title, "In pattern: %d", total_beads()); break;
    case S_EXPORT:
        snprintf(v->title, sizeof v->title, s_qr ? "Scan with phone" : "Export");
        if (s_qr) snprintf(v->hint, sizeof v->hint, "Press for Wi-Fi");
        break;
    case S_WIFI:   snprintf(v->title, sizeof v->title, "Wi-Fi export"); break;
    case S_PATTERN: snprintf(v->title, sizeof v->title, "Pattern"); break;
    case S_SAVE:   snprintf(v->title, sizeof v->title, "Save to slot"); break;
    case S_LOAD:   snprintf(v->title, sizeof v->title, "Load slot"); break;
    case S_CONFIRM:
        if (s_confirm == C_REPLACE) snprintf(v->title, sizeof v->title, "Replace slot %d?", s_confirm_slot + 1);
        else snprintf(v->title, sizeof v->title, "Discard board?");
        break;
    case S_BRAND_LOCKED: snprintf(v->title, sizeof v->title, "%s locked", pal()->brand); break;
    }
}

// ---- navigation ---------------------------------------------------------------

// Slot rows (thumbnail + two lines) are double height.
int menu_row_units(int i)
{
    if (s_screen == S_SAVE || s_screen == S_LOAD) return 2;
    if (s_screen == S_CONFIRM && s_confirm == C_REPLACE && i == 0) return 2;
    return 1;
}

static int rows_fitting(int top)                  // rows from `top` that fit on screen
{
    int n = row_count(), u = 0, k = 0;
    for (int i = top; i < n && (u += menu_row_units(i)) <= VISIBLE_ROWS; i++) k++;
    return k;
}

static void move(int d)
{
    int n = row_count();
    if (n <= 0) return;
    s_sel = (s_sel + d + n) % n;                      // wrap
    if (s_sel < s_top) s_top = s_sel;
    while (s_sel >= s_top + rows_fitting(s_top)) s_top++;
}

static void show_sel_centered(int sel)
{
    s_sel = sel;
    s_top = sel - VISIBLE_ROWS / 2;
    int n = row_count();
    if (s_top > n - VISIBLE_ROWS) s_top = n - VISIBLE_ROWS;
    if (s_top < 0) s_top = 0;
}

static menu_result_t do_reset(void)
{
    reset_board();
    if (s_reset_from_brand) { go(S_BRAND, UI.palette); return MENU_STAY; }   // pick a brand next
    return MENU_CLOSE;
}

static menu_result_t request_reset(bool from_brand)
{
    s_reset_from_brand = from_brand;
    if (unsaved_beads()) { s_confirm = C_RESET; go(S_CONFIRM, 1); return MENU_STAY; }
    return do_reset();
}

static menu_result_t back(void)
{
    switch (s_screen) {
    case S_MAIN:   return MENU_CLOSE;
    case S_BRAND:  go(S_MAIN, 1); break;
    case S_BRAND_LOCKED: go(S_MAIN, 1); break;
    case S_PATTERN: go(S_MAIN, 0); break;
    case S_SAVE:   go(S_PATTERN, 0); break;
    case S_LOAD:   go(S_PATTERN, 1); break;
    case S_CONFIRM:
        if (s_confirm == C_REPLACE) go(S_SAVE, s_confirm_slot);
        else if (s_confirm == C_LOAD) go(S_LOAD, s_confirm_slot);
        else if (s_reset_from_brand) go(S_BRAND_LOCKED, 1);
        else go(S_PATTERN, 2);
        break;
    case S_SLOTS:  go(S_MAIN, 2); break;
    case S_SLOT:   go(S_SLOTS, s_edit); break;
    case S_COLORS: go(S_SLOT, 0); break;
    case S_CODE:   go(S_SLOT, 1); break;
    case S_USED:   if (s_direct) return MENU_CLOSE; go(S_MAIN, 3); break;
    case S_EXPORT: go(S_MAIN, 8); break;
    case S_WIFI:   export_stop(); go(S_EXPORT, -1); break;
    }
    return MENU_STAY;
}

static menu_result_t select(void)
{
    switch (s_screen) {
    case S_MAIN:
        if (s_sel == 0) go(S_PATTERN, 0);
        else if (s_sel == 1) {
            if (total_beads()) go(S_BRAND_LOCKED, 1);      // brand is fixed once beads exist
            else go(S_BRAND, UI.palette);
        }
        else if (s_sel == 2) go(S_SLOTS, UI.slot);
        else if (s_sel == 3) { count_used(); go(S_USED, 0); s_direct = false; }
        else if (s_sel == 4) { UI.board = (UI.board + 1) % BOARD__COUNT; look_changed(); }
        else if (s_sel == 5) { UI.texture = (UI.texture + 1) % TEXTURE_COUNT; look_changed(); }
        else if (s_sel == 6) { UI.bevel = !UI.bevel; look_changed(); }
        else if (s_sel == 7) { UI.saver = (UI.saver + 1) % SAVER_CHOICES; look_changed(); }
        else { s_qr = export_qr(); go(S_EXPORT, -1); }
        break;
    case S_BRAND:
        set_brand(s_sel);
        go(S_MAIN, 1);
        break;
    case S_BRAND_LOCKED:
        if (s_sel == 1) return request_reset(true);
        if (s_sel == 2) return back();
        break;
    case S_PATTERN:
        if (s_sel == 0) go(S_SAVE, 0);
        else if (s_sel == 1) go(S_LOAD, 0);
        else return request_reset(false);
        break;
    case S_SAVE:
        if (store_slot_info(s_sel, NULL, NULL, NULL)) {
            s_confirm = C_REPLACE; s_confirm_slot = s_sel; go(S_CONFIRM, 1);
        } else if (store_save_slot(s_sel)) {
            return MENU_CLOSE;
        }
        break;
    case S_LOAD:
        if (!store_slot_info(s_sel, NULL, NULL, NULL)) break;   // empty slot
        if (unsaved_beads()) { s_confirm = C_LOAD; s_confirm_slot = s_sel; go(S_CONFIRM, 1); }
        else if (store_load_slot(s_sel)) return MENU_CLOSE;
        break;
    case S_CONFIRM:
        if (s_sel == 2) return back();
        if (s_sel != 1) break;
        if (s_confirm == C_REPLACE) { if (store_save_slot(s_confirm_slot)) return MENU_CLOSE; }
        else if (s_confirm == C_LOAD) { if (store_load_slot(s_confirm_slot)) return MENU_CLOSE; }
        else return do_reset();
        break;
    case S_SLOTS:
        s_edit = s_sel;
        go(S_SLOT, 0);
        break;
    case S_SLOT:
        if (s_sel == 0) {
            go(S_COLORS, 0);
            show_sel_centered(palette_browse_pos(pal(), UI.slots[s_edit]));
        } else {
            s_code[0] = 0; s_cand = 0;
            go(S_CODE, -1);
            code_refresh();
        }
        break;
    case S_COLORS:
        set_slot_color(s_edit, palette_browse_index(pal(), s_sel));
        go(S_SLOTS, s_edit);
        break;
    case S_CODE:
        if (!s_cands[0]) break;
        if (s_cands[s_cand] == OK_CHAR) {
            int m = code_match();
            if (m >= 0) { set_slot_color(s_edit, m); go(S_SLOTS, s_edit); }
        } else if (strlen(s_code) < CODE_MAX) {
            size_t len = strlen(s_code);
            s_code[len] = s_cands[s_cand]; s_code[len + 1] = 0;
            s_cand = 0;
            code_refresh();
        }
        break;
    case S_USED:
        if (s_used_n) set_slot_color(UI.slot, s_used[s_sel]);   // load into selected slot
        return MENU_CLOSE;
    case S_EXPORT:                                   // fallback: phone joins the device
        store_flush(); export_start(); go(S_WIFI, -1);
        break;
    case S_WIFI:
        break;
    }
    return MENU_STAY;
}

bool menu_saver_allowed(void) { return s_screen != S_EXPORT && s_screen != S_WIFI; }

void menu_open_main(void) { s_direct = false; go(S_MAIN, 0); }
void menu_open_used(void) { count_used(); s_direct = true; go(S_USED, 0); }

menu_result_t menu_event(event_t ev)
{
    if (ev == EV_MENU) {                               // close from anywhere
        if (s_screen == S_WIFI) export_stop();
        return MENU_CLOSE;
    }
    if (s_screen == S_CODE) {                          // character dial
        int n = (int)strlen(s_cands);
        switch (ev) {
        case EV_ENC_CW: case EV_DOWN: if (n) { s_cand = (s_cand + 1) % n; s_version++; } break;
        case EV_ENC_CCW: case EV_UP:  if (n) { s_cand = (s_cand + n - 1) % n; s_version++; } break;
        case EV_ENC_PRESS: case EV_RIGHT: return select();
        case EV_CENTER: case EV_LEFT:
            if (s_code[0]) { s_code[strlen(s_code) - 1] = 0; s_cand = 0; code_refresh(); }
            else return back();
            break;
        default: break;
        }
        return MENU_STAY;
    }
    bool colors = s_screen == S_COLORS;
    switch (ev) {
    case EV_UP: case EV_ENC_CCW: move(-1); break;
    case EV_DOWN: case EV_ENC_CW: move(+1); break;
    case EV_LEFT:
        if (colors) show_sel_centered(palette_family_jump(pal(), s_sel, -1));
        else return back();
        break;
    case EV_RIGHT:
        if (colors) show_sel_centered(palette_family_jump(pal(), s_sel, +1));
        else return select();
        break;
    case EV_ENC_PRESS: return select();
    case EV_CENTER: return back();
    default: break;
    }
    return MENU_STAY;
}
