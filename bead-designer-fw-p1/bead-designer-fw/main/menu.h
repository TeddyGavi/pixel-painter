#pragma once
// Menu: a stack of list screens (title + rows). menu.c owns the logic and builds
// rows on demand; render.c draws any screen through this generic view.
#include <stdbool.h>
#include <stdint.h>
#include "input.h"

// What selecting a row does, for its System 7 marker on the native 450x600 UI:
// a submenu triangle, or "..." after the label for rows that open a dialog.
typedef enum { ROW_PLAIN, ROW_SUB, ROW_DIALOG } row_kind_t;

typedef struct {
    char left[44];
    char right[20];
    int32_t swatch;        // RGB565 colour chip, or -1 = none
    const uint8_t *thumb;  // non-NULL: thumb_n x thumb_n cells drawn as a tiny pattern
    int thumb_n;
    int thumb_pal;         // palette of thumb's cells
    bool slot;             // saved-slot row: framed thumbnail (empty frame if no thumb)
    char sub[24];          // second text line (double-height rows)
    row_kind_t kind;
} menu_row_t;

typedef struct {
    char title[40];
    int count;             // rows
    int sel;               // highlighted row, -1 = none
    int top;               // first visible row
    unsigned version;      // bumps whenever row contents change (full redraw)
    const uint8_t *qr;     // non-NULL: draw this QR code (qrcodegen) instead of rows
    char hint[40];         // with qr: dim line under the code
    bool bar;              // draw the position bar (touch-scrolled list)
} menu_view_t;

typedef enum { MENU_STAY, MENU_CLOSE } menu_result_t;

void menu_open_main(void);          // MENU from drawing
void menu_open_used(void);          // SIDE from drawing: colours in pattern
menu_result_t menu_event(event_t ev);
void menu_view(menu_view_t *v);
void menu_row(int i, menu_row_t *r);
int menu_row_units(int i);
bool menu_touch_scroll(void);       // the screen scrolls by touch (Browse colours)
bool menu_saver_allowed(void);      // false on the export screens (QR, Wi-Fi)          // row height in ROW_H units (slot rows are 2)
