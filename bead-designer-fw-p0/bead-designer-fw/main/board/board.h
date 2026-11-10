#pragma once
// Board abstraction. Everything hardware-specific lives behind this header.
// board_ws241.c  : Waveshare ESP32-S3-Touch-AMOLED-2.41 V2 (product, pins VERIFIED
//                  against the V2 schematic, spec Rev A.5 section 5.1)
// board_freenove.c: Freenove ESP32 CYD 2.4" (interim, pins TBV)

#include <stdint.h>
#include <stdbool.h>

#if CONFIG_IDF_TARGET_ESP32S3
// ---- Waveshare 2.41 V2, header J8 ----
#define PIN_ENC_L_A     1   // J8-3
#define PIN_ENC_L_B     7   // J8-13
#define PIN_ENC_L_SW    8   // J8-15, active low
#define PIN_ENC_R_A     38  // J8-19
#define PIN_ENC_R_B     39  // J8-21 (MTCK: no external JTAG while in use)
#define PIN_ENC_R_SW    40  // J8-23, active low
#define PIN_BTN_MENU    41  // J8-25, active low
#define PIN_BTN_SIDE    42  // J8-24, active low
#define PIN_PWR_KEY     15  // onboard PWR key (Key_BAT)
#define PIN_PWR_LATCH   16  // BAT_Control: drive HIGH at boot or battery power drops
#define PIN_BAT_ADC     17  // battery voltage / 3 (200k/100k divider)
#define HAVE_PWR_LATCH  1
#define DISPLAY_W       450 // portrait (panel is 600x450, rotated in firmware)
#define DISPLAY_H       600
#elif CONFIG_IDF_TARGET_ESP32
// ---- Freenove CYD 2.4" : ALL TBV until board + pinout research arrive ----
#define PIN_ENC_L_A     (-1) // TBV
#define PIN_ENC_L_B     (-1) // TBV
#define PIN_ENC_L_SW    (-1) // TBV
#define PIN_ENC_R_A     (-1) // not wired on interim board
#define PIN_ENC_R_B     (-1)
#define PIN_ENC_R_SW    (-1)
#define PIN_BTN_MENU    (-1) // TBV
#define PIN_BTN_SIDE    (-1) // use BOOT key if short on pins
#define PIN_PWR_KEY     (-1)
#define PIN_PWR_LATCH   (-1)
#define PIN_BAT_ADC     (-1)
#define HAVE_PWR_LATCH  0
#define DISPLAY_W       240 // portrait
#define DISPLAY_H       320
#endif

// Display operations each board implements.
typedef struct {
    void (*init)(void);
    // Fill an axis-aligned rectangle with one RGB565 colour (portrait coords).
    void (*fill_rect)(int x, int y, int w, int h, uint16_t rgb565);
    // Push a small RGB565 bitmap (glyphs, icons).
    void (*blit)(int x, int y, int w, int h, const uint16_t *px);
    void (*set_brightness)(uint8_t pct); // burn-in care: dim chrome, sleep idle
} display_ops_t;

const display_ops_t *board_display(void);
void board_early_init(void);   // power latch FIRST, then rails/quirks
int  board_battery_mv(void);   // -1 if unsupported
