#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32
// Freenove FNK0114A 2.4" 240x320 IPS, ST7789 on HSPI (interim dev board).
// Pins from Freenove's schematic + TFT_eSPI setup (see board.h).
// SPI clock kept at 40 MHz for bring-up (Freenove's setup uses 80 MHz).

#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "board_freenove";
static esp_lcd_panel_handle_t s_panel;
static adc_oneshot_unit_handle_t s_adc;

#define LINE_PX 2400                    // DMA chunk: 10 rows of 240
static uint16_t *s_line;                // DMA-capable scratch buffer

// esp_lcd_panel_draw_bitmap() only QUEUES the SPI DMA transfer and returns at once
// (up to trans_queue_depth in flight). The pixel buffer must not change until the
// transfer has gone out, or later fills overwrite colours still being sent (seen on
// hardware as half-drawn cells and a highlight frame showing only its top rows).
// Each finished colour transfer gives s_done; wait_idle() drains them all.
static SemaphoreHandle_t s_done;
static int s_inflight;                  // queued colour transfers not yet finished

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    (void)io; (void)ev; (void)ctx;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &hp);
    return hp == pdTRUE;
}

static void wait_idle(void)
{
    while (s_inflight > 0) {
        xSemaphoreTake(s_done, portMAX_DELAY);
        s_inflight--;
    }
}

static void queue_bitmap(int x0, int y0, int x1, int y1, const void *px)
{
    if (esp_lcd_panel_draw_bitmap(s_panel, x0, y0, x1, y1, px) == ESP_OK) s_inflight++;
}

void board_early_init(void)
{
    ESP_LOGI(TAG, "Freenove 2.4\" interim board");
    adc_oneshot_unit_init_cfg_t cfg = { .unit_id = ADC_UNIT_1 }; // GPIO34 = ADC1_CH6
    adc_oneshot_new_unit(&cfg, &s_adc);
    adc_oneshot_chan_cfg_t ch = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    adc_oneshot_config_channel(s_adc, ADC_CHANNEL_6, &ch);
}

// Battery features are off on this interim bench board: no cell is fitted, and a
// floating, uncalibrated reading could trigger the low/critical policy (a false
// shutdown). The ADC stays set up for bench checks.
int board_battery_mv(void) { return -1; }

static void dsp_init(void)
{
    gpio_config_t bl = { .pin_bit_mask = 1ULL << LCD_BL, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bl);
    gpio_set_level(LCD_BL, 0);

    spi_bus_config_t bus = {
        .mosi_io_num = LCD_MOSI, .sclk_io_num = LCD_SCLK, .miso_io_num = -1,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = LINE_PX * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    s_done = xSemaphoreCreateCounting(64, 0); // > max chunks per fill (32)
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = LCD_DC, .cs_gpio_num = LCD_CS,
        .pclk_hz = 40 * 1000 * 1000, .spi_mode = 0, .trans_queue_depth = 4,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .on_color_trans_done = on_color_done,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io));

    esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = -1,           // RST tied to EN
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &pcfg, &s_panel));
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true); // IPS ST7789 panels are usually inverted; flip if colours look wrong
    // FRCTRL2 (C6h): column inversion (NLA=111) at 90 Hz (RTNA=05h). The chip
    // default is dot inversion at 60 Hz, under which a 1 px checker background puts
    // every light pixel on the same polarity and the whole screen pulses at 30 Hz
    // ("shake"). Column inversion cancels that for all our textures, and 90 Hz
    // pushes any remainder above flicker fusion. ST7789VW datasheet 9.2.18.
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0xC6, (const uint8_t[]){ 0xE5 }, 1));
    esp_lcd_panel_disp_on_off(s_panel, true);

    s_line = heap_caps_malloc(LINE_PX * sizeof(uint16_t), MALLOC_CAP_DMA);
    gpio_set_level(LCD_BL, 1);
}

static void dsp_fill(int x, int y, int w, int h, uint16_t c)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > DISPLAY_W) w = DISPLAY_W - x;
    if (y + h > DISPLAY_H) h = DISPLAY_H - y;
    uint16_t sw = (uint16_t)((c >> 8) | (c << 8)); // panel expects big-endian RGB565
    int rows = LINE_PX / w; if (rows < 1) rows = 1;
    wait_idle();                        // previous fill may still be reading s_line
    for (int i = 0; i < rows * w; i++) s_line[i] = sw;
    for (int yy = y; yy < y + h; yy += rows) {  // chunks share one colour: queue freely
        int n = (y + h - yy < rows) ? (y + h - yy) : rows;
        queue_bitmap(x, yy, x + w, yy + n, s_line);
    }
}

static void dsp_blit(int x, int y, int w, int h, const uint16_t *p)
{
    wait_idle();
    queue_bitmap(x, y, x + w, y + h, p); // caller supplies big-endian
    wait_idle();                         // caller may reuse its buffer on return
}

static void dsp_bright(uint8_t pct) { gpio_set_level(LCD_BL, pct ? 1 : 0); } // P1: LEDC PWM

int board_exio_read(void) { return -1; }
void board_power_off(void)                         // no power latch: soft off
{
    ESP_LOGW(TAG, "power off: no latch on this board, deep sleep (reset to wake)");
    gpio_set_level(LCD_BL, 0);
    esp_deep_sleep_start();
}
void board_display_sleep(bool sleep) { gpio_set_level(LCD_BL, sleep ? 0 : 1); }
const char *board_status(void) { return "Freenove FNK0114A"; }        // no I/O expander
const uint16_t *board_framebuffer(void) { return NULL; }   // no shadow framebuffer
uint16_t *board_fb_draw(void) { return NULL; }
void board_fb_lock(bool lock) { (void)lock; }
void board_fb_dirty(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
uint8_t *board_mono_begin(uint16_t fg, uint16_t bg) { (void)fg; (void)bg; return NULL; }
void board_mono_color(uint16_t fg, uint16_t bg) { (void)fg; (void)bg; }
void board_mono_end(void) {}
void board_fb_sync(void) {}
bool board_touch_power(bool on) { (void)on; return true; }     // no touch panel
int  board_touch_read(int *x, int *y) { (void)x; (void)y; return -1; }

static const display_ops_t OPS = { dsp_init, dsp_fill, dsp_blit, dsp_bright, NULL };
const display_ops_t *board_display(void) { return &OPS; }
#endif
