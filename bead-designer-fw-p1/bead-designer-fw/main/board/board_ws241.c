#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32S3
// Waveshare ESP32-S3-Touch-AMOLED-2.41 Rev 2.0 (board bring-up 2026-11-17).
//
// Sources: Waveshare V2 repo (github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.41-V2),
// schematic V2.0 and examples/esp-idf (display_bsp.c, 07_EX_GPIO).
//
// Display: 450x600 AMOLED, RM690B0 (driven like the SH8601) over QSPI:
//   CS 9, CLK 10, D0..D3 11..14, TE 21 (unused). Reset is on the TCA9554 expander
//   (EXIO0), not a GPIO. The panel sits upside down in the case (DISPLAY_ROT180) and
//   its visible columns start at RAM column 16 (LCD_X_GAP).
//
// Native resolution (UI update stage 1, 2026-11-20): the UI is drawn at the panel's
//   450 x 600. Draws go into a shadow framebuffer (PSRAM, big-endian RGB565, also
//   used by the test dump) and grow a dirty rectangle; present() sends it band by
//   band through two internal DMA buffers, filling one while the other is on the
//   bus. Every panel window is widened to even columns and rows, which the
//   controller requires. (Until 2026-11-20 the UI was 225 x 300, pixel-doubled.)
//   The screen saver's two-colour bitmap stays 225 x 300 (SAVER_W x SAVER_H) and is
//   still shown 2x2.
//
// I2C (GPIO47 SDA / GPIO48 SCL) carries the TCA9554 at 0x20:
//   P0 OLED reset (out), P1 touch reset (out; pulsed low to wake the touch IC),
//   P2 RTC_INT, P3/P4 IMU INT (in), P5 MENU, P6 SIDE (in, J8-6/8), P7 USB_PRESENT
//   (in, J8-10, control PCB D revB4). Inputs have ~100k internal pull-ups.
//
// Touch: FT6336U at 0x38 on the same bus, used only by the Browse colours list
//   (input.c). Polled over I2C; its INT (GPIO3) is not used. Hibernated otherwise.
// Not yet used: RTC, IMU.

#include "board.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "board_ws241";
static void battery_adc_init(void);
static char s_status[160];                          // reprinted on request (serial '?')
const char *board_status(void) { return s_status; }

#define I2C_SDA     47
#define I2C_SCL     48
#define TCA_ADDR    0x20
#define TCA_IN      0x00
#define TCA_OUT     0x01
#define TCA_CFG     0x03
#define TCA_P_OLED_RST  0
#define TCA_P_TP_RST    1

#define LCD_HOST    SPI2_HOST
#define LCD_CS      9
#define LCD_CLK     10
#define LCD_D0      11
#define LCD_D1      12
#define LCD_D2      13
#define LCD_D3      14
#define LCD_PCLK_HZ (80 * 1000 * 1000)            // 40 MHz in Waveshare's demo; 80 doubles the frame rate
#define LCD_RAM_W   480                             // RM690B0 column RAM; 450 columns are visible
#define LCD_X_GAP   16                              // RAM column = native column + gap (mod 480); 16 = Waveshare CASET start 0x10, confirmed with the G ruler 2026-11-17

#define PANEL_W     DISPLAY_W                       // 450
#define PANEL_H     DISPLAY_H                       // 600
#define MSCALE      (PANEL_W / SAVER_W)             // saver bitmap px -> MSCALE x MSCALE panel px
_Static_assert(SAVER_W * MSCALE == PANEL_W && SAVER_H * MSCALE == PANEL_H && MSCALE == 2,
               "the saver bitmap is expanded 2x2");
#define BAND_ROWS   24                              // panel rows per DMA transfer (even); 2 x 21.6 KB internal
#define BAND_PX     (PANEL_W * BAND_ROWS)

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_tca;
static i2c_master_dev_handle_t s_tp;               // FT6336U touch controller
bool board_touch_power(bool on);
#define TP_ADDR     0x38
#define TP_ROT180   DISPLAY_ROT180                  // touch axes follow the panel (Waveshare: same native orientation)
static uint8_t s_tca_out = 0xFF;
static bool s_tca_ok;

static esp_lcd_panel_io_handle_t s_io;
static uint16_t *s_fb;                              // DISPLAY_W x DISPLAY_H, big-endian: drawn into
static uint16_t *s_snapbuf[2];                      // two snapshots: one being sent, one being filled
static uint16_t *s_snap;                            // the one the display task is sending
static int s_fill_k;                                // index of the one present() fills next
static int s_qx0, s_qy0, s_qx1, s_qy1;              // previous present's dirty rectangle
static SemaphoreHandle_t s_idle;                    // given while the display task is not sending
static TaskHandle_t s_task;
static int s_px0, s_py0, s_px1, s_py1;              // rectangle handed to the display task
static uint16_t *s_band[2];                         // internal DMA buffers (ping-pong)
static SemaphoreHandle_t s_done;
static int s_inflight;
static SemaphoreHandle_t s_lock;                    // shadow framebuffer + bus
static int s_dx0 = DISPLAY_W, s_dy0 = DISPLAY_H, s_dx1, s_dy1;   // dirty rectangle, not yet presented

// ---- TCA9554 ---------------------------------------------------------------------
static esp_err_t tca_write(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_tca, b, 2, 50);
}

static esp_err_t tca_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_tca, &reg, 1, val, 1, 50);
}

static void tca_set(int pin, int level)
{
    if (level) s_tca_out |= (uint8_t)(1u << pin); else s_tca_out &= (uint8_t)~(1u << pin);
    tca_write(TCA_OUT, s_tca_out);
}

static void expander_init(void)
{
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0, .sda_io_num = I2C_SDA, .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_i2c));
    i2c_device_config_t dev = { .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                .device_address = TCA_ADDR, .scl_speed_hz = 400000 };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c, &dev, &s_tca));
    i2c_device_config_t tp = { .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                               .device_address = TP_ADDR, .scl_speed_hz = 400000 };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c, &tp, &s_tp));
    // Outputs first (both resets high), then make only P0/P1 outputs.
    s_tca_ok = tca_write(TCA_OUT, s_tca_out) == ESP_OK &&
               tca_write(TCA_CFG, (uint8_t)~((1u << TCA_P_OLED_RST) | (1u << TCA_P_TP_RST))) == ESP_OK;
    uint8_t in = 0;
    if (s_tca_ok) s_tca_ok = tca_read(TCA_IN, &in) == ESP_OK;
    if (s_tca_ok) ESP_LOGI(TAG, "TCA9554 ok at 0x%02x, inputs 0x%02x", TCA_ADDR, in);
    // The FT6336U does not answer I2C after power-on until it has been reset
    // (measured 2026-11-22): pulse EXIO1 now; input.c hibernates it once it is up
    // (~300 ms, by when the display init is done).
    if (s_tca_ok) board_touch_power(true);
    else          ESP_LOGE(TAG, "TCA9554 not answering at 0x%02x", TCA_ADDR);
}

int board_exio_read(void)
{
    if (!s_tca_ok) return -1;
    uint8_t v;
    return tca_read(TCA_IN, &v) == ESP_OK ? v : -1;
}

// ---- FT6336U touch ---------------------------------------------------------------
// Waking needs a reset (EXIO1 low 10 ms); the first report comes ~300 ms later.
// Hibernate (PMODE 0xA5 = 3) draws ~55 uA against ~4 mA active.
bool board_touch_power(bool on)
{
    if (!s_tca_ok) return false;
    if (on) {
        tca_set(TCA_P_TP_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        tca_set(TCA_P_TP_RST, 1);
        return true;
    }
    uint8_t b[2] = { 0xA5, 0x03 };
    return i2c_master_transmit(s_tp, b, 2, 20) == ESP_OK;
}

int board_touch_read(int *x, int *y)
{
    uint8_t reg = 0x02, b[5];                       // TD_STATUS, P1_XH, P1_XL, P1_YH, P1_YL
    if (i2c_master_transmit_receive(s_tp, &reg, 1, b, sizeof b, 20) != ESP_OK) return -1;
    int n = b[0] & 0x0F;
    if (n == 0 || n > 2 || (b[1] >> 6) == 1) return 0;   // no point, or event "lift up"
    int tx = (b[1] & 0x0F) << 8 | b[2], ty = (b[3] & 0x0F) << 8 | b[4];
    if (TP_ROT180) { tx = PANEL_W - 1 - tx; ty = PANEL_H - 1 - ty; }
    *x = tx < 0 ? 0 : tx >= PANEL_W ? PANEL_W - 1 : tx;
    *y = ty < 0 ? 0 : ty >= PANEL_H ? PANEL_H - 1 : ty;
    return 1;
}

// ---- power -----------------------------------------------------------------------
void board_early_init(void)
{
    // Hold power on when running from battery. Do this before anything else.
    gpio_config_t latch = { .pin_bit_mask = 1ULL << PIN_PWR_LATCH, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&latch);
    gpio_set_level(PIN_PWR_LATCH, 1);
    ESP_LOGI(TAG, "Waveshare 2.41 Rev 2.0; power latch held high (GPIO%d)", PIN_PWR_LATCH);
    s_lock = xSemaphoreCreateMutex();
    expander_init();
    battery_adc_init();
}

// ---- battery: BAT / 3 (R21 200k / R23 100k) on GPIO17 = ADC2 ------------------------
static adc_oneshot_unit_handle_t s_adc;
static adc_channel_t s_adc_ch;
static adc_cali_handle_t s_cali;

static void battery_adc_init(void)
{
    adc_unit_t unit;
    if (adc_oneshot_io_to_channel(PIN_BAT_ADC, &unit, &s_adc_ch) != ESP_OK) return;
    adc_oneshot_unit_init_cfg_t u = { .unit_id = unit };
    if (adc_oneshot_new_unit(&u, &s_adc) != ESP_OK) { s_adc = NULL; return; }
    adc_oneshot_chan_cfg_t c = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    adc_oneshot_config_channel(s_adc, s_adc_ch, &c);
    adc_cali_curve_fitting_config_t cal = { .unit_id = unit, .chan = s_adc_ch,
                                            .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) s_cali = NULL;
}

// Average of 8 calibrated readings x3. -1 while Wi-Fi holds ADC2.
int board_battery_mv(void)
{
    if (!s_adc) return -1;
    int sum = 0, n = 0;
    for (int i = 0; i < 8; i++) {
        int raw, mv;
        if (adc_oneshot_read(s_adc, s_adc_ch, &raw) != ESP_OK) continue;
        if (!s_cali || adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) mv = raw * 3100 / 4095;
        sum += mv; n++;
    }
    return n ? sum / n * 3 : -1;
}

void board_power_off(void)
{
    ESP_LOGW(TAG, "power off: dropping the latch");
    gpio_set_level(PIN_PWR_LATCH, 0);              // on battery: off once PWR is released
    vTaskDelay(pdMS_TO_TICKS(400));
    // Still running: USB keeps the board powered. Soft off: wait for PWR to be
    // released (it would wake us at once), then deep sleep until it is pressed.
    ESP_LOGW(TAG, "still powered (USB): soft off, PWR wakes");
    board_touch_power(false);
    board_display_sleep(true);
    vTaskDelay(pdMS_TO_TICKS(150));
    while (gpio_get_level(PIN_PWR_KEY) == 0) vTaskDelay(pdMS_TO_TICKS(20));
    esp_sleep_enable_ext0_wakeup(PIN_PWR_KEY, 0);
    esp_deep_sleep_start();
}

// ---- RM690B0 over QSPI --------------------------------------------------------------
// SH8601-style framing: 32-bit "command" = opcode << 24 | register << 8.
//   0x02 = single-line write of a register + parameters, 0x32 = quad pixel write.
#define CMD(reg)   ((0x02u << 24) | ((uint32_t)(reg) << 8))
#define PIXELS     ((0x32u << 24) | (0x2Cu << 8))   // RAMWR: start at the window origin
#define PIXELS_CONT ((0x32u << 24) | (0x3Cu << 8))  // RAMWR continue: carry on where the last band ended

static void lcd_cmd(uint8_t reg, const uint8_t *p, size_t n)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, CMD(reg), p, n));
}

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    (void)io; (void)ev; (void)ctx;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &hp);
    return hp == pdTRUE;
}

static void wait_inflight(int max)
{
    while (s_inflight > max) { xSemaphoreTake(s_done, portMAX_DELAY); s_inflight--; }
}

// Window in RAM coordinates (inclusive). tx_param first waits for queued pixel
// transfers, so the band before this one is fully sent when it returns.
static void set_window(int c0, int r0, int c1, int r1)
{
    uint8_t c[4] = { c0 >> 8, c0 & 0xFF, c1 >> 8, c1 & 0xFF };
    uint8_t r[4] = { r0 >> 8, r0 & 0xFF, r1 >> 8, r1 & 0xFF };
    lcd_cmd(0x2A, c, 4);
    lcd_cmd(0x2B, r, 4);
}

// Fill native panel rows [y, y+n) x columns [nx0, nx0+w) from the snapshot (or the
// saver bitmap, expanded 2x2). nx0 and w are even. Each snapshot row is copied out
// of PSRAM in one burst.
static uint16_t s_row[DISPLAY_W];                     // internal copy of one snapshot row

// Two-colour (1 bit per saver pixel) mode for the screen saver: it draws into
// s_mono_draw in internal RAM; present() copies that 8.4 KB bitmap instead of
// 540 KB of RGB565, and the display task expands bits to the two colours.
#define MONO_STRIDE ((SAVER_W + 7) / 8)
#define MONO_BYTES  (MONO_STRIDE * SAVER_H)
static uint8_t *s_mono_draw, *s_mono_snap[2];
static uint8_t *s_mono_job;                           // bitmap the display task sends (NULL = RGB565 job)
static bool s_mono_on;
static uint16_t s_mono_fg, s_mono_bg;                 // RGB565 (native order)
static uint32_t s_job_fg, s_job_bg;                   // the job's colours, doubled big-endian pixels
static void fill_band(uint16_t *dst, int nx0, int w, int y, int n)
{
    for (int r = 0; r < n; r++) {
        int ny = y + r;
        uint16_t *out = dst + r * w;
        if (s_mono_job) {                             // two-colour job: expand bits 2x2
            if (r > 0 && ((ny ^ (ny - 1)) & ~1) == 0) {   // same bitmap row as the line above
                memcpy(out, out - w, w * sizeof(uint16_t));
                continue;
            }
            uint32_t *o = (uint32_t *)out;
            int lw = w / MSCALE;
#if DISPLAY_ROT180
            int ly = (PANEL_H - 1 - ny) / MSCALE, lx = (PANEL_W - 1 - nx0) / MSCALE;
            const uint8_t *row = s_mono_job + ly * MONO_STRIDE;
            for (int i = 0; i < lw; i++, lx--) o[i] = (row[lx >> 3] >> (lx & 7) & 1) ? s_job_fg : s_job_bg;
#else
            int ly = ny / MSCALE, lx = nx0 / MSCALE;
            const uint8_t *row = s_mono_job + ly * MONO_STRIDE;
            for (int i = 0; i < lw; i++, lx++) o[i] = (row[lx >> 3] >> (lx & 7) & 1) ? s_job_fg : s_job_bg;
#endif
            continue;
        }
#if DISPLAY_ROT180
        // Panel mounted upside down: native (x, y) shows framebuffer (W-1-x, H-1-y).
        int ly = PANEL_H - 1 - ny, lx_hi = PANEL_W - 1 - nx0;
        memcpy(s_row, s_snap + ly * DISPLAY_W + (lx_hi - w + 1), w * sizeof(uint16_t));
        for (int i = 0; i < w; i++) out[i] = s_row[w - 1 - i];
#else
        memcpy(out, s_snap + ny * DISPLAY_W + nx0, w * sizeof(uint16_t));
#endif
    }
}

// Send native columns [nx0, nx1) x rows [py0, py1). RAM column = native + gap
// (mod 480); callers split where that wraps.
static void send_native(int nx0, int nx1, int py0, int py1)
{
    int w = nx1 - nx0;
    int rows = BAND_PX / w;
    if (rows > py1 - py0) rows = py1 - py0;
    rows &= ~1;
    int c0 = ((nx0 + LCD_X_GAP) % LCD_RAM_W + LCD_RAM_W) % LCD_RAM_W;
    // One window for the whole rectangle; bands after the first use "write
    // continue", so the bus never idles for window commands between bands.
    int k = 0;
    fill_band(s_band[0], nx0, w, py0, (py1 - py0 < rows) ? (py1 - py0) : rows);
    set_window(c0, py0, c0 + w - 1, py1 - 1);
    for (int y = py0; y < py1; y += rows, k ^= 1) {
        int n = (py1 - y < rows) ? (py1 - y) : rows;
        if (y != py0) {
            wait_inflight(1);                         // s_band[k]'s previous use is done
            fill_band(s_band[k], nx0, w, y, n);       // overlaps the other band's DMA
        }
        if (esp_lcd_panel_io_tx_color(s_io, y == py0 ? PIXELS : PIXELS_CONT, s_band[k],
                                      w * n * sizeof(uint16_t)) == ESP_OK)
            s_inflight++;
    }
    wait_inflight(0);
}

// Copy framebuffer rectangle [x0,x1) x [y0,y1) to the panel, widened to even
// columns and rows (the controller needs even-aligned windows; W and H are even).
static void flush(int x0, int y0, int x1, int y1)
{
    if (x1 > DISPLAY_W) x1 = DISPLAY_W;
    if (y1 > DISPLAY_H) y1 = DISPLAY_H;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 <= x0 || y1 <= y0) return;
    x0 &= ~1; y0 &= ~1; x1 = (x1 + 1) & ~1; y1 = (y1 + 1) & ~1;
#if DISPLAY_ROT180
    int nx0 = PANEL_W - x1, nx1 = PANEL_W - x0;
    int py0 = PANEL_H - y1, py1 = PANEL_H - y0;
#else
    int nx0 = x0, nx1 = x1, py0 = y0, py1 = y1;
#endif
    int wrap = LCD_X_GAP < 0 ? -LCD_X_GAP : LCD_RAM_W - LCD_X_GAP;   // native column where RAM wraps to 0
    if (nx0 < wrap && nx1 > wrap) {
        send_native(nx0, wrap, py0, py1);
        send_native(wrap, nx1, py0, py1);
    } else {
        send_native(nx0, nx1, py0, py1);
    }
}

static void panel_hw_init(void);
static volatile int s_sleep_req;                 // 1 = panel sleep, 2 = panel wake (display task)
static volatile int s_bright = -1;                  // pending brightness (0..255), -1 = none
static SemaphoreHandle_t s_ready;

// Display task (core 1): sends the snapshot while the UI draws the next frame.
static void display_task(void *arg)
{
    (void)arg;
    panel_hw_init();
    xSemaphoreGive(s_ready);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int b = s_bright;
        if (b >= 0) { uint8_t v = (uint8_t)b; s_bright = -1; lcd_cmd(0x51, &v, 1); }
        int sr = s_sleep_req;
        if (sr) {
            s_sleep_req = 0;
            if (sr == 1) { lcd_cmd(0x28, NULL, 0); vTaskDelay(pdMS_TO_TICKS(20)); lcd_cmd(0x10, NULL, 0); }
            else { lcd_cmd(0x11, NULL, 0); vTaskDelay(pdMS_TO_TICKS(120)); lcd_cmd(0x29, NULL, 0); }
            xSemaphoreGive(s_ready);                  // request done
        }
        if (s_px1 > s_px0) {
            flush(s_px0, s_py0, s_px1, s_py1);
            s_px0 = s_px1 = 0;
            xSemaphoreGive(s_idle);
        }
    }
}

static const struct { uint8_t reg; uint8_t p[4]; uint8_t n; uint16_t ms; } INIT[] = {
    { 0xFE, { 0x20 }, 1, 0 },          // vendor page 0x20
    { 0x26, { 0x0A }, 1, 0 },
    { 0x24, { 0x80 }, 1, 0 },
    { 0xFE, { 0x00 }, 1, 0 },          // back to the user page
    { 0x3A, { 0x55 }, 1, 0 },          // 16 bpp RGB565
    { 0xC2, { 0x00 }, 1, 10 },
    { 0x35, { 0x00 }, 0, 0 },          // tearing effect line on (TE unused for now)
    { 0x51, { 0x00 }, 1, 10 },         // brightness 0 while the RAM is still random
    { 0x11, { 0x00 }, 0, 80 },         // sleep out
    { 0x29, { 0x00 }, 0, 10 },         // display on
};

static void battery_adc_init(void);

static void dsp_init(void)
{
    s_done = xSemaphoreCreateCounting(64, 0);
    s_fb = heap_caps_calloc(DISPLAY_W * DISPLAY_H, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    for (int i = 0; i < 2; i++)
        s_snapbuf[i] = heap_caps_calloc(DISPLAY_W * DISPLAY_H, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    s_snap = s_snapbuf[0]; s_fill_k = 1;
    s_idle = xSemaphoreCreateBinary();
    for (int i = 0; i < 2; i++)
        s_band[i] = heap_caps_malloc(BAND_PX * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_fb || !s_snapbuf[0] || !s_snapbuf[1] || !s_band[0] || !s_band[1]) { ESP_LOGE(TAG, "no memory for the framebuffer"); abort(); }
    // All SPI work, including bus setup (which places the SPI interrupt on the
    // calling core), happens on the display task: issuing transfers from one core
    // while the bus interrupt lived on the other hung the bus lock (2026-11-17).
    s_ready = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 12, &s_task, 1);
    xSemaphoreTake(s_ready, portMAX_DELAY);
}

static void panel_hw_init(void)
{
    // Panel reset through the expander (EXIO0): high, low, high (Waveshare timing).
    if (s_tca_ok) {
        tca_set(TCA_P_OLED_RST, 1); vTaskDelay(pdMS_TO_TICKS(20));
        tca_set(TCA_P_OLED_RST, 0); vTaskDelay(pdMS_TO_TICKS(20));
        tca_set(TCA_P_OLED_RST, 1); vTaskDelay(pdMS_TO_TICKS(120));
    }

    spi_bus_config_t bus = {
        .sclk_io_num = LCD_CLK, .data0_io_num = LCD_D0, .data1_io_num = LCD_D1,
        .data2_io_num = LCD_D2, .data3_io_num = LCD_D3,
        .max_transfer_sz = BAND_PX * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io = {
        .cs_gpio_num = LCD_CS, .dc_gpio_num = -1, .spi_mode = 0,
        .pclk_hz = LCD_PCLK_HZ, .trans_queue_depth = 4,
        .on_color_trans_done = on_color_done,
        .lcd_cmd_bits = 32, .lcd_param_bits = 8,
        .flags.quad_mode = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io, &s_io));

    for (unsigned i = 0; i < sizeof INIT / sizeof *INIT; i++) {
        lcd_cmd(INIT[i].reg, INIT[i].p, INIT[i].n);
        if (INIT[i].ms) vTaskDelay(pdMS_TO_TICKS(INIT[i].ms));
    }
    // Clear all 480 RAM columns (including the 30 hidden ones) before light-up.
    memset(s_band[0], 0, BAND_PX * sizeof(uint16_t));
    for (int y = 0; y < PANEL_H; y += BAND_PX / LCD_RAM_W) {
        int n = BAND_PX / LCD_RAM_W;
        if (n > PANEL_H - y) n = PANEL_H - y;
        set_window(0, y, LCD_RAM_W - 1, y + n - 1);
        if (esp_lcd_panel_io_tx_color(s_io, PIXELS, s_band[0], LCD_RAM_W * n * sizeof(uint16_t)) == ESP_OK)
            s_inflight++;
        wait_inflight(0);
    }
    uint8_t full = 0xFF;
    lcd_cmd(0x51, &full, 1);
    int64_t t0 = esp_timer_get_time();
    flush(0, 0, DISPLAY_W, DISPLAY_H);
    ESP_LOGI(TAG, "RM690B0 ready: %dx%d native, QSPI %d MHz, full frame %lld us",
             PANEL_W, PANEL_H, LCD_PCLK_HZ / 1000000, (long long)(esp_timer_get_time() - t0));
    snprintf(s_status, sizeof s_status, "Waveshare 2.41 Rev 2.0; %s; RM690B0 ready %dx%d native",
             s_tca_ok ? "TCA9554 ok at 0x20" : "TCA9554 MISSING", PANEL_W, PANEL_H);
    xSemaphoreGive(s_idle);
}

// Draws only touch the shadow framebuffer and grow the dirty rectangle; present()
// sends it to the panel once per UI pass, so screen changes appear in one go.
static void dirty(int x0, int y0, int x1, int y1)
{
    if (x0 < s_dx0) s_dx0 = x0;
    if (y0 < s_dy0) s_dy0 = y0;
    if (x1 > s_dx1) s_dx1 = x1;
    if (y1 > s_dy1) s_dy1 = y1;
}

// Copy framebuffer rectangle [x0,x1) x [y0,y1) into a snapshot.
static void snap_copy(uint16_t *dst, int x0, int y0, int x1, int y1)
{
    for (int y = y0; y < y1; y++)
        memcpy(dst + y * DISPLAY_W + x0, s_fb + y * DISPLAY_W + x0, (x1 - x0) * sizeof(uint16_t));
}

// Hand the dirty rectangle to the display task. The frame is copied into the
// snapshot that is not being sent (overlapping the send of the previous frame);
// that snapshot is two frames old, so the copy covers this frame's and the last
// frame's dirty rectangles. Then wait for the display task to finish the previous
// frame, hand over, and return: the next frame is drawn while this one goes out.
static void dsp_present(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    // flush() widens every window to even columns and rows; widen here first, so the
    // snapshot copy covers every pixel that gets sent. (Copying only the odd-edged
    // rectangle sent a 1 px line from a two-frames-old snapshot along its edges:
    // stale cursor ring / halo lines around cells after moving, 2026-11-21.)
    if (s_dx1 > s_dx0 && s_dy1 > s_dy0) {
        s_dx0 &= ~1; s_dy0 &= ~1;
        s_dx1 = (s_dx1 + 1) & ~1; s_dy1 = (s_dy1 + 1) & ~1;   // W and H are even: stays on screen
    }
    if (s_mono_on && s_dx1 > s_dx0 && s_dy1 > s_dy0) {
        uint8_t *dst = s_mono_snap[s_fill_k];
        memcpy(dst, s_mono_draw, MONO_BYTES);
        xSemaphoreTake(s_idle, portMAX_DELAY);       // previous frame fully sent
        uint32_t fg = (uint16_t)((s_mono_fg >> 8) | (s_mono_fg << 8)), bg = (uint16_t)((s_mono_bg >> 8) | (s_mono_bg << 8));
        s_job_fg = fg | fg << 16; s_job_bg = bg | bg << 16;
        s_mono_job = dst;
        s_px0 = s_dx0; s_py0 = s_dy0; s_px1 = s_dx1; s_py1 = s_dy1;
        xTaskNotifyGive(s_task);
        s_fill_k ^= 1;
        s_qx0 = 0; s_qy0 = 0; s_qx1 = DISPLAY_W; s_qy1 = DISPLAY_H;   // RGB565 snapshots are stale now
        s_dx0 = DISPLAY_W; s_dy0 = DISPLAY_H; s_dx1 = s_dy1 = 0;
        xSemaphoreGive(s_lock);
        return;
    }
    if (s_dx1 > s_dx0 && s_dy1 > s_dy0) {
        uint16_t *dst = s_snapbuf[s_fill_k];
        if (s_mono_job) {                             // leaving two-colour mode: snapshots
            snap_copy(dst, 0, 0, DISPLAY_W, DISPLAY_H);   // are stale, copy everything
        } else {
            // This frame's rectangle and the previous frame's, each on its own: their
            // bounding box can be most of the screen (the cursor near the top and the
            // strip at the bottom, during motion), and PSRAM copies cost ~25 us a row.
            snap_copy(dst, s_dx0, s_dy0, s_dx1, s_dy1);
            bool inside = s_qx0 >= s_dx0 && s_qy0 >= s_dy0 && s_qx1 <= s_dx1 && s_qy1 <= s_dy1;
            if (s_qx1 > s_qx0 && !inside) snap_copy(dst, s_qx0, s_qy0, s_qx1, s_qy1);   // (already copied if inside)
        }
        xSemaphoreTake(s_idle, portMAX_DELAY);       // previous frame fully sent
        s_mono_job = NULL;
        s_snap = dst;
        s_px0 = s_dx0; s_py0 = s_dy0; s_px1 = s_dx1; s_py1 = s_dy1;
        xTaskNotifyGive(s_task);
        s_fill_k ^= 1;
        s_qx0 = s_dx0; s_qy0 = s_dy0; s_qx1 = s_dx1; s_qy1 = s_dy1;
    } else {
        s_qx0 = s_qy0 = s_qx1 = s_qy1 = 0;           // nothing new: both snapshots now equal
    }
    s_dx0 = DISPLAY_W; s_dy0 = DISPLAY_H; s_dx1 = s_dy1 = 0;
    xSemaphoreGive(s_lock);
}

static bool clip(int *x, int *y, int *w, int *h)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > DISPLAY_W) *w = DISPLAY_W - *x;
    if (*y + *h > DISPLAY_H) *h = DISPLAY_H - *y;
    return *w > 0 && *h > 0;
}

static void dsp_fill(int x, int y, int w, int h, uint16_t c)
{
    if (!clip(&x, &y, &w, &h)) return;
    uint16_t be = (uint16_t)((c >> 8) | (c << 8));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int r = 0; r < h; r++) {
        uint16_t *row = s_fb + (y + r) * DISPLAY_W + x;
        for (int i = 0; i < w; i++) row[i] = be;
    }
    dirty(x, y, x + w, y + h);
    xSemaphoreGive(s_lock);
}

static void dsp_blit(int x, int y, int w, int h, const uint16_t *p)   // p: big-endian
{
    int x0 = x, y0 = y, w0 = w;
    if (!clip(&x, &y, &w, &h)) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int r = 0; r < h; r++)
        memcpy(s_fb + (y + r) * DISPLAY_W + x, p + (y - y0 + r) * w0 + (x - x0), w * sizeof(uint16_t));
    dirty(x, y, x + w, y + h);
    xSemaphoreGive(s_lock);
}

static void dsp_bright(uint8_t pct)
{
    if (pct > 100) pct = 100;
    s_bright = pct * 255 / 100;                       // sent by the display task
    xTaskNotifyGive(s_task);
}

// Panel sleep (display off + sleep in: microamps) / wake. Waits until done.
void board_display_sleep(bool sleep)
{
    xSemaphoreTake(s_idle, portMAX_DELAY);            // not in the middle of a frame
    s_sleep_req = sleep ? 1 : 2;
    xTaskNotifyGive(s_task);
    xSemaphoreTake(s_ready, portMAX_DELAY);
    xSemaphoreGive(s_idle);
}

const uint16_t *board_framebuffer(void) { return s_fb; }
uint16_t *board_fb_draw(void) { return s_mono_on ? NULL : s_fb; }
void board_fb_lock(bool lock) { if (lock) xSemaphoreTake(s_lock, portMAX_DELAY); else xSemaphoreGive(s_lock); }
void board_fb_dirty(int x, int y, int w, int h)      // caller holds the lock
{
    if (s_mono_on) { x *= MSCALE; y *= MSCALE; w *= MSCALE; h *= MSCALE; }   // saver coordinates
    dirty(x, y, x + w, y + h);
}

uint8_t *board_mono_begin(uint16_t fg, uint16_t bg)
{
    if (!s_mono_draw) {
        s_mono_draw = heap_caps_calloc(1, MONO_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        for (int i = 0; i < 2; i++) s_mono_snap[i] = heap_caps_calloc(1, MONO_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!s_mono_draw || !s_mono_snap[0] || !s_mono_snap[1]) return NULL;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_mono_draw, 0, MONO_BYTES);
    s_mono_fg = fg; s_mono_bg = bg; s_mono_on = true;
    dirty(0, 0, DISPLAY_W, DISPLAY_H);
    xSemaphoreGive(s_lock);
    return s_mono_draw;
}

void board_mono_color(uint16_t fg, uint16_t bg) { s_mono_fg = fg; s_mono_bg = bg; }

void board_mono_end(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_mono_on = false;
    dirty(0, 0, DISPLAY_W, DISPLAY_H);
    xSemaphoreGive(s_lock);
}

// Test dump while in two-colour mode: rebuild the RGB565 framebuffer from the bitmap.
void board_fb_sync(void)
{
    if (!s_mono_on) return;
    for (int y = 0; y < DISPLAY_H; y++)
        for (int x = 0; x < DISPLAY_W; x++) {
            int mx = x / MSCALE, my = y / MSCALE;
            uint16_t c = (s_mono_draw[my * MONO_STRIDE + (mx >> 3)] >> (mx & 7) & 1) ? s_mono_fg : s_mono_bg;
            s_fb[y * DISPLAY_W + x] = (uint16_t)((c >> 8) | (c << 8));
        }
}

static const display_ops_t OPS = { dsp_init, dsp_fill, dsp_blit, dsp_bright, dsp_present };
const display_ops_t *board_display(void) { return &OPS; }
#endif
