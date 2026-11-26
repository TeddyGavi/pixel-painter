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
//   touch (Browse colours list only)  -> EV_SCROLL (drag, flick) / EV_TAP (see touch section)

#include "input.h"
#include "board.h"
#include "render.h"      // MENU_ROW_H, DISPLAY_*: touch scrolls in menu rows
#include <math.h>
#include <stdlib.h>
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
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_rom_crc.h"
#include "battery.h"
#include "esp_debug_helpers.h"
#include "miniz.h"                 // ROM deflate (tdefl) for the screen dump
#include "esp_heap_caps.h"
#include <stdio.h>
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#endif
static void serial_task(void *arg);
#endif

#define COUNTS_PER_DETENT 4
#define POLL_MS           5
#define DEBOUNCE_SAMPLES  3     // 15 ms stable
#define LONG_PRESS_MS     600
#define REPEAT_DELAY_MS   400
#define REPEAT_MS         120
#define PWR_HOLD_MS       1500  // PWR held this long: shut down

const char *const EVENT_NAMES[EV__COUNT] = {
    "UP", "DOWN", "LEFT", "RIGHT", "CENTER", "CENTER_LONG",
    "ENC_CW", "ENC_CCW", "ENC_PRESS", "BROWSE_CW", "BROWSE_CCW", "BROWSE_END",
    "MENU", "SIDE", "TICK", "TEST_BARS", "PAINT", "PWR", "PWR_HOLD", "SCROLL", "TAP",
};

static QueueHandle_t s_q;
static pcnt_unit_handle_t s_enc;
static int s_accum;

typedef enum { K_DIR, K_CENTER, K_ENC, K_BTN, K_PWR } kind_t;
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
    int exio;            // I/O-expander bit when pin < 0 (MENU/SIDE on the Waveshare), else -1
} sw_t;

static sw_t s_sw[] = {
    { .pin = PIN_DPAD_UP, .kind = K_DIR, .ev = EV_UP, .ladder = L_UP, .exio = -1 },
    { .pin = PIN_DPAD_DOWN, .kind = K_DIR, .ev = EV_DOWN, .ladder = L_DOWN, .exio = -1 },
    { .pin = PIN_DPAD_LEFT, .kind = K_DIR, .ev = EV_LEFT, .ladder = L_LEFT, .exio = -1 },
    { .pin = PIN_DPAD_RIGHT, .kind = K_DIR, .ev = EV_RIGHT, .ladder = L_RIGHT, .exio = -1 },
    { .pin = PIN_DPAD_CENTER, .kind = K_CENTER, .ev = EV_CENTER, .ladder = L_CENTER, .exio = -1 },
    { .pin = PIN_ENC_SW, .kind = K_ENC, .ev = EV_ENC_PRESS, .exio = -1 },
    { .pin = PIN_BTN_MENU, .kind = K_BTN, .ev = EV_MENU, .exio = EXIO_BTN_MENU },
    { .pin = PIN_BTN_SIDE, .kind = K_BTN, .ev = EV_SIDE, .exio = EXIO_BTN_SIDE },
    // PWR (Waveshare): also the power-on button, so the press that switched the
    // board on is ignored until it has been released once.
    { .pin = PIN_PWR_KEY, .kind = K_PWR, .ev = EV_PWR, .exio = -1 },
};
#define N_SW (sizeof s_sw / sizeof *s_sw)

static bool s_pwr_armed;    // PWR released at least once since boot
static bool s_enc_painted;  // encoder held and the D-pad moved: painting (brush mode)
static volatile bool s_ser_enc;  // bench: serial '[' holds the encoder push, ']' releases
static bool s_enc_down;     // encoder switch held
static bool s_enc_turned;   // ...and turned since it went down

static bool emit(event_t e) { return xQueueSend(s_q, &e, 0) == pdTRUE; }
static void touch_cancel(void);

// A D-pad move. With the encoder push held (and not turned: that is browsing) it
// paints: the start cell first, then each cell the cursor moves onto.
static void dpad(event_t e)
{
    if (s_enc_down && !s_enc_turned) {
        if (!s_enc_painted) { emit(EV_PAINT); s_enc_painted = true; }
        emit(e);
        emit(EV_PAINT);
    } else {
        emit(e);
    }
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

static void detent(int dir)
{
    touch_cancel();
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
    touch_cancel();                 // any physical input stops a coasting list
    switch (s->kind) {
    case K_DIR:    dpad(s->ev); s->next_rep_us = now + REPEAT_DELAY_MS * 1000; break;
    case K_CENTER: s->long_fired = false; break;           // decided on hold/release
    case K_ENC:    s_enc_down = true; s_enc_turned = false; s_enc_painted = false; break;
    case K_BTN:    emit(s->ev); break;
    case K_PWR:    s->long_fired = false; break;           // decided on hold/release
    }
}

static void on_release(sw_t *s)
{
    switch (s->kind) {
    case K_CENTER: if (!s->long_fired) emit(EV_CENTER); break;
    case K_PWR:
        if (s_pwr_armed && !s->long_fired) emit(EV_PWR);
        s_pwr_armed = true;                     // first release: the power-on press is over
        break;
    case K_ENC:
        s_enc_down = false;
        if (s_enc_turned) emit(EV_BROWSE_END);
        else if (!s_enc_painted) emit(EV_ENC_PRESS);    // painted: the release adds nothing
        s_enc_painted = false;
        break;
    default: break;
    }
}

static void on_held(sw_t *s, int64_t now)
{
    if (s->kind == K_DIR && now >= s->next_rep_us) {
        dpad(s->ev);                                   // held: auto-repeat (paints a line in brush mode)
        s->next_rep_us = now + REPEAT_MS * 1000;
    } else if (s->kind == K_CENTER && !s->long_fired &&
               now - s->since_us >= LONG_PRESS_MS * 1000) {
        s->long_fired = true;
        emit(EV_CENTER_LONG);
    } else if (s->kind == K_PWR && s_pwr_armed && !s->long_fired &&
               now - s->since_us >= PWR_HOLD_MS * 1000) {
        s->long_fired = true;
        emit(EV_PWR_HOLD);
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
    // Expander inputs (one I2C read per poll). A failed read = nothing pressed.
    int ex = (EXIO_BTN_MENU >= 0 || EXIO_BTN_SIDE >= 0) ? board_exio_read() : -1;
    for (unsigned i = 0; i < N_SW; i++) {
        sw_t *s = &s_sw[i];
        bool level_down;
        if (s->pin >= 0) level_down = gpio_get_level(s->pin) == 0 ||   // active low
                                      (s->kind == K_ENC && s_ser_enc);   // bench: serial '[' holds it
        else if (s->exio >= 0) level_down = ex >= 0 && !((ex >> s->exio) & 1); // active low
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

// ---- touch: drag and flick scrolling (Browse colours) ------------------------------
// Sampled every TOUCH_POLL_MS. Distances are in menu rows (R = MENU_ROW_H px: 56 on
// the Waveshare, 32 on the Freenove), so the feel is the same on both boards.
//   slop     R*2/7 (16 px): below it a touch is still a tap; past it the axis locks
//            (vertical = drag; mostly horizontal = ignored until lift)
//   drag     the list follows the finger one row per R of travel, re-anchored where
//            the slop was crossed, with R/7 (8 px) hysteresis at each half row
//   flick    lift moving >= 12.5 rows/s (700 px/s), measured over the last 80 ms,
//            after at least one row of drag: the list coasts at that rate (cap 30
//            rows/s, one row per redraw: a step waits until the last one is drawn),
//            decaying with tau 400 ms, and stops below 8 rows/s. A second flick the same way within 500 ms of catching adds on.
//   tap      < 300 ms, inside the slop, and not the touch that caught a coast
// Lift = two polls in a row without a finger (one dropped report is not a lift).
// Samples come from the board, or from a scripted gesture (serial "T", for tests).
#define TOUCH_POLL_MS    10
#define TOUCH_READY_MS   300            // FT6336U: reset to first report
#define T_R              MENU_ROW_H
#define T_SLOP           (T_R * 2 / 7)
#define T_HYST           (T_R / 7)
#define T_FLICK_RPS      12.5f
#define T_COAST_MAX_RPS  30.0f
#define T_COAST_MIN_RPS  8.0f
#define T_TAU_S          0.4f
#define T_VEL_WIN_US     80000
#define T_VEL_FRESH_US   40000
#define T_TAP_US         300000
#define T_ADD_US         500000
#define T_HIST           12

static portMUX_TYPE s_tmux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_touch_want;      // set by the UI
static bool s_touch_on;                 // what the input task last applied
static bool s_touch_asleep;             // controller confirmed in hibernate
static int64_t s_touch_ready_us, s_touch_retry_us;
static int s_touch_miss;
static volatile bool s_touch_stop_req, s_touch_cancel_req;
static int s_scroll_pending;            // under s_tmux
static bool s_scroll_token;             // an EV_SCROLL is queued and not yet taken
static volatile int s_tap_y;
static volatile bool s_busy;

static struct {
    bool down, dead, dragging, caught;
    int x0, y0, ya, k, steps;
    int64_t t_down;
    int n; int64_t ht[T_HIST]; int hy[T_HIST];   // recent samples (ring)
} G;
static struct { bool on; float rate; int64_t last_us, next_us; } C;   // coast (+ = later rows)
static float s_caught_rate;
static int64_t s_caught_us;

static void scroll_emit(int d)
{
    bool send = false;
    portENTER_CRITICAL(&s_tmux);
    s_scroll_pending += d;
    if (!s_scroll_token) { s_scroll_token = send = true; }
    portEXIT_CRITICAL(&s_tmux);
    if (send && !emit(EV_SCROLL)) {                  // queue full: try again next step
        portENTER_CRITICAL(&s_tmux);
        s_scroll_token = false;
        portEXIT_CRITICAL(&s_tmux);
    }
}

int input_take_scroll(void)
{
    portENTER_CRITICAL(&s_tmux);
    int d = s_scroll_pending;
    s_scroll_pending = 0; s_scroll_token = false;
    portEXIT_CRITICAL(&s_tmux);
    return d;
}

int  input_tap_y(void) { return s_tap_y; }
void input_touch_enable(bool on) { s_touch_want = on; }
void input_touch_stop(void) { s_touch_stop_req = true; }
bool input_touch_busy(void) { return s_busy; }

// Physical input: stop coasting. A finger still down keeps dragging from where it is.
static void touch_cancel(void)
{
    C.on = false;
    if (G.down && G.dragging) { G.ya = G.hy[(G.n - 1 + T_HIST) % T_HIST]; G.k = 0; }
}

static void touch_down(int64_t now, int x, int y)
{
    G.down = true; G.dead = false; G.dragging = false;
    G.x0 = x; G.y0 = y; G.ya = y; G.k = 0; G.steps = 0; G.t_down = now; G.n = 0;
    G.caught = C.on;
    if (C.on) { s_caught_rate = C.rate; s_caught_us = now; C.on = false; }
}

static void touch_move(int64_t now, int x, int y)
{
    G.ht[G.n % T_HIST] = now; G.hy[G.n % T_HIST] = y; G.n++;
    if (G.dead) return;
    if (!G.dragging) {
        int dx = abs(x - G.x0), dy = abs(y - G.y0);
        if (dx < T_SLOP && dy < T_SLOP) return;
        if (dy < dx) { G.dead = true; return; }        // sideways: not a scroll
        G.dragging = true; G.ya = y; G.k = 0;
    }
    int d = G.ya - y;                                  // finger up = later rows
    while (d - G.k * T_R > T_R / 2 + T_HYST)    { G.k++; G.steps++; scroll_emit(+1); }
    while (d - G.k * T_R < -(T_R / 2 + T_HYST)) { G.k--; G.steps++; scroll_emit(-1); }
}

static void touch_lift(int64_t now)
{
    G.down = false;
    if (G.dead) return;
    if (!G.dragging) {
        if (now - G.t_down < T_TAP_US && !G.caught) { s_tap_y = G.y0; emit(EV_TAP); }
        return;
    }
    if (!G.steps || G.n < 2) return;
    int last = (G.n - 1) % T_HIST, first = last, cnt = G.n < T_HIST ? G.n : T_HIST;
    for (int i = 1; i < cnt; i++) {                   // oldest sample inside the window
        int j = (G.n - 1 - i) % T_HIST;
        if (G.ht[last] - G.ht[j] > T_VEL_WIN_US) break;
        first = j;
    }
    int64_t dt = G.ht[last] - G.ht[first];
    if (dt <= 0 || now - G.ht[last] > T_VEL_FRESH_US) return;
    float rps = (float)(G.hy[first] - G.hy[last]) * 1e6f / (float)dt / T_R;
    if (fabsf(rps) < T_FLICK_RPS) return;
    if (G.caught && now - s_caught_us < T_ADD_US && (rps > 0) == (s_caught_rate > 0))
        rps += s_caught_rate;                          // flicking again: speeds up
    if (rps > T_COAST_MAX_RPS) rps = T_COAST_MAX_RPS;
    if (rps < -T_COAST_MAX_RPS) rps = -T_COAST_MAX_RPS;
    C.on = true; C.rate = rps; C.last_us = now;
    C.next_us = now + (int64_t)(1e6f / fabsf(rps));
}

static void coast_tick(int64_t now)
{
    if (!C.on || now < C.next_us) return;
    // One row per redraw, never skipped: wait while the last step is not drawn yet
    // (a full list redraw takes ~45 ms on the Waveshare, so the coast runs at most
    // at the redraw rate there).
    portENTER_CRITICAL(&s_tmux);
    bool queued = s_scroll_token;
    portEXIT_CRITICAL(&s_tmux);
    if (queued) return;
    scroll_emit(C.rate > 0 ? +1 : -1);
    C.rate *= expf(-(float)(now - C.last_us) / (T_TAU_S * 1e6f));
    C.last_us = now;
    if (fabsf(C.rate) < T_COAST_MIN_RPS) { C.on = false; return; }
    C.next_us = now + (int64_t)(1e6f / fabsf(C.rate));
}

// Scripted gestures (serial "T" + a letter; tests and the bench). Positions in rows
// of the list, from the middle of the screen:
//   u / d  flick up / down: 5 rows in 100 ms, then lift (coasts on)
//   U / D  slow drag up / down: 3 rows in 600 ms, rest 100 ms, lift (no coast)
//   1..9   tap list row n          h  touch and hold 150 ms (catches a coast)
//   s      sideways swipe (ignored)
static struct { bool on; int64_t t0; int dur_us, hold_us, x0, x1, y0, y1; } S;

__attribute__((unused)) static void synth_start(char c)
{
    const int mid = T_R + T_R * 9 / 2, cx = DISPLAY_W / 2;
    int x0 = cx, x1 = cx, y0 = mid, y1 = mid, dur = 0, hold = 0;
    switch (c) {
    case 'u': y0 = mid + T_R * 5 / 2; y1 = y0 - 5 * T_R; dur = 100000; break;
    case 'd': y0 = mid - T_R * 5 / 2; y1 = y0 + 5 * T_R; dur = 100000; break;
    case 'U': y0 = mid + T_R * 3 / 2; y1 = y0 - 3 * T_R; dur = 600000; hold = 100000; break;
    case 'D': y0 = mid - T_R * 3 / 2; y1 = y0 + 3 * T_R; dur = 600000; hold = 100000; break;
    case 'h': hold = 150000; break;
    case 's': x0 = cx - 2 * T_R; x1 = cx + 2 * T_R; dur = 200000; break;
    default:
        if (c < '1' || c > '9') return;
        y0 = y1 = T_R + (c - '1') * T_R + T_R / 2; hold = 60000; break;
    }
    portENTER_CRITICAL(&s_tmux);
    S.x0 = x0; S.x1 = x1; S.y0 = y0; S.y1 = y1; S.dur_us = dur; S.hold_us = hold;
    S.t0 = esp_timer_get_time(); S.on = true;
    portEXIT_CRITICAL(&s_tmux);
}

static int synth_sample(int64_t now, int *x, int *y)
{
    portENTER_CRITICAL(&s_tmux);
    int64_t t = now - S.t0;
    int r = 0;
    if (t < S.dur_us) {
        *x = S.x0 + (int)((S.x1 - S.x0) * t / S.dur_us);
        *y = S.y0 + (int)((S.y1 - S.y0) * t / S.dur_us); r = 1;
    } else if (t < S.dur_us + S.hold_us) {
        *x = S.x1; *y = S.y1; r = 1;
    } else {
        S.on = false;
    }
    portEXIT_CRITICAL(&s_tmux);
    return r;
}

static void touch_poll(int64_t now)
{
    bool want = s_touch_want;
    if (want != s_touch_on) {
        s_touch_on = want;
        C.on = false;
        input_take_scroll();                           // drop steps not yet taken
        if (want) {
            board_touch_power(true);
            ESP_LOGI("touch", "on (Browse colours)");
            s_touch_asleep = false;
            s_touch_ready_us = now + TOUCH_READY_MS * 1000LL;
            G.down = true; G.dead = true;              // a finger already down: ignored until lift
        } else {
            G.down = false;
            s_touch_retry_us = 0;
        }
    }
    if (!s_touch_on) {
        // Hibernate the controller; retried while it is still starting up (boot).
        static int tries;
        if (!s_touch_asleep && now >= s_touch_retry_us && tries < 25) {
            s_touch_asleep = board_touch_power(false);
            s_touch_retry_us = now + 200000;
            if (s_touch_asleep) { ESP_LOGI("touch", "controller hibernated"); tries = 0; }
            else if (++tries == 25) ESP_LOGW("touch", "controller not answering (hibernate)");
        }
        s_busy = false;
        return;
    }
    if (s_touch_cancel_req) { s_touch_cancel_req = false; touch_cancel(); }
    if (s_touch_stop_req)   { s_touch_stop_req = false; C.on = false; }
    coast_tick(now);
    int x = 0, y = 0, r;
    if (S.on) r = synth_sample(now, &x, &y);
    else if (now < s_touch_ready_us) r = 0;
    else {
        r = board_touch_read(&x, &y);
        static bool said;
        if (r < 0 && !said) { said = true; ESP_LOGW("touch", "read failed (no controller?)"); }
    }
    if (r > 0) {
        s_touch_miss = 0;
        if (!G.down) touch_down(now, x, y);
        touch_move(now, x, y);
    } else if (G.down && ++s_touch_miss >= 2) {
        s_touch_miss = 0;
        touch_lift(now);
    }
    s_busy = (G.down && !G.dead) || C.on;
}

static void poll_task(void *arg)
{
    (void)arg;
    // Poll switches before the encoder so a press that starts together with a
    // turn is already "down" when the detent is classified.
    int64_t next_touch = 0;
    for (;;) {
        poll_switches();
        poll_encoder();
        int64_t now = esp_timer_get_time();
        if (now >= next_touch) { next_touch = now + TOUCH_POLL_MS * 1000; touch_poll(now); }
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
    // Not held at boot (USB power, or a reset): PWR presses count from the start.
    s_pwr_armed = PIN_PWR_KEY < 0 || gpio_get_level(PIN_PWR_KEY) != 0;
    ladder_init();
    xTaskCreate(poll_task, "input", 3072, NULL, 10, NULL);
#if CONFIG_BEAD_SERIAL_KEYS
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    // ESP32-S3: the console is the chip's USB Serial/JTAG port. The driver is
    // installed for reading keys only; output stays on the default console path,
    // which waits briefly for the host and then drops data instead of blocking.
    // (Routing stdout through the driver hung the UI: with a screen dump and log
    // lines in flight together, the driver's TX path stalled and every task that
    // logged blocked forever.)
    usb_serial_jtag_driver_config_t ucfg = { .rx_buffer_size = 256, .tx_buffer_size = 256 };
    if (usb_serial_jtag_driver_install(&ucfg) == ESP_OK)
        xTaskCreate(serial_task, "keys", 8192, NULL, 5, NULL);   // 8 KB: the screen dump runs ROM deflate + newlib fputs on this stack
#else
    // UART0 is also the log console; the driver only takes over its RX side here.
    if (uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0) == ESP_OK)
        xTaskCreate(serial_task, "keys", 8192, NULL, 5, NULL);   // 8 KB: the screen dump runs ROM deflate + newlib fputs on this stack
#endif
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
    "k|p|space=encoder press(place)  [/]=hold/release encoder (with w/a/s/d: paint brush)  "
    "J/L=press+turn(browse; a/d=family)  "
    "m=menu  o=side  t=colour bars  v=D-pad volts  z=PWR(sleep)  q=PWR hold(shut down)  "
    "b=simulated battery level  h=battery history  F=screen dump  !Z=factory reset  "
    "T+u/d=flick up/down  T+U/D=slow drag  T+1..9=tap row  T+h=touch-hold  T+s=sideways (Browse colours)  ?=help";

static int key_read(uint8_t *c, TickType_t wait)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    return usb_serial_jtag_read_bytes(c, 1, wait);
#else
    return uart_read_bytes(UART_NUM_0, c, 1, wait);
#endif
}

// Test support (key 'F'): dump the shadow framebuffer, zlib-compressed (ROM
// deflate) as base64 text lines, so a host script can rebuild the exact screen.
//   FBDUMP <w> <h> ZLIB
//   FB:<base64 of the zlib stream of the big-endian RGB565 pixels> ...
//   FBEND <bytes> <crc32 of the zlib stream, hex>
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static uint8_t s_dump[57];          // 57 bytes -> one 76-char base64 line
static int s_dump_n;
static uint32_t s_dump_crc, s_dump_total;
static void dump_line(void)
{
    char out[4 + 76 + 2] = "FB:";
    int o = 3;
    for (int i = 0; i < s_dump_n; i += 3) {
        uint32_t v = s_dump[i] << 16 | (i + 1 < s_dump_n ? s_dump[i + 1] << 8 : 0) |
                     (i + 2 < s_dump_n ? s_dump[i + 2] : 0);
        out[o++] = B64[v >> 18 & 63]; out[o++] = B64[v >> 12 & 63];
        out[o++] = i + 1 < s_dump_n ? B64[v >> 6 & 63] : '=';
        out[o++] = i + 2 < s_dump_n ? B64[v & 63] : '=';
    }
    out[o++] = '\n'; out[o] = 0;
    fputs(out, stdout);
    s_dump_n = 0;
    static unsigned lines;
    if (++lines % 64 == 0) vTaskDelay(1);            // let the idle task feed the watchdog
}
static void dump_bytes(const uint8_t *b, int n)
{
    s_dump_crc = esp_rom_crc32_le(s_dump_crc, b, n);
    s_dump_total += n;
    for (int i = 0; i < n; i++) {
        s_dump[s_dump_n++] = b[i];
        if (s_dump_n == sizeof s_dump) dump_line();
    }
}
static mz_bool dump_put(const void *buf, int len, void *user)
{
    (void)user;
    dump_bytes(buf, len);
    return MZ_TRUE;
}
static void fb_dump(void)
{
    const uint16_t *fb = board_framebuffer();
    if (!fb) { ESP_LOGW("keys", "no framebuffer on this board"); return; }
    tdefl_compressor *z = heap_caps_malloc(sizeof *z, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!z) z = malloc(sizeof *z);
    if (!z) { ESP_LOGE("keys", "no memory for the screen dump"); return; }
    fflush(stdout);
    board_fb_lock(true);
    board_fb_sync();
    s_dump_n = 0; s_dump_crc = 0; s_dump_total = 0;
    printf("FBDUMP %d %d ZLIB\n", DISPLAY_W, DISPLAY_H);
    tdefl_init(z, dump_put, NULL, TDEFL_WRITE_ZLIB_HEADER | 128);
    const uint8_t *p = (const uint8_t *)fb;
    size_t left = (size_t)DISPLAY_W * DISPLAY_H * 2;
    while (left) {                                     // feed a row at a time, yielding
        size_t n = left < DISPLAY_W * 2 ? left : DISPLAY_W * 2;
        tdefl_compress_buffer(z, p, n, n == left ? TDEFL_FINISH : TDEFL_NO_FLUSH);
        p += n; left -= n;
        taskYIELD();
    }
    free(z);
    if (s_dump_n) dump_line();
    printf("FBEND %lu %08lx\n", (unsigned long)s_dump_total, (unsigned long)s_dump_crc);
    fflush(stdout);
    board_fb_lock(false);
}

static void serial_task(void *arg)
{
    (void)arg;
    bool browsing = false;
    int64_t bang_us = 0;                // '!' seen: 'Z' within 1 s wipes the patterns
    ESP_LOGI("keys", "%s", KEY_HELP);
    for (;;) {
        uint8_t c;
        TickType_t wait = browsing ? pdMS_TO_TICKS(BROWSE_IDLE_MS) : portMAX_DELAY;
        if (key_read(&c, wait) != 1) {
            if (browsing) { browsing = false; emit(EV_BROWSE_END); }
            continue;
        }
        if (c == '\r' || c == '\n') continue;
        if (c == 'T') {                                       // scripted touch gesture
            uint8_t g;
            if (key_read(&g, pdMS_TO_TICKS(1000)) == 1) synth_start((char)g);
            continue;
        }
        s_touch_cancel_req = true;                            // a key = physical input: stop a coast
        if (browsing && c != 'J' && c != 'L' && c != 'a' && c != 'd') {
            browsing = false;
            emit(EV_BROWSE_END);
        }
        event_t ev;
        switch (c) {
        case 'w': dpad(EV_UP);    continue;
        case 's': dpad(EV_DOWN);  continue;
        case 'a': if (browsing) { ev = EV_LEFT;  break; } dpad(EV_LEFT);  continue;
        case 'd': if (browsing) { ev = EV_RIGHT; break; } dpad(EV_RIGHT); continue;
        case '[': s_ser_enc = true;  continue;                 // bench: hold the encoder push
        case ']': s_ser_enc = false; continue;                 //        release it
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
        case 'z': ev = EV_PWR; break;                         // PWR short press: sleep / wake
        case 'q': ev = EV_PWR_HOLD; break;                    // PWR held: shut down
        case 'b': battery_sim_next(); ev = EV_TICK; break;    // bench: step simulated battery level
        case 'h': battery_history_print(); continue;          // bench: recent battery readings
        case 'v':                                 // toggle D-pad voltage logging
            s_ladder_log = !s_ladder_log; s_ladder_last_mv = -1000;
            ESP_LOGI("dpad", "voltage log %s%s", s_ladder_log ? "on" : "off",
                     s_ladder_ok ? "" : " (no ladder on this board)");
            continue;
        case '?': ESP_LOGI("keys", "%s", KEY_HELP); ESP_LOGI("keys", "board: %s", board_status());
                  ESP_LOGI("keys", "battery: %d mV now, %d mV smoothed (%d%%)", board_battery_mv(), battery_mv(), battery_pct()); continue;
        case 'F': fb_dump(); continue;                        // test: screen dump
        case 'B': esp_backtrace_print_all_tasks(24); continue;   // debug: every task's backtrace
        case 'G': {                                           // bench: edge ruler
            // 8-px stripes from each side edge inwards: red orange yellow green cyan
            // blue magenta white (left edge x 0.., right edge x W-1..), same from the
            // top and bottom edges in the middle third. Grey elsewhere. Any key redraws
            // nothing; open/close the menu (m m) to get the UI back.
            static const uint16_t C[8] = { 0xF800, 0xFC00, 0xFFE0, 0x07E0, 0x07FF, 0x001F, 0xF81F, 0xFFFF };
            const display_ops_t *D = board_display();
            D->fill_rect(0, 0, DISPLAY_W, DISPLAY_H, 0x4208);
            for (int k = 0; k < 8; k++) {
                D->fill_rect(k * 8, 0, 8, DISPLAY_H, C[k]);
                D->fill_rect(DISPLAY_W - (k + 1) * 8, 0, 8, DISPLAY_H, C[k]);
                D->fill_rect(DISPLAY_W / 3, k * 8, DISPLAY_W / 3, 8, C[k]);
                D->fill_rect(DISPLAY_W / 3, DISPLAY_H - (k + 1) * 8, DISPLAY_W / 3, 8, C[k]);
            }
            if (D->present) D->present();
            ESP_LOGI("keys", "edge ruler: 8 px stripes red,orange,yellow,green,cyan,blue,magenta,white from each edge");
            continue;
        }
        case '!': bang_us = esp_timer_get_time(); continue;
        case 'Z':                                             // test: "!Z" = factory reset
            if (bang_us && esp_timer_get_time() - bang_us < 1000000) {
                ESP_LOGW("keys", "factory reset: erasing NVS and restarting");
                fflush(stdout);
                nvs_flash_erase();
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            }
            continue;
        default: continue;
        }
        emit(ev);
    }
}
#endif
