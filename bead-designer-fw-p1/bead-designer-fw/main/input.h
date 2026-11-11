#pragma once
#include <stdint.h>

typedef enum {
    EV_ENC_L_CW, EV_ENC_L_CCW,   // left knob = X
    EV_ENC_R_CW, EV_ENC_R_CCW,   // right knob = Y
    EV_PUSH_L, EV_PUSH_R,        // encoder push switches
    EV_BTN_MENU, EV_BTN_SIDE,
    EV_TICK,                     // periodic (idle dimming, battery poll)
    EV_TEST_BARS,                // bench: colour test pattern (serial key 't')
} event_t;

void input_init(void);
// Blocks up to timeout_ms; returns 1 with *ev set, 0 on timeout.
int input_wait(event_t *ev, uint32_t timeout_ms);
// Queue an event as if it came from the hardware (serial keys, tests).
void input_inject(event_t ev);
