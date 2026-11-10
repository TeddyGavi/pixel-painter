#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32
// Freenove ESP32 CYD 2.4" 240x320 IPS (interim development board).
// P1 TODO, on arrival / pinout research:
//   1. Fill the display SPI pins + driver (ILI9341 or ST7789) below.
//   2. Set PIN_ENC_L_* and PIN_BTN_MENU in board.h to the free GPIOs.
//   3. Implement fill_rect/blit with esp_lcd (spi) — simple 4-wire SPI panel.
// Backlight is usually a single GPIO (PWM for dimming).

#include "board.h"
#include "esp_log.h"

static const char *TAG = "board_freenove";

void board_early_init(void)
{
    ESP_LOGI(TAG, "freenove interim board (no power latch)");
}

int board_battery_mv(void) { return -1; }

static void dsp_init(void) { /* P1 TODO: esp_lcd SPI panel init */ }
static void dsp_fill(int x, int y, int w, int h, uint16_t c)
{ (void)x; (void)y; (void)w; (void)h; (void)c; /* P1 TODO */ }
static void dsp_blit(int x, int y, int w, int h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; /* P1 TODO */ }
static void dsp_bright(uint8_t pct) { (void)pct; /* P1 TODO: BL PWM */ }

static const display_ops_t OPS = { dsp_init, dsp_fill, dsp_blit, dsp_bright };
const display_ops_t *board_display(void) { return &OPS; }
#endif
