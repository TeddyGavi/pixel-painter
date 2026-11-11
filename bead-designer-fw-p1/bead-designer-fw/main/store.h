#pragma once
#include <stdbool.h>

#define STORE_SLOTS 8

void store_init(void);
bool store_save_slot(int slot);
bool store_load_slot(int slot);
void store_load_autosave(void);   // restore working pattern at boot
// Autosave of the working pattern. Edits only mark it dirty; the write happens
// once the user has paused (store_autosave_if_idle, called while no input is
// pending), never between a keypress and its redraw. Flash writes stall both
// CPUs (cache off), from ~1 ms up to tens of ms when NVS erases a sector.
void store_mark_dirty(void);      // a cell changed
bool store_pending(void);         // unsaved changes waiting for a pause
void store_autosave_if_idle(void);// save if dirty and idle long enough
void store_flush(void);           // save now if dirty (before export, sleep, ...)
