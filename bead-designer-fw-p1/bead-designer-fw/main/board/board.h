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
// LEFT/RIGHT are deliberately crossed against the PCB net names. Alps SKQUCAA010
// top view: B (pin 2) = LEFT, D (pin 5) = RIGHT, but control PCB Rev B wires
// pad 2 to net NAV_RIGHT (J8-25) and pad 5 to NAV_LEFT (J8-23). Fixed here
// rather than respinning the board (2026-11-15).
#define PIN_DPAD_LEFT   41  // J8-25, net NAV_RIGHT = SW3 pin 2 (B, physical left)
#define PIN_DPAD_RIGHT  40  // J8-23, net NAV_LEFT  = SW3 pin 5 (D, physical right)
#define PIN_DPAD_CENTER 1   // J8-3
#define PIN_ENC_A       7   // J8-13
#define PIN_ENC_B       8   // J8-15
#define PIN_ENC_SW      42  // J8-24
#define PIN_BTN_MENU    (-1) // on the TCA9554 expander instead: EXIO_BTN_MENU
#define PIN_BTN_SIDE    (-1) // on the TCA9554 expander instead: EXIO_BTN_SIDE
#define EXIO_BTN_MENU   5    // TCA9554 P5 (J8-6), polled over I2C by input.c
#define EXIO_BTN_SIDE   6    // TCA9554 P6 (J8-8)
#define EXIO_USB_PRESENT 7   // TCA9554 P7 (J8-10): high = USB 5 V present (R1 2.2k / R2 4.7k divider on the J8 adapter PCB, rev A). Not read yet; debounce ~100 ms
#define PIN_DPAD_LADDER (-1) // D-pad uses its own GPIOs here
#define PIN_PWR_KEY     15  // onboard PWR key (Key_BAT)
#define PIN_PWR_LATCH   16  // BAT_Control: drive HIGH at boot or battery power drops
#define PIN_BAT_ADC     17  // battery voltage / 3 (200k/100k divider)
#define HAVE_PWR_LATCH  1
#define DISPLAY_W       450 // native RM690B0 panel px (UI update stage 1, 2026-11-20; was 225 logical px doubled)
#define DISPLAY_ROT180  1   // panel is mounted upside down relative to the case (USB-C left): flip in the flush (checked on the board 2026-11-17)
#define DISPLAY_H       600
#define SAVER_W         225 // the screen saver keeps its 225x300 bitmap, each px shown 2x2
#define SAVER_H         300
#define UI_HIRES        1   // native-resolution UI: Soft-bead art, Chicago 3x Scale3x (docs/ui-update-2.41)
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
#define EXIO_USB_PRESENT (-1) // no USB sense on the Freenove
#define EXIO_BTN_MENU   (-1) // no I/O expander on the Freenove
#define EXIO_BTN_SIDE   (-1)
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
#define SAVER_W         DISPLAY_W
#define SAVER_H         DISPLAY_H
#define UI_HIRES        0
#endif

// Display operations each board implements.
typedef struct {
    void (*init)(void);
    // Fill an axis-aligned rectangle with one RGB565 colour (portrait coords).
    void (*fill_rect)(int x, int y, int w, int h, uint16_t rgb565);
    // Push a small RGB565 bitmap (glyphs, icons).
    void (*blit)(int x, int y, int w, int h, const uint16_t *px);
    void (*set_brightness)(uint8_t pct); // burn-in care: dim chrome, sleep idle
    // Optional (NULL = draws go straight to the panel): send everything drawn since
    // the last call in one go, so a screen change appears at once (no flicker).
    void (*present)(void);
} display_ops_t;

const display_ops_t *board_display(void);
void board_early_init(void);   // power latch FIRST, then rails/quirks
int  board_battery_mv(void);   // battery voltage (calibrated, averaged), -1 if unsupported/unreadable
// Switch off: drop the power latch. Where that can't cut power (USB connected, or
// a board without a latch) fall back to a "soft off": display off, deep sleep,
// woken by the PWR key as a fresh boot. Does not return.
void board_power_off(void);
void board_display_sleep(bool sleep); // panel off (low power) / back on; redraw after waking
int  board_exio_read(void);
const char *board_status(void); // one-line hardware status (reprinted by serial '?')    // I/O expander input port (bit n = EXIOn), -1 if none
// Test support: the shadow framebuffer (DISPLAY_W x DISPLAY_H, big-endian RGB565),
// or NULL on boards without one. Hold board_fb_lock(true) while reading it.
const uint16_t *board_framebuffer(void);
void board_fb_lock(bool lock);
// The same framebuffer for drawing into directly (menu rows), or NULL. Hold
// board_fb_lock(true) while writing and mark the area with board_fb_dirty.
uint16_t *board_fb_draw(void);
// After writing into the framebuffer directly (screen saver): mark the area changed.
void board_fb_dirty(int x, int y, int w, int h);
// Two-colour bitmap mode (screen saver): returns a cleared 1-bit-per-pixel bitmap
// (SAVER_W x SAVER_H, (SAVER_W+7)/8 bytes per row, bit x&7 of byte x>>3) shown in
// fg/bg, or NULL if the board has none. Mark changes with board_fb_dirty (under
// board_fb_lock); while the bitmap is on, board_fb_dirty takes SAVER coordinates.
uint8_t *board_mono_begin(uint16_t fg, uint16_t bg);
void board_mono_color(uint16_t fg, uint16_t bg);
void board_mono_end(void);
void board_fb_sync(void);      // test dump: bring the framebuffer up to date first
// Touch (FT6336U on the Waveshare; none on the Freenove). board_touch_power(true)
// resets the controller (first report ~300 ms later); false puts it in hibernate.
// Returns false if the controller did not answer. board_touch_read: 1 = a finger at
// (*x, *y) in display coordinates, 0 = no finger, -1 = no controller / read failed.
bool board_touch_power(bool on);
int  board_touch_read(int *x, int *y);
