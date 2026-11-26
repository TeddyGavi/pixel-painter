#pragma once
#include <stdint.h>

// Paint brush: hold the encoder push and move with the D-pad. The start cell and
// every cell moved onto are painted (EV_PAINT after each move); the release then
// places nothing extra.
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
    EV_PAINT,                            // paint brush: place at the cursor (encoder held + D-pad)
    EV_PWR,                              // PWR key short press: sleep / wake
    EV_PWR_HOLD,                         // PWR key held PWR_HOLD_MS: shut down
    EV_SCROLL,                           // touch drag/flick: scroll the list (input_take_scroll)
    EV_TAP,                              // touch tap: highlight the row at input_tap_y()
    EV__COUNT
} event_t;

extern const char *const EVENT_NAMES[EV__COUNT];

void input_init(void);
// Blocks up to timeout_ms; returns 1 with *ev set, 0 on timeout.
int input_wait(event_t *ev, uint32_t timeout_ms);
// Queue an event as if it came from the hardware (serial keys, tests).
void input_inject(event_t ev);

// Touch scrolling (only the Browse colours list uses it). The UI enables it while
// that list is on screen; otherwise touches are ignored and the controller sleeps.
// A drag scrolls one row per row of finger travel; a flick coasts on (at most 30
// rows/s, one row per redraw) and stops below 8 rows/s. Any button or encoder
// input stops a coast; a touch catches it.
#include <stdbool.h>
void input_touch_enable(bool on);   // cheap when unchanged: call every loop pass
int  input_take_scroll(void);       // rows to scroll since the last EV_SCROLL (+ = later rows)
int  input_tap_y(void);             // display y of the last EV_TAP
void input_touch_stop(void);        // the list hit its end: stop coasting
bool input_touch_busy(void);        // a finger is down or the list is coasting
