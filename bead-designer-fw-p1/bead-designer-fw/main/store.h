#pragma once
#include <stdbool.h>
#include <stdint.h>

#define STORE_SLOTS 8

void store_init(void);            // NVS up; reads the save slots into RAM
void store_load_autosave(void);   // restore working pattern + display look at boot

// Save slots: snapshots of the working pattern (brand, shortlist, beads).
bool store_save_slot(int slot);   // working pattern -> slot; clears UI.unsaved
bool store_load_slot(int slot);   // slot -> working pattern; clears UI.unsaved
// Slot contents for the slot lists; false = empty slot. Any out pointer may be NULL.
// cells: n x n, row-major.
bool store_slot_info(int slot, int *palette, const uint8_t **cells, int *n, int *beads);

// Autosave of the working pattern. Edits only mark it dirty; the write happens
// once the user has paused (store_autosave_if_idle, called while no input is
// pending), never between a keypress and its redraw. Flash writes stall both
// CPUs (cache off), from ~1 ms up to tens of ms when NVS erases a sector.
void store_mark_dirty(void);      // working pattern changed
void store_mark_look_dirty(void); // device-wide display look changed
bool store_pending(void);         // unsaved changes waiting for a pause
void store_autosave_if_idle(void);// save if dirty and idle long enough
void store_flush(void);           // save now if dirty (before export, sleep, ...)
