#pragma once
// Screen saver: halftone particle animation shown after a period without input.
// Every SAVER_CYCLE_MS a new gradient shape, origin, motion and bead colour are
// picked and the dots flow from the old image into the new one.
#include <stdbool.h>
#include <stdint.h>

#define SAVER_CYCLE_MS 8000

// Idle timeout choices (Menu > Screensaver); UI.saver indexes these. 0 s = off.
#define SAVER_CHOICES 5
#define SAVER_DEFAULT 3   // 1 min
extern const uint16_t SAVER_TIMEOUT_S[SAVER_CHOICES];
extern const char *const SAVER_NAMES[SAVER_CHOICES];

void saver_start(void);     // take over the screen (caller redraws it on stop)
void saver_stop(void);
bool saver_active(void);
int  saver_next_ms(void);   // ms until saver_frame() wants to run
void saver_frame(void);     // step the animation and draw what changed

// Device tilt as a gravity direction in screen axes (+x right, +y down).
// Returns false when there is no tilt sensor (the origin is then random).
// Weak default; a sensor driver can override it.
bool saver_tilt(float *gx, float *gy);
