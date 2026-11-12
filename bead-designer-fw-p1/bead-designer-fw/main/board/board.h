#pragma once
// Board abstraction. Everything hardware-specific lives behind this header.
// board_ws241.c  : Waveshare ESP32-S3-Touch-AMOLED-2.41 V2 (product, pins VERIFIED
//                  against the V2 schematic, spec Rev A.5 section 5.1)
// board_freenove.c: Freenove ESP32 CYD 2.4" (interim, pins TBV)

#include <stdint.h>
#include <stdbool.h>
#include "sdkconfig.h"   // CONFIG_IDF_TARGET_* used below

// Inputs (all switches active low): a 5-way D-pad (cursor; centre = erase,
// long-press = eyedropper), one push-encoder (turn = colour slot, press = place,
// press-and-turn = browse the catalogue), MENU and SIDE. -1 = not wired.
#if CONFIG_IDF_TARGET_ESP32S3
// ---- Waveshare 2.41 V2 + Rev B control board, D variant (layout-revB.md),
//      via header J8 ----
#define PIN_DPAD_UP     38  // J8-19
#define PIN_DPAD_DOWN   39  // J8-21 (MTCK: no external JTAG while in use)
#define PIN_DPAD_LEFT   40  // J8-23
#define PIN_DPAD_RIGHT  41  // J8-25
#define PIN_DPAD_CENTER 1   // J8-3
#define PIN_ENC_A       7   // J8-13
#define PIN_ENC_B       8   // J8-15
#define PIN_ENC_SW      42  // J8-24
#define PIN_BTN_MENU    (-1) // EXIO5 on the onboard I/O expander: TODO expander driver
#define PIN_BTN_SIDE    (-1) // EXIO6 on the onboard I/O expander: TODO expander driver
#define PIN_DPAD_LADDER (-1) // D-pad uses its own GPIOs here
#define PIN_PWR_KEY     15  // onboard PWR key (Key_BAT)
#define PIN_PWR_LATCH   16  // BAT_Control: drive HIGH at boot or battery power drops
#define PIN_BAT_ADC     17  // battery voltage / 3 (200k/100k divider)
#define HAVE_PWR_LATCH  1
#define DISPLAY_W       450 // portrait (panel is 600x450, rotated in firmware)
#define DISPLAY_H       600
#elif CONFIG_IDF_TARGET_ESP32
// ---- Freenove FNK0114A 2.4" ST7789 (ESP32-WROOM-32E), from Freenove's
//      2.4inch_ESP32-32E_Display_Schematic.pdf. Only 2 free GPIOs exist. ----
// Breadboard bench wiring (BENCH.md). Only 5 GPIOs reach connectors, so the
// 5-way D-pad is a resistor ladder on one ADC pin. P3 = the SD-card bus: no card.
#define PIN_DPAD_UP     (-1) // all 5 directions: ladder on PIN_DPAD_LADDER
#define PIN_DPAD_DOWN   (-1)
#define PIN_DPAD_LEFT   (-1)
#define PIN_DPAD_RIGHT  (-1)
#define PIN_DPAD_CENTER (-1)
#define PIN_DPAD_LADDER 27   // P3 pin 4: ADC2_CH7 (unreadable while Wi-Fi runs)
#define PIN_ENC_A       35   // P4 pin 2; input-only, EXTERNAL 10k pull-up required
#define PIN_ENC_B       23   // P3 pin 1 (SPI_MOSI)
#define PIN_ENC_SW      19   // P3 pin 2 (SPI_MISO)
#define PIN_BTN_MENU    18   // P3 pin 3 (SPI_CLK)
#define PIN_BTN_SIDE    0    // onboard BOOT key (10k pull-up); don't hold it during reset
#define PIN_PWR_KEY     (-1)
#define PIN_PWR_LATCH   (-1)
#define PIN_BAT_ADC     34   // 100k/100k divider (x2)
// Display (ST7789, HSPI): MOSI 13, SCLK 14, CS 15, DC 2, RST tied to EN, BL 21 (high = on)
#define LCD_MOSI 13
#define LCD_SCLK 14
#define LCD_CS   15
#define LCD_DC   2
#define LCD_BL   21
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
