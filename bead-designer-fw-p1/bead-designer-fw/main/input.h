#pragma once
#include <stdint.h>

// Semantic input events. The input layer turns raw switches and the encoder into
// these, so the UI never deals with debouncing, timing or gestures.
typedef enum {
    EV_UP, EV_DOWN, EV_LEFT, EV_RIGHT,   // D-pad (auto-repeat while held)
    EV_CENTER,                           // D-pad centre, short press: erase
    EV_CENTER_LONG,                      // D-pad centre held >= LONG_PRESS_MS: eyedropper
    EV_ENC_CW, EV_ENC_CCW,               // encoder turned (not pressed): next/prev slot
    EV_ENC_PRESS,                        // encoder pressed and released WITHOUT turning: place
    EV_BROWSE_CW, EV_BROWSE_CCW,         // encoder turned WHILE pressed: browse catalogue
    EV_BROWSE_END,                       // encoder released after browsing: keep colour
    EV_MENU, EV_SIDE,
    EV_TICK,                             // periodic (idle dimming, battery poll)
    EV_TEST_BARS,                        // bench: colour test pattern (serial key 't')
    EV__COUNT
} event_t;

extern const char *const EVENT_NAMES[EV__COUNT];

void input_init(void);
// Blocks up to timeout_ms; returns 1 with *ev set, 0 on timeout.
int input_wait(event_t *ev, uint32_t timeout_ms);
// Queue an event as if it came from the hardware (serial keys, tests).
void input_inject(event_t ev);
