// Input layer: raw switches + one push-encoder -> semantic events (input.h).
//
// Encoder: ESP32 pulse counter (PCNT), hardware quadrature decode, x4 counting,
// one event per detent (PEC11R: 4 counts per detent). RC filters (10k+10k+10nF)
// sit in front of A/B; the PCNT glitch filter adds a second line of defence.
//
// Switches: polled every POLL_MS and debounced in software (a level must hold for
// DEBOUNCE_SAMPLES polls). Polling, rather than edge interrupts, gives clean press
// AND release edges plus reliable timing for long-press, auto-repeat and
// press-and-turn, which edge ISRs with contact bounce make fragile.
//
// D-pad as a resistor ladder (PIN_DPAD_LADDER >= 0, bench board): one ADC pin,
// pull-up 20k to 3V3; each direction pulls the node to GND through its own
// resistance, so each press gives a distinct voltage (BENCH.md):
//   centre 0 R -> 0 V, up 5k -> 0.66 V, down 10k -> 1.10 V, left 20k -> 1.65 V,
//   right 40k -> 2.20 V, none -> 3.3 V.
// Thresholds sit halfway between levels; the classified direction then goes
// through the same debounce/long-press/repeat logic as real switches.
//
// Gestures:
//   encoder press + release, no turn  -> EV_ENC_PRESS (place)
//   encoder turned while pressed      -> EV_BROWSE_CW/CCW ..., then EV_BROWSE_END on release
//   D-pad centre short / held         -> EV_CENTER on release / EV_CENTER_LONG at the threshold
//   D-pad directions                  -> one event on press, auto-repeat while held

#include "input.h"
#include "board.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <stdbool.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#if CONFIG_BEAD_SERIAL_KEYS
#include "driver/uart.h"
static void serial_task(void *arg);
#endif

#define COUNTS_PER_DETENT 4
#define POLL_MS           5
#define DEBOUNCE_SAMPLES  3     // 15 ms stable
#define LONG_PRESS_MS     600
#define REPEAT_DELAY_MS   400
#define REPEAT_MS         120

const char *const EVENT_NAMES[EV__COUNT] = {
    "UP", "DOWN", "LEFT", "RIGHT", "CENTER", "CENTER_LONG",
    "ENC_CW", "ENC_CCW", "ENC_PRESS", "BROWSE_CW", "BROWSE_CCW", "BROWSE_END",
    "MENU", "SIDE", "TICK", "TEST_BARS",
};

static QueueHandle_t s_q;
static pcnt_unit_handle_t s_enc;
static int s_accum;

typedef enum { K_DIR, K_CENTER, K_ENC, K_BTN } kind_t;
typedef enum { L_NONE, L_CENTER, L_UP, L_DOWN, L_LEFT, L_RIGHT } ladder_t;
typedef struct {
    int pin;
    kind_t kind;
    event_t ev;          // event on press (K_DIR, K_BTN)
    bool down;           // debounced state
    uint8_t same;        // consecutive samples disagreeing with `down`
    int64_t since_us;    // when `down` became true
    int64_t next_rep_us; // auto-repeat (K_DIR)
    bool long_fired;     // K_CENTER
    ladder_t ladder;     // ladder position when pin < 0 and a ladder is fitted
} sw_t;

static sw_t s_sw[] = {
    { .pin = PIN_DPAD_UP, .kind = K_DIR, .ev = EV_UP, .ladder = L_UP },
    { .pin = PIN_DPAD_DOWN, .kind = K_DIR, .ev = EV_DOWN, .ladder = L_DOWN },
    { .pin = PIN_DPAD_LEFT, .kind = K_DIR, .ev = EV_LEFT, .ladder = L_LEFT },
    { .pin = PIN_DPAD_RIGHT, .kind = K_DIR, .ev = EV_RIGHT, .ladder = L_RIGHT },
    { .pin = PIN_DPAD_CENTER, .kind = K_CENTER, .ev = EV_CENTER, .ladder = L_CENTER },
    { .pin = PIN_ENC_SW, .kind = K_ENC, .ev = EV_ENC_PRESS },
    { .pin = PIN_BTN_MENU, .kind = K_BTN, .ev = EV_MENU },
    { .pin = PIN_BTN_SIDE, .kind = K_BTN, .ev = EV_SIDE },
};
#define N_SW (sizeof s_sw / sizeof *s_sw)

static bool s_enc_down;     // encoder switch held
static bool s_enc_turned;   // ...and turned since it went down

static void emit(event_t e) { xQueueSend(s_q, &e, 0); }

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

static void detent(int dir)
{
    if (s_enc_down) {               // press-and-turn: browse the catalogue
        s_enc_turned = true;
        emit(dir > 0 ? EV_BROWSE_CW : EV_BROWSE_CCW);
    } else {
        emit(dir > 0 ? EV_ENC_CW : EV_ENC_CCW);
    }
}

static void poll_encoder(void)
{
    if (!s_enc) return;
    int count = 0;
    pcnt_unit_get_count(s_enc, &count);
    pcnt_unit_clear_count(s_enc);
    s_accum += count;
    while (s_accum >= COUNTS_PER_DETENT)  { s_accum -= COUNTS_PER_DETENT; detent(+1); }
    while (s_accum <= -COUNTS_PER_DETENT) { s_accum += COUNTS_PER_DETENT; detent(-1); }
}

static void on_press(sw_t *s, int64_t now)
{
    s->since_us = now;
    switch (s->kind) {
    case K_DIR:    emit(s->ev); s->next_rep_us = now + REPEAT_DELAY_MS * 1000; break;
    case K_CENTER: s->long_fired = false; break;           // decided on hold/release
    case K_ENC:    s_enc_down = true; s_enc_turned = false; break;
    case K_BTN:    emit(s->ev); break;
    }
}

static void on_release(sw_t *s)
{
    switch (s->kind) {
    case K_CENTER: if (!s->long_fired) emit(EV_CENTER); break;
    case K_ENC:
        s_enc_down = false;
        emit(s_enc_turned ? EV_BROWSE_END : EV_ENC_PRESS);
        break;
    default: break;
    }
}

static void on_held(sw_t *s, int64_t now)
{
    if (s->kind == K_DIR && now >= s->next_rep_us) {
        emit(s->ev);
        s->next_rep_us = now + REPEAT_MS * 1000;
    } else if (s->kind == K_CENTER && !s->long_fired &&
               now - s->since_us >= LONG_PRESS_MS * 1000) {
        s->long_fired = true;
        emit(EV_CENTER_LONG);
    }
}

// ---- resistor-ladder D-pad ----------------------------------------------------
static adc_oneshot_unit_handle_t s_ladder_adc;
static adc_channel_t s_ladder_ch;
static adc_cali_handle_t s_ladder_cali;
static bool s_ladder_ok;

static void ladder_init(void)
{
    if (PIN_DPAD_LADDER < 0) return;
    adc_unit_t unit;
    if (adc_oneshot_io_to_channel(PIN_DPAD_LADDER, &unit, &s_ladder_ch) != ESP_OK) return;
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = unit };
    if (adc_oneshot_new_unit(&ucfg, &s_ladder_adc) != ESP_OK) return;
    adc_oneshot_chan_cfg_t ccfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    if (adc_oneshot_config_channel(s_ladder_adc, s_ladder_ch, &ccfg) != ESP_OK) return;
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cal = { .unit_id = unit, .atten = ADC_ATTEN_DB_12,
                                           .bitwidth = ADC_BITWIDTH_12 };
    if (adc_cali_create_scheme_line_fitting(&cal, &s_ladder_cali) != ESP_OK) s_ladder_cali = NULL;
#elif ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cal = { .unit_id = unit, .chan = s_ladder_ch,
                                            .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_ladder_cali) != ESP_OK) s_ladder_cali = NULL;
#endif
    s_ladder_ok = true;
    ESP_LOGI("input", "D-pad ladder on GPIO%d (%s)", PIN_DPAD_LADDER,
             s_ladder_cali ? "calibrated" : "uncalibrated");
}

// Bench aid (serial key 'v'): log the ladder voltage whenever it moves by more than
// 50 mV, so the D-pad can be checked without a meter.
static volatile bool s_ladder_log;
static int s_ladder_last_mv = -1000;
static int64_t s_ladder_log_us;
static int s_ladder_prev_mv = -1000;   // previous sample, for the stability check

// A real ladder (with its 10 nF node cap) holds a level to within a few mV; an
// unconnected or half-built one floats and jumps by hundreds of mV between samples
// (measured on the Freenove: 130-550 mV every 5 ms). Only readings within
// LADDER_STABLE_MV of the previous sample count, so a floating pin never presses.
#define LADDER_STABLE_MV 80

// Current ladder position. A failed read (ADC2 is taken while Wi-Fi runs) = none.
static ladder_t ladder_read(void)
{
    int raw, mv;
    if (!s_ladder_ok || adc_oneshot_read(s_ladder_adc, s_ladder_ch, &raw) != ESP_OK)
        return L_NONE;
    if (!s_ladder_cali || adc_cali_raw_to_voltage(s_ladder_cali, raw, &mv) != ESP_OK)
        mv = raw * 3300 / 4095;
    bool stable = mv - s_ladder_prev_mv < LADDER_STABLE_MV &&
                  s_ladder_prev_mv - mv < LADDER_STABLE_MV;
    s_ladder_prev_mv = mv;
    int64_t now = esp_timer_get_time();
    if (s_ladder_log && (mv > s_ladder_last_mv + 50 || mv < s_ladder_last_mv - 50) &&
        now - s_ladder_log_us >= 100000) {               // at most 10 lines/s
        s_ladder_log_us = now;
        static const char *const NAME[] = { "none", "CENTRE", "UP", "DOWN", "LEFT", "RIGHT" };
        ladder_t l = mv < 330 ? L_CENTER : mv < 880 ? L_UP : mv < 1375 ? L_DOWN :
                     mv < 1925 ? L_LEFT : mv < 2750 ? L_RIGHT : L_NONE;
        ESP_LOGI("dpad", "GPIO%d = %4d mV -> %s%s", PIN_DPAD_LADDER, mv, NAME[l],
                 stable ? "" : " (unsteady: ignored)");
        s_ladder_last_mv = mv;
    }
    if (!stable) return L_NONE;
    return mv < 330 ? L_CENTER : mv < 880 ? L_UP : mv < 1375 ? L_DOWN :
           mv < 1925 ? L_LEFT : mv < 2750 ? L_RIGHT : L_NONE;
}

static void poll_switches(void)
{
    int64_t now = esp_timer_get_time();
    ladder_t lad = s_ladder_ok ? ladder_read() : L_NONE;
    for (unsigned i = 0; i < N_SW; i++) {
        sw_t *s = &s_sw[i];
        bool level_down;
        if (s->pin >= 0) level_down = gpio_get_level(s->pin) == 0;  // active low
        else if (s_ladder_ok && s->ladder != L_NONE) level_down = (lad == s->ladder);
        else continue;
        if (level_down != s->down) {
            if (++s->same >= DEBOUNCE_SAMPLES) {
                s->same = 0;
                s->down = level_down;
                if (s->down) on_press(s, now); else on_release(s);
            }
        } else {
            s->same = 0;
            if (s->down) on_held(s, now);
        }
    }
}

static void poll_task(void *arg)
{
    (void)arg;
    // Poll switches before the encoder so a press that starts together with a
    // turn is already "down" when the detent is classified.
    for (;;) {
        poll_switches();
        poll_encoder();
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void input_init(void)
{
    s_q = xQueueCreate(32, sizeof(event_t));
    if (PIN_ENC_A >= 0) s_enc = make_encoder(PIN_ENC_A, PIN_ENC_B);
    for (unsigned i = 0; i < N_SW; i++) {
        if (s_sw[i].pin < 0) continue;
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << s_sw[i].pin,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,   // external 10k also fitted
        };
        gpio_config(&io);
    }
    ladder_init();
    xTaskCreate(poll_task, "input", 3072, NULL, 10, NULL);
#if CONFIG_BEAD_SERIAL_KEYS
    // UART0 is also the log console; the driver only takes over its RX side here.
    if (uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0) == ESP_OK)
        xTaskCreate(serial_task, "keys", 2560, NULL, 5, NULL);
#endif
}

void input_inject(event_t ev) { emit(ev); }

int input_wait(event_t *ev, uint32_t timeout_ms)
{
    return xQueueReceive(s_q, ev, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

#if CONFIG_BEAD_SERIAL_KEYS
// Bench input: single keys typed into `idf.py monitor` stand in for the D-pad,
// encoder and buttons. CR/LF are ignored. A browse (J/L = turn while pressed)
// stays "held" through a/d (family jump) and ends on any other key or after
// BROWSE_IDLE_MS without browse keys, like releasing the encoder.
#define BROWSE_IDLE_MS 1500
static const char *KEY_HELP =
    "w/a/s/d=D-pad  x=centre(erase)  X=long centre(eyedropper)  j/l=encoder ccw/cw  "
    "k|p|space=encoder press(place)  J/L=press+turn(browse; a/d=family)  "
    "m=menu  o=side  t=colour bars  v=D-pad volts  ?=help";

static void serial_task(void *arg)
{
    (void)arg;
    bool browsing = false;
    ESP_LOGI("keys", "%s", KEY_HELP);
    for (;;) {
        uint8_t c;
        TickType_t wait = browsing ? pdMS_TO_TICKS(BROWSE_IDLE_MS) : portMAX_DELAY;
        if (uart_read_bytes(UART_NUM_0, &c, 1, wait) != 1) {
            if (browsing) { browsing = false; emit(EV_BROWSE_END); }
            continue;
        }
        if (c == '\r' || c == '\n') continue;
        if (browsing && c != 'J' && c != 'L' && c != 'a' && c != 'd') {
            browsing = false;
            emit(EV_BROWSE_END);
        }
        event_t ev;
        switch (c) {
        case 'w': ev = EV_UP;    break;
        case 's': ev = EV_DOWN;  break;
        case 'a': ev = EV_LEFT;  break;
        case 'd': ev = EV_RIGHT; break;
        case 'x': ev = EV_CENTER; break;
        case 'X': ev = EV_CENTER_LONG; break;
        case 'j': ev = EV_ENC_CCW; break;
        case 'l': ev = EV_ENC_CW;  break;
        case 'k': case 'p': case ' ': ev = EV_ENC_PRESS; break;
        case 'J': ev = EV_BROWSE_CCW; browsing = true; break;
        case 'L': ev = EV_BROWSE_CW;  browsing = true; break;
        case 'm': ev = EV_MENU; break;
        case 'o': ev = EV_SIDE; break;
        case 't': ev = EV_TEST_BARS; break;
        case 'v':                                 // toggle D-pad voltage logging
            s_ladder_log = !s_ladder_log; s_ladder_last_mv = -1000;
            ESP_LOGI("dpad", "voltage log %s%s", s_ladder_log ? "on" : "off",
                     s_ladder_ok ? "" : " (no ladder on this board)");
            continue;
        case '?': ESP_LOGI("keys", "%s", KEY_HELP); continue;
        default: continue;
        }
        emit(ev);
    }
}
#endif
