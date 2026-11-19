// Battery level from the board's ADC (board_battery_mv): smoothed, mapped to a
// percentage with a resting-LiPo curve, plus the low (power saving) and critical
// (save and switch off) thresholds the UI acts on.
#include "battery.h"
#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "battery";

#define LOW_PCT      20
#define CRIT_MV      3450
#define CRIT_READS   3

static int s_mv = -1, s_crit, s_sim = -1;

// Bench history (serial 'h'): the last readings, kept in RAM so a run on battery
// (no USB, no console) can be read back after USB is plugged in again.
#define HIST 64
static struct { uint32_t s; int16_t raw, mv; } s_hist[HIST];
static int s_hist_n;

// Resting single-cell LiPo, mV -> %, linear in between.
static const struct { int mv, pct; } CURVE[] = {
    { 3400, 0 }, { 3450, 2 }, { 3500, 5 }, { 3550, 8 }, { 3600, 12 }, { 3650, 20 },
    { 3700, 30 }, { 3750, 40 }, { 3800, 50 }, { 3900, 65 }, { 4000, 80 },
    { 4100, 90 }, { 4200, 100 },
};
#define NCURVE (int)(sizeof CURVE / sizeof *CURVE)

void battery_init(void)
{
    battery_poll();
    ESP_LOGI(TAG, "battery: raw %d mV, smoothed %d mV (%d%%)", board_battery_mv(), s_mv, battery_pct());
}

void battery_poll(void)
{
    int mv = s_sim >= 0 ? s_sim : board_battery_mv();
    int k = s_hist_n++ % HIST;
    s_hist[k].s = (uint32_t)(esp_timer_get_time() / 1000000);
    s_hist[k].raw = (int16_t)mv;
    if (mv < 2500 || mv > 4500) {            // no reading (Wi-Fi holds ADC2) or no cell
        if (s_sim < 0 && mv >= 0) s_mv = -1;
        return;
    }
    s_mv = (s_mv < 0 || s_sim >= 0) ? mv : (s_mv * 3 + mv) / 4;
    s_hist[k].mv = (int16_t)s_mv;
    if (s_mv < CRIT_MV) { if (s_crit < CRIT_READS) s_crit++; }
    else s_crit = 0;
    ESP_LOGD(TAG, "%d mV -> %d mV, %d%%", mv, s_mv, battery_pct());
}

int battery_mv(void) { return s_mv; }

int battery_pct(void)
{
    if (s_mv < 0) return -1;
    if (s_mv <= CURVE[0].mv) return 0;
    for (int i = 1; i < NCURVE; i++)
        if (s_mv <= CURVE[i].mv)
            return CURVE[i - 1].pct + (s_mv - CURVE[i - 1].mv) * (CURVE[i].pct - CURVE[i - 1].pct) /
                                      (CURVE[i].mv - CURVE[i - 1].mv);
    return 100;
}

bool battery_low(void) { int p = battery_pct(); return p >= 0 && p <= LOW_PCT; }
bool battery_critical(void) { return s_crit >= CRIT_READS; }

int battery_sim_next(void)
{
    static const int LEVELS[] = { -1, 3950, 3640, 3560, 3420 };
    static int i;
    i = (i + 1) % (int)(sizeof LEVELS / sizeof *LEVELS);
    s_sim = LEVELS[i];
    s_mv = -1; s_crit = 0;
    if (s_sim >= 0) for (int k = 0; k < CRIT_READS; k++) battery_poll();   // settle at once
    else battery_poll();
    ESP_LOGI(TAG, "simulated battery: %s%d mV (%d%%)", s_sim < 0 ? "off, real " : "", s_mv, battery_pct());
    return s_sim;
}

void battery_history_print(void)
{
    int n = s_hist_n < HIST ? s_hist_n : HIST;
    ESP_LOGI(TAG, "history: %d readings (every %d s), oldest first: t[s] raw smoothed [mV]", n, BATTERY_POLL_MS / 1000);
    for (int i = s_hist_n - n; i < s_hist_n; i++) {
        int k = i % HIST;
        ESP_LOGI(TAG, "  %5lu %5d %5d", (unsigned long)s_hist[k].s, s_hist[k].raw, s_hist[k].mv);
    }
}
