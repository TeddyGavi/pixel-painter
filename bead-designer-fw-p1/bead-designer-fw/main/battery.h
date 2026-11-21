#pragma once
#include <stdbool.h>

// Battery state for the UI (battery.c). Readings are smoothed; call battery_poll()
// about every BATTERY_POLL_MS.
#define BATTERY_POLL_MS 10000

void battery_init(void);
void battery_poll(void);
int  battery_mv(void);        // smoothed battery voltage, -1 = unknown
int  battery_pct(void);       // 0..100 from a LiPo curve, -1 = unknown
bool battery_low(void);       // <= 20 %: power saving
bool battery_critical(void);  // below the shutdown level for 3 readings in a row
// Bench (serial key 'b'): step through simulated levels: off, 3950, 3640 (low),
// 3560 (alert), 3420 mV (critical). Returns the simulated mV, or -1 when off.
int  battery_sim_next(void);
void battery_history_print(void);   // bench (serial 'h'): recent readings
