// Encoders via the ESP32 pulse counter (PCNT): hardware quadrature decode,
// x4 counting, one UI step per detent (PEC11R: 24 detents = 24 cycles/rev,
// so 4 counts per detent). RC filters (10k+10k+10nF) sit in front of A/B;
// the PCNT glitch filter adds a second line of defence.
// Switches/buttons: GPIO ISR -> queue, debounced by timestamp in here.

#include "input.h"
#include "board.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "sdkconfig.h"
#if CONFIG_BEAD_SERIAL_KEYS
#include "driver/uart.h"
static void serial_task(void *arg);
#endif

#define COUNTS_PER_DETENT 4
#define DEBOUNCE_US       15000

static QueueHandle_t s_q;
static pcnt_unit_handle_t s_unit_l, s_unit_r;
static int s_accum_l, s_accum_r;

typedef struct { int pin; event_t ev; int64_t last_us; } btn_t;
static btn_t s_btns[] = {
    { PIN_ENC_L_SW, EV_PUSH_L, 0 }, { PIN_ENC_R_SW, EV_PUSH_R, 0 },
    { PIN_BTN_MENU, EV_BTN_MENU, 0 }, { PIN_BTN_SIDE, EV_BTN_SIDE, 0 },
};

static void IRAM_ATTR btn_isr(void *arg)
{
    btn_t *b = (btn_t *)arg;
    int64_t now = esp_timer_get_time();
    if (now - b->last_us < DEBOUNCE_US) return;
    b->last_us = now;
    BaseType_t hp = pdFALSE;
    xQueueSendFromISR(s_q, &b->ev, &hp);
    if (hp) portYIELD_FROM_ISR();
}

static pcnt_unit_handle_t make_encoder(int pin_a, int pin_b)
{
    pcnt_unit_config_t ucfg = { .low_limit = -1000, .high_limit = 1000,
                                .flags.accum_count = true };
    pcnt_unit_handle_t unit;
    ESP_ERROR_CHECK(pcnt_new_unit(&ucfg, &unit));
    pcnt_glitch_filter_config_t f = { .max_glitch_ns = 1000 };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(unit, &f));

    pcnt_chan_config_t c1 = { .edge_gpio_num = pin_a, .level_gpio_num = pin_b };
    pcnt_channel_handle_t ch1;
    ESP_ERROR_CHECK(pcnt_new_channel(unit, &c1, &ch1));
    pcnt_channel_set_edge_action(ch1, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                      PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(ch1, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                       PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_chan_config_t c2 = { .edge_gpio_num = pin_b, .level_gpio_num = pin_a };
    pcnt_channel_handle_t ch2;
    ESP_ERROR_CHECK(pcnt_new_channel(unit, &c2, &ch2));
    pcnt_channel_set_edge_action(ch2, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                      PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(ch2, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                       PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    ESP_ERROR_CHECK(pcnt_unit_enable(unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(unit));
    ESP_ERROR_CHECK(pcnt_unit_start(unit));
    return unit;
}

static void poll_encoder(pcnt_unit_handle_t u, int *accum, event_t cw, event_t ccw)
{
    if (!u) return;
    int count = 0;
    pcnt_unit_get_count(u, &count);
    pcnt_unit_clear_count(u);
    *accum += count;
    while (*accum >= COUNTS_PER_DETENT)  { *accum -= COUNTS_PER_DETENT; event_t e = cw;  xQueueSend(s_q, &e, 0); }
    while (*accum <= -COUNTS_PER_DETENT) { *accum += COUNTS_PER_DETENT; event_t e = ccw; xQueueSend(s_q, &e, 0); }
}

static void poll_task(void *arg)
{
    (void)arg;
    for (;;) {
        poll_encoder(s_unit_l, &s_accum_l, EV_ENC_L_CW, EV_ENC_L_CCW);
        poll_encoder(s_unit_r, &s_accum_r, EV_ENC_R_CW, EV_ENC_R_CCW);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void input_init(void)
{
    s_q = xQueueCreate(32, sizeof(event_t));
    if (PIN_ENC_L_A >= 0) s_unit_l = make_encoder(PIN_ENC_L_A, PIN_ENC_L_B);
    if (PIN_ENC_R_A >= 0) s_unit_r = make_encoder(PIN_ENC_R_A, PIN_ENC_R_B);

    gpio_install_isr_service(0);
    for (unsigned i = 0; i < sizeof s_btns / sizeof *s_btns; i++) {
        if (s_btns[i].pin < 0) continue;
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << s_btns[i].pin,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE, // external 10k also fitted
            .intr_type = GPIO_INTR_NEGEDGE,   // active low
        };
        gpio_config(&io);
        gpio_isr_handler_add(s_btns[i].pin, btn_isr, &s_btns[i]);
    }
    xTaskCreate(poll_task, "enc_poll", 2048, NULL, 10, NULL);
#if CONFIG_BEAD_SERIAL_KEYS
    // UART0 is also the log console; the driver only takes over its RX side here.
    if (uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0) == ESP_OK)
        xTaskCreate(serial_task, "keys", 2560, NULL, 5, NULL);
#endif
}

void input_inject(event_t ev)
{
    xQueueSend(s_q, &ev, 0);
}

#if CONFIG_BEAD_SERIAL_KEYS
// Bench input: single keys typed into `idf.py monitor` become UI events, so the
// whole UI can be driven with no encoders or buttons wired. CR/LF are ignored.
static const char *KEY_HELP =
    "a/d=knob L ccw/cw  w/s=knob R ccw/cw  space|p=place  x=erase  "
    "m=menu  o=settings  t=colour bars  ?=help";

static void serial_task(void *arg)
{
    (void)arg;
    ESP_LOGI("keys", "%s", KEY_HELP);
    for (;;) {
        uint8_t c;
        if (uart_read_bytes(UART_NUM_0, &c, 1, portMAX_DELAY) != 1) continue;
        event_t ev;
        switch (c) {
        case 'd': ev = EV_ENC_L_CW;  break;
        case 'a': ev = EV_ENC_L_CCW; break;
        case 's': ev = EV_ENC_R_CW;  break;
        case 'w': ev = EV_ENC_R_CCW; break;
        case ' ': case 'p': ev = EV_PUSH_R; break;
        case 'x': ev = EV_PUSH_L;    break;
        case 'm': ev = EV_BTN_MENU;  break;
        case 'o': ev = EV_BTN_SIDE;  break;
        case 't': ev = EV_TEST_BARS; break;
        case '?': ESP_LOGI("keys", "%s", KEY_HELP); continue;
        default: continue;           // CR, LF and unmapped keys
        }
        input_inject(ev);
    }
}
#endif

int input_wait(event_t *ev, uint32_t timeout_ms)
{
    return xQueueReceive(s_q, ev, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}
