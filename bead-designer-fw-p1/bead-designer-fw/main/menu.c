// Menu screens (opened with MENU; SIDE opens "Colours in pattern" directly).
//
//   Main ── Brand ............ pick a palette; placed beads + slots are remapped
//        │                      to the nearest colour of the new brand
//        ├─ Edit shortlist ─── slot ─┬─ Browse colours (list, family jump)
//        │                           └─ Enter code (dial characters, OK)
//        ├─ Colours in pattern  (counts; press = load into the selected slot)
//        └─ Export ............ Wi-Fi export running while shown
//
// Controls: up/down or encoder turn = move; encoder press or right = select;
// D-pad centre (or left, except in colour lists) = back; MENU = close.
// In colour lists left/right jump colour families.
#include "menu.h"
#include "ui.h"
#include "palettes.h"
#include "store.h"
#include "export.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define VISIBLE_ROWS 9     // must match render.c (rows below the title band)

typedef enum { S_MAIN, S_BRAND, S_SLOTS, S_SLOT, S_COLORS, S_CODE, S_USED, S_EXPORT } screen_t;

static screen_t s_screen;
static int s_sel, s_top;
static unsigned s_version;
static int s_edit;                 // slot being edited (S_SLOT / S_COLORS / S_CODE)
static bool s_direct;              // opened straight into a screen (SIDE): back closes

// Code entry
#define CODE_MAX 5
static char s_code[CODE_MAX + 1];
static char s_cands[24];           // valid next characters; '\x01' = OK
static int s_cand;
#define OK_CHAR '\x01'

// Colours in pattern (sorted by count)
static uint8_t s_used[256];
static int s_used_n;
static int s_count[256];

static const palette_t *pal(void) { return &PALETTES[UI.palette]; }

static void go(screen_t sc, int sel)
{
    s_screen = sc; s_sel = sel; s_top = 0; s_version++;
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
    case S_MAIN:   return 4;
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
    case S_EXPORT: return 4;
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
    switch (s_screen) {
    case S_MAIN: {
        static const char *const L[] = { "Brand", "Edit shortlist", "Colours used", "Export" };
        snprintf(r->left, sizeof r->left, "%s", L[i]);
        if (i == 0) snprintf(r->right, sizeof r->right, "%s", pal()->brand);
        if (i == 2) { count_used(); snprintf(r->right, sizeof r->right, "%d", s_used_n); }
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
    case S_EXPORT: {
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
    v->sel = (s_screen == S_CODE || s_screen == S_EXPORT) ? -1 : s_sel;
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
    case S_EXPORT: snprintf(v->title, sizeof v->title, "Export"); break;
    }
}

// ---- navigation ---------------------------------------------------------------

static void move(int d)
{
    int n = row_count();
    if (n <= 0) return;
    s_sel = (s_sel + d + n) % n;                      // wrap
    if (s_sel < s_top) s_top = s_sel;
    if (s_sel >= s_top + VISIBLE_ROWS) s_top = s_sel - VISIBLE_ROWS + 1;
}

static void show_sel_centered(int sel)
{
    s_sel = sel;
    s_top = sel - VISIBLE_ROWS / 2;
    int n = row_count();
    if (s_top > n - VISIBLE_ROWS) s_top = n - VISIBLE_ROWS;
    if (s_top < 0) s_top = 0;
}

static menu_result_t back(void)
{
    switch (s_screen) {
    case S_MAIN:   return MENU_CLOSE;
    case S_BRAND:  go(S_MAIN, 0); break;
    case S_SLOTS:  go(S_MAIN, 1); break;
    case S_SLOT:   go(S_SLOTS, s_edit); break;
    case S_COLORS: go(S_SLOT, 0); break;
    case S_CODE:   go(S_SLOT, 1); break;
    case S_USED:   if (s_direct) return MENU_CLOSE; go(S_MAIN, 2); break;
    case S_EXPORT: export_stop(); go(S_MAIN, 3); break;
    }
    return MENU_STAY;
}

static menu_result_t select(void)
{
    switch (s_screen) {
    case S_MAIN:
        if (s_sel == 0) go(S_BRAND, UI.palette);
        else if (s_sel == 1) go(S_SLOTS, UI.slot);
        else if (s_sel == 2) { count_used(); go(S_USED, 0); s_direct = false; }
        else { store_flush(); export_start(); go(S_EXPORT, -1); }
        break;
    case S_BRAND:
        set_brand(s_sel);
        go(S_MAIN, 0);
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
    case S_EXPORT:
        break;
    }
    return MENU_STAY;
}

void menu_open_main(void) { s_direct = false; go(S_MAIN, 0); }
void menu_open_used(void) { count_used(); s_direct = true; go(S_USED, 0); }

menu_result_t menu_event(event_t ev)
{
    if (ev == EV_MENU) {                               // close from anywhere
        if (s_screen == S_EXPORT) export_stop();
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
