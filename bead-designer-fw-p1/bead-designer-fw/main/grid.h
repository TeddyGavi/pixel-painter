#pragma once
// Grid size changes (Pattern > Grid size; docs/ui-update-2.41 DECISIONS.md #6):
// growing is lossless (old content centred); shrinking keeps the window with the
// most beads (the centred one on ties). The board as it was before the last resize
// stays restorable, exactly, until the next change to the beads.
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int n;          // target size
    int lost;       // beads that would be cut off
    bool restore;   // going back to the board as it was before the last resize
} grid_plan_t;

grid_plan_t grid_plan(int n);              // what resizing the working pattern to n does
void grid_preview(int n, uint8_t *dst);    // the cells it would have (GRID_MAX^2 array)
void grid_apply(int n);                    // resize (or restore); moves the cursor along
