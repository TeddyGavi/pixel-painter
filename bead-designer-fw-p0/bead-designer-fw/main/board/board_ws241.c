#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32S3
// Waveshare ESP32-S3-Touch-AMOLED-2.41 V2.
// Display: 2.41" 600x450 AMOLED, RM690B0 over QSPI (GPIO9..14, TE=21).
// P2 TODO: port the RM690B0 init sequence + QSPI esp_lcd setup from
// Waveshare's V2 demo (LVGL-free path), then implement fill_rect/blit via
// esp_lcd_panel_draw_bitmap on a per-rect basis (no full framebuffer needed;
// 8 MB PSRAM available if a full buffer ever helps).

#include "board.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "board_ws241";
static adc_oneshot_unit_handle_t s_adc;

void board_early_init(void)
{
    // Hold power on when running from battery. Do this before anything else.
    gpio_config_t latch = {
        .pin_bit_mask = 1ULL << PIN_PWR_LATCH,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&latch);
    gpio_set_level(PIN_PWR_LATCH, 1);
    ESP_LOGI(TAG, "power latch held high (GPIO%d)", PIN_PWR_LATCH);

    adc_oneshot_unit_init_cfg_t cfg = { .unit_id = ADC_UNIT_2 }; // GPIO17 = ADC2_CH6 on S3
    adc_oneshot_new_unit(&cfg, &s_adc);
    adc_oneshot_chan_cfg_t ch = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    adc_oneshot_config_channel(s_adc, ADC_CHANNEL_6, &ch);
}

int board_battery_mv(void)
{
    int raw = 0;
    if (adc_oneshot_read(s_adc, ADC_CHANNEL_6, &raw) != ESP_OK) return -1;
    // 12-bit, 12 dB atten ~ 3.1 V full scale (calibrate in P2); divider = /3.
    return (raw * 3100 / 4095) * 3;
}

static void dsp_init(void) { /* P2 TODO: RM690B0 QSPI init */ }
static void dsp_fill(int x, int y, int w, int h, uint16_t c)
{ (void)x; (void)y; (void)w; (void)h; (void)c; /* P2 TODO */ }
static void dsp_blit(int x, int y, int w, int h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; /* P2 TODO */ }
static void dsp_bright(uint8_t pct) { (void)pct; /* P2 TODO: RM690B0 0x51 */ }

static const display_ops_t OPS = { dsp_init, dsp_fill, dsp_blit, dsp_bright };
const display_ops_t *board_display(void) { return &OPS; }
#endif
