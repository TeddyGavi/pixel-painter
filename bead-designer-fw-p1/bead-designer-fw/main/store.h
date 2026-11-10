#pragma once
#include <stdbool.h>

#define STORE_SLOTS 8

void store_init(void);
bool store_save_slot(int slot);
bool store_load_slot(int slot);
void store_load_autosave(void);   // restore working pattern at boot
void store_autosave_lazy(void);   // debounced write of working pattern
