#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "MAX30102.h"
#include <stdio.h>
#include "global_defines.h"
#include "stdlib.h"
#include "math.h"
#include "pico/time.h"
#include <string.h>

/* ═══════════════════════════════════════════════════════════════
 * Tunables
 * ═══════════════════════════════════════════════════════════════ */
#define SAMPLE_RATE     100         /* Hz                           */
#define WINDOW_SIZE     200         /* samples  (2 s @ 100 Hz)      */
#define STEP_SIZE       (WINDOW_SIZE / 2)   /* 50 % overlap         */

#define AC_THRESHOLD    0.05f       /* autocorr / lag0 gate         */
#define RATIO_MIN       0.08f       /* periodicity ratio floor      */
#define CORR_MIN        0.70f       /* IR-RED cross-corr floor      */
#define PI_MIN          0.0001f     /* perfusion index floor        */
#define PI_MAX          0.05f       /* motion-artifact ceiling      */
#define QUALITY_GOOD    50.0f       /* quality score for conf++     */
#define RMS_FROZEN      5.0f        /* below this = dead signal     */
#define CONF_MAX        5
#define LAG_JUMP_MAX    25          /* samples — hard lag reset     */
#define NO_LAG_RESET    3           /* streak count — reopen search */
#define LOG_FAIL_EVERY  10          /* print fail msg every N times */
#define HR_WARMUP_MS    1500
#define MISS_UNLOCK_COUNT 3
#define RHYTHM_RATIO_MIN 0.88f
#define RHYTHM_RATIO_MAX 1.30f
#define BPM_JUMP_UP_MAX 12.0f
#define BPM_JUMP_DOWN_MAX 22.0f
#define PRELOCK_INTERVAL_MIN 650
#define PRELOCK_INTERVAL_MAX 950
#define PRELOCK_RATIO_MIN 0.80f
#define PRELOCK_RATIO_MAX 1.25f
#define PRELOCK_READY_COUNT 3

#define INT_BUF_SIZE 5

#define HR_DEBUG 0

#if HR_DEBUG
#define DBG(...) printf(__VA_ARGS__)
#else
#define DBG(...)
#endif

/* ═══════════════════════════════════════════════════════════════
 * HR state  (static — no VLA on Pico stack)
 * ═══════════════════════════════════════════════════════════════ */

/* ===== HR (band-pass + peak) ===== */

static float lp = 0, hp = 0, prev_lp = 0;
static float prev1 = 0, prev2 = 0;
static float threshold = 1000;
static float avg_amp = 0.0f;

static uint32_t last_peak_time = 0;
static uint32_t signal_start_time = 0;

static float averaged_bpm = 0;
static int has_signal = 0;
static int prev_has_signal = 0;
static float expected_interval = 0.0f;
static float last_peak_value = 0.0f;
static int peak_count = 0;
static int miss_count = 0;
static uint32_t last_miss_slot = 0;
static float prelock_interval_avg = 0.0f;
static int prelock_count = 0;

static float cand_peak = 0;
static uint32_t cand_time = 0;
static int cand_valid = 0;

/* ═══════════════════════════════════════════════════════════════
 * SpO2 state
 * ═══════════════════════════════════════════════════════════════ */
static float spo2_red_buf[WINDOW_SIZE];
static float spo2_ir_buf [WINDOW_SIZE];
static int   spo2_idx          = 0;
static int   spo2_value        = 0;
static float g_R               = 0.0f;
static float last_valid_R      = 0.0f;

/* ═══════════════════════════════════════════════════════════════
 * Low-level I²C helpers
 * ═══════════════════════════════════════════════════════════════ */
static int write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_write_timeout_us(I2C0_PORT, MAX30102_ADDR, buf, 2, false, 1000);
}

static int read_reg(uint8_t reg, uint8_t *value)
{
    int ret = i2c_write_timeout_us(I2C0_PORT, MAX30102_ADDR, &reg, 1, true, 1000);
    if (ret < 0) return ret;
    return i2c_read_timeout_us(I2C0_PORT, MAX30102_ADDR, value, 1, false, 1000);
}

/* ═══════════════════════════════════════════════════════════════
 * Hardware init / setup
 * ═══════════════════════════════════════════════════════════════ */
void max30102_init(void)
{
    uint8_t data;
    printf("Init MAX30102...\n");

    int ret = read_reg(MAX30102_PART_ID, &data);
    if (ret < 0) { printf("Read PART_ID failed\n"); return; }
    printf("PART_ID = 0x%02X\n", data);

    ret = write_reg(MAX30102_MODE_CONFIG, 0x40);   /* soft-reset */
    if (ret < 0) { printf("Reset failed\n"); return; }
    sleep_ms(100);
    printf("Init done\n");
}

void max30102_setup(void)
{
    /* FIFO: no averaging, roll-over enabled */
    write_reg(MAX30102_FIFO_CONFIG, 0x10);

    /* SpO2 mode (Red + IR), 4096 nA range, 100 Hz, 411 µs pulse */
    write_reg(MAX30102_MODE_CONFIG, 0x03);
    write_reg(MAX30102_SPO2_CONFIG, 0x47);

    /* LED current ~18.8 mA  (0x5F).  Raise to 0x7F (~25 mA) for wrist. */
    write_reg(MAX30102_LED1_PA, 0x4F);
    write_reg(MAX30102_LED2_PA, 0x4F);

    /* Clear FIFO */
    write_reg(MAX30102_FIFO_WR_PTR, 0x00);
    write_reg(MAX30102_OVF_COUNTER, 0x00);
    write_reg(MAX30102_FIFO_RD_PTR, 0x00);

    printf("MAX30102 setup done.\n");
}

int max30102_read_fifo(uint32_t *red, uint32_t *ir)
{
    uint8_t wr = 0, rd = 0;
    if (read_reg(MAX30102_FIFO_WR_PTR, &wr) < 0) return -1;
    if (read_reg(MAX30102_FIFO_RD_PTR, &rd) < 0) return -1;

    if (((wr - rd) & 0x1F) <= 0) return 0;

    uint8_t reg  = MAX30102_FIFO_DATA;
    uint8_t data[6];

    if (i2c_write_timeout_us(I2C0_PORT, MAX30102_ADDR, &reg, 1, true,  1000) < 0) return -1;
    if (i2c_read_timeout_us (I2C0_PORT, MAX30102_ADDR, data, 6, false, 1000) < 0) return -1;

    *red = (((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2]) & 0x03FFFF;
    *ir  = (((uint32_t)data[3] << 16) | ((uint32_t)data[4] << 8) | data[5]) & 0x03FFFF;
    return 1;
}

/* ═══════════════════════════════════════════════════════════════
 * HR — init / getters
 * ═══════════════════════════════════════════════════════════════ */
void max30102_hr_init(void)
{
    averaged_bpm = 0.0f;

    has_signal = 0;
    prev_has_signal = 0;

    lp = hp = prev_lp = 0.0f;
    prev1 = prev2 = 0.0f;

    threshold = 0.0f;
    avg_amp = 0.0f;

    last_peak_time = 0;
    signal_start_time = 0;
    expected_interval = 0.0f;
    last_peak_value = 0.0f;
    peak_count = 0;
    miss_count = 0;
    last_miss_slot = 0;
    prelock_interval_avg = 0.0f;
    prelock_count = 0;

    cand_peak = 0.0f;
    cand_time = 0;
    cand_valid = 0;
}

int   max30102_get_bpm      (void) { return (int)(averaged_bpm + 0.5f); }
int   max30102_has_signal   (void) { return has_signal; }

/* ───────────────────────────────────────────────────────────────
 * Inline helper: suppress repeated failure spam.
 * Prints the message only on the 1st failure and every
 * LOG_FAIL_EVERY-th one after that.
 * ─────────────────────────────────────────────────────────────── */

/* ═══════════════════════════════════════════════════════════════
 * HR — main update  (call once per sample, i.e. at 100 Hz)
 * ═══════════════════════════════════════════════════════════════ */

void max30102_hr_update(uint32_t red, uint32_t ir, uint32_t now)
{
    (void)red;

    /* ── 0. finger detect ── */
    if (!has_signal) {
        if (ir > 120000) {
            has_signal = 1;
            signal_start_time = now;
            last_peak_time = 0;
            expected_interval = 0.0f;
            last_peak_value = 0.0f;
            peak_count = 0;
            miss_count = 0;
            last_miss_slot = 0;
            prelock_interval_avg = 0.0f;
            prelock_count = 0;
            cand_valid = 0;
            DBG("[SIGNAL] detected ir=%d\n", ir);
        }
    } else {
        if (ir < 15000) {
            has_signal = 0;
            DBG("[SIGNAL] lost ir=%d\n", ir);
        }
    }

    if (!has_signal && prev_has_signal) {
        max30102_hr_init();
        max30102_spo2_init();
        last_peak_time = 0;
        cand_valid = 0;
        printf("[RESET] finger removed\n");
    }

    prev_has_signal = has_signal;
    if (!has_signal) return;

    /* ── 1. Band-pass ── */
    lp = 0.9f * lp + 0.1f * ir;
    hp = lp - prev_lp + 0.95f * hp;
    prev_lp = lp;

    float signal = hp;

    // ===== normalization =====
    float abs_sig = fabsf(signal);

    // 慢速追蹤振幅（不要太快）
    avg_amp = 0.95f * avg_amp + 0.05f * abs_sig;

    // 避免除以太小
    if (avg_amp > 0.001f) {
        signal = signal / avg_amp;
    }

    /* ── 2. threshold ── */

    // 初始化
    abs_sig = fabsf(signal);

    if (threshold < 0.1f) {
        threshold = abs_sig;
    }

    // 上升限制（放寬）
    float max_rise = threshold * 1.5f;

    // target（允許跟上訊號）
    float target = fminf(abs_sig, max_rise);

    // 更新（加快一點）
    threshold = 0.9f * threshold + 0.1f * target;

    if (signal_start_time != 0 && (now - signal_start_time < HR_WARMUP_MS)) {
        goto shift;
    }

    if (expected_interval > 0 && last_peak_time != 0) {
        float dt = now - last_peak_time;

        if (dt > expected_interval * 1.8f) {
            uint32_t miss_slot = (uint32_t)(dt / expected_interval);

            if (miss_slot > last_miss_slot) {
                last_miss_slot = miss_slot;
                miss_count++;
                cand_valid = 0;

                DBG("[MISS] missed beat dt=%.0f expected=%.0f count=%d\n",
                    dt, expected_interval, miss_count);

                if (miss_count >= MISS_UNLOCK_COUNT) {
                    DBG("[RESET] lost rhythm\n");
                    last_peak_time = 0;
                    last_peak_value = 0.0f;
                    expected_interval = 0.0f;
                    peak_count = 0;
                    miss_count = 0;
                    last_miss_slot = 0;
                    prelock_interval_avg = 0.0f;
                    prelock_count = 0;
                }
            }
        }
    }
    /* ── 3. slope peak detect ── */
    float slope1 = prev1 - prev2;
    float slope2 = signal - prev1;

    // ===== peak candidate =====
    if (slope1 > 0 && slope2 < 0) {

        // ===== 1. 最基本振幅門檻（先砍極小雜訊）=====
        if (prev1 < 0.1f) {
            DBG("[REJECT] too small\n");
            goto shift;
        }

        if (prev1 <= threshold * 1.0f) {
            DBG("[REJECT] below threshold\n");
            goto shift;
        }


        if (threshold > 0 && prev1 > threshold * 3.0f) {
            DBG("[REJECT] too large peak %.2f\n", prev1);
            goto shift;
        }

        if (last_peak_time != 0 &&
            expected_interval > 0 &&
            (now - last_peak_time < expected_interval * 0.8f)) {
            DBG("[REJECT] early peak\n");
            goto shift;
        }

        if (last_peak_time != 0 && (now - last_peak_time < 450)) {
            DBG("[REJECT] too close (double peak)\n");
            goto shift;
        }

        DBG("[PEAK?] slope ok sig=%.2f thr=%.2f\n",
            prev1, threshold);

        if (!cand_valid || prev1 > cand_peak) {
            cand_peak = prev1;
            cand_time = now;
            cand_valid = 1;
            DBG("[CAND] updated peak=%.2f\n", cand_peak);
        }
    }
    // ===== candidate timeout =====
    if (cand_valid && (now - cand_time > 1200)) {
        DBG("[CAND] timeout\n");
        cand_valid = 0;
    }

    // ===== validation =====
    if (cand_valid && (now - cand_time > 250))
    {
        if (last_peak_time != 0 &&
            miss_count >= MISS_UNLOCK_COUNT &&
            (now - last_peak_time > 3000)) {
        DBG("[FORCE RESET] re-lock rhythm\n");

        last_peak_time = cand_time;
        last_peak_value = cand_peak;

        expected_interval = 0;
        peak_count = 0;
        miss_count = 0;
        last_miss_slot = 0;
        prelock_interval_avg = 0.0f;
        prelock_count = 0;

        cand_valid = 0;
        goto shift;
    }

        DBG("[VALIDATE] trying peak=%.2f\n", cand_peak);

        if (last_peak_time != 0 && (now - last_peak_time > 3000)) {
            DBG("[RESET] lost rhythm\n");
            last_peak_time = 0;
            last_peak_value = 0.0f;
            expected_interval = 0;
            peak_count = 0;
            miss_count = 0;
            last_miss_slot = 0;
            prelock_interval_avg = 0.0f;
            prelock_count = 0;
        }

        if (last_peak_time == 0) {
            DBG("[INIT] first peak\n");
            last_peak_time = cand_time;
            last_peak_value = cand_peak;
            cand_valid = 0;
            goto shift;
        }

        uint32_t interval = cand_time - last_peak_time;

        if (expected_interval == 0) {
            if (interval < PRELOCK_INTERVAL_MIN || interval > PRELOCK_INTERVAL_MAX) {
                DBG("[REJECT] prelock interval=%d\n", interval);
                miss_count++;
                cand_valid = 0;
                goto shift;
            }

            if (prelock_interval_avg > 0.0f) {
                float prelock_ratio = interval / prelock_interval_avg;

                if (prelock_ratio < PRELOCK_RATIO_MIN ||
                    prelock_ratio > PRELOCK_RATIO_MAX) {
                    DBG("[REJECT] prelock ratio=%.2f\n", prelock_ratio);
                    miss_count++;
                    cand_valid = 0;
                    goto shift;
                }
            }
        }

        if (expected_interval > 0)
        {
            float ratio = interval / expected_interval;

            // ===== 2 beat =====
            if (ratio > 1.6f && ratio < 2.6f) {
                DBG("[FIX] x2\n");
                interval /= 2;
            }
/*             // ===== 3 beat =====
            else if (ratio > 2.6f && ratio < 3.6f &&
                    peak_count > 3) {
                DBG("[FIX] x3\n");
                interval /= 3;
            }
            // ===== 4 beat（保守）=====
            else if (ratio > 3.6f && ratio < 5.0f &&
                    peak_count > 6 &&
                    miss_count < 2) {
                DBG("[FIX] x4\n");
                interval /= 4;
            } */
        }

        DBG("[CHECK] interval=%d\n", interval);

        if (interval < 500 || interval > 1500) {
            DBG("[REJECT] interval out of range\n");
            miss_count++;
            cand_valid = 0;
            goto shift;
        }

        // ===== 節奏 =====
        if (expected_interval > 0)
        {
            float ratio = interval / expected_interval;

            if (ratio < RHYTHM_RATIO_MIN || ratio > RHYTHM_RATIO_MAX) {
                DBG("[REJECT] rhythm ratio=%.2f\n", ratio);
                miss_count++;
                cand_valid = 0;
                goto shift;
            }
        }

        float bpm = 60000.0f / interval;

        if (bpm < 45 || bpm > 130) {
            DBG("[REJECT] bpm out of range=%.1f\n", bpm);
            miss_count++;
            cand_valid = 0;
            goto shift;
        }

        if (expected_interval > 0 && averaged_bpm > 0 &&
            (bpm > averaged_bpm + BPM_JUMP_UP_MAX ||
             bpm < averaged_bpm - BPM_JUMP_DOWN_MAX)) {
            DBG("[REJECT] bpm jump (%.1f vs %.1f)\n",
                bpm, averaged_bpm);
            miss_count++;
            cand_valid = 0;
            goto shift;
        }

        // ===== VALID =====
        miss_count = 0;
        last_miss_slot = 0;

        if (expected_interval > 0) {
            float ratio = interval / expected_interval;

            if (ratio > 1.6f) {
                DBG("[BREAK] wrong rhythm unlock\n");
                expected_interval = 0;
                peak_count = 0;
                prelock_interval_avg = 0.0f;
                prelock_count = 0;
            }
        }

        if (expected_interval == 0) {
            prelock_interval_avg = (prelock_interval_avg == 0.0f)
                ? (float)interval
                : (0.65f * prelock_interval_avg + 0.35f * (float)interval);
            prelock_count++;

            if (prelock_count >= PRELOCK_READY_COUNT) {
                expected_interval = prelock_interval_avg;
                averaged_bpm = 60000.0f / expected_interval;
                peak_count = PRELOCK_READY_COUNT;
            } else if (averaged_bpm <= 2) {
                averaged_bpm = bpm;
            } else {
                averaged_bpm = 0.85f * averaged_bpm + 0.15f * bpm;
            }
        } else if (averaged_bpm <= 2) {
            averaged_bpm = bpm;
        } else {
            averaged_bpm = 0.7f * averaged_bpm + 0.3f * bpm;
        }

        float cur_interval = 60000.0f / averaged_bpm;

        if (expected_interval > 0)
            peak_count++;

        if (expected_interval > 0 && peak_count > 3)
        {
            expected_interval = 0.85f * expected_interval + 0.15f * cur_interval;
        }

        last_peak_time = cand_time;
        last_peak_value = cand_peak;

        DBG("[ACCEPT] interval=%d bpm=%.1f avg=%.1f exp=%.0f\n",
            interval, bpm, averaged_bpm, expected_interval);

        cand_valid = 0;
    }

    // ===== fallback =====
    if (miss_count >= 4) {
        DBG("[RESET] too many miss\n");
        last_peak_time = 0;
        last_peak_value = 0.0f;
        expected_interval = 0;
        peak_count = 0;
        miss_count = 0;
        last_miss_slot = 0;
        prelock_interval_avg = 0.0f;
        prelock_count = 0;
        cand_valid = 0;
    }

shift:
    prev2 = prev1;
    prev1 = signal;
}

/* ═══════════════════════════════════════════════════════════════
 * SpO2
 * ═══════════════════════════════════════════════════════════════ */
void max30102_spo2_init(void)
{
    spo2_value   = 0;
    spo2_idx     = 0;
    g_R          = 0.0f;
    last_valid_R = 0.0f;
    memset(spo2_red_buf, 0, sizeof(spo2_red_buf));
    memset(spo2_ir_buf,  0, sizeof(spo2_ir_buf));
}

void max30102_spo2_update(uint32_t red, uint32_t ir)
{
    if (!has_signal) {
        return;
    }

    spo2_red_buf[spo2_idx] = (float)red;
    spo2_ir_buf [spo2_idx] = (float)ir;
    spo2_idx++;
    if (spo2_idx < WINDOW_SIZE) return;
    spo2_idx = 0;

    /* DC */
    float red_mean = 0.0f, ir_mean = 0.0f;
    for (int i = 0; i < WINDOW_SIZE; i++) {
        red_mean += spo2_red_buf[i];
        ir_mean  += spo2_ir_buf[i];
    }
    red_mean /= WINDOW_SIZE;
    ir_mean  /= WINDOW_SIZE;

    if (red_mean < 1000.0f || ir_mean < 1000.0f) return;

    /* AC RMS */
    float red_ac = 0.0f, ir_ac = 0.0f;
    for (int i = 0; i < WINDOW_SIZE; i++) {
        float r = spo2_red_buf[i] - red_mean;
        float v = spo2_ir_buf[i]  - ir_mean;
        red_ac += r * r;
        ir_ac  += v * v;
    }
    red_ac = sqrtf(red_ac / WINDOW_SIZE);
    ir_ac  = sqrtf(ir_ac  / WINDOW_SIZE);

    if (red_ac < 30.0f || ir_ac < 30.0f) return;

    float ratio_r = red_ac / red_mean;
    float ratio_i = ir_ac  / ir_mean;

    /* Perfusion gate — same logic as HR */
    if (ratio_r < 0.0008f || ratio_i < 0.0008f) return;
    if (ratio_i > PI_MAX)  return;   /* motion artifact */

    /* R = (AC_red/DC_red) / (AC_ir/DC_ir) */
    float R = ratio_r / ratio_i;
    if (R < 0.4f || R > 3.4f) return;   /* physically impossible range */

    /* Reject large R jumps */
    if (g_R != 0.0f && fabsf(R - g_R) > 0.3f) return;

    /* Smooth R with slow EMA (changes slowly with SpO2) */
    g_R = (g_R == 0.0f) ? R : (0.95f * g_R + 0.05f * R);

    /*
     * Linear empirical approximation:
     *   SpO2 ≈ 110 − 25×R   (valid for R ≈ 0.4 – 1.0, SpO2 95–100 %)
     * For lower saturation extend with:
     *   SpO2 ≈ 104 − 17×R   (R 1.0 – 2.0, SpO2 80–95 %)
     */
    int spo2;
    if (g_R <= 1.0f)
        spo2 = (int)(110.0f - 25.0f * g_R + 0.5f);
    else
        spo2 = (int)(104.0f - 17.0f * g_R + 0.5f);

    if (spo2 > 100) spo2 = 100;
    if (spo2 < 70)  spo2 = 70;

    spo2_value   = spo2;
    last_valid_R = g_R;
}

int   max30102_get_spo2(void) { return spo2_value; }
float max30102_get_R   (void) { return g_R; }
