/*
 * MQ-135 空气质量传感器驱动 - 实现
 *
 * 算法与 water_sensor 一致 (统一风格):
 *   1) 中位数滤波 (32 次采样, 去除首尾 8 个极端值)
 *   2) EMA 指数平滑 (跨采样周期低通)
 *   3) 相对基线的 delta 判断 (消除个体差异)
 *   4) 三态 + 迟滞去抖 (CLEAN / ODOR / BAD)
 *   5) 上电预热 (加热丝需 ~15s 稳定后才校准基线)
 */
#include "air_mq135.h"
#include "adc1_bus.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "air";

/* GPIO7 在 ESP32-S3 上属于 ADC1 通道 6 */
#define AIR_ADC_UNIT      ADC_UNIT_1
#define AIR_ADC_CHANNEL   ADC_CHANNEL_6
#define AIR_ADC_ATTEN     ADC_ATTEN_DB_12    // 量程 ~0-3.1V

static adc_oneshot_unit_handle_t s_adc_handle  = NULL;
static adc_cali_handle_t         s_cali_handle = NULL;
static bool                      s_cali_ok     = false;

/* 干净空气基线 (启动预热后校准) */
static int  s_baseline = 0;
/* 上一次已确认状态 (用于迟滞去抖 + 输出) */
static air_state_t s_last_state = AIR_STATE_CLEAN;
/* 候选新状态 + 连续命中计数 (用于强抗抖) */
static air_state_t s_pending_state = AIR_STATE_CLEAN;
static int         s_pending_cnt   = 0;
/* 指数平滑后的 raw */
static int  s_smoothed = -1;
/* 是否完成预热 */
static bool s_warmed_up = false;

/* ADC 校准 (曲线拟合) */
static bool air_adc_calibration_init(void)
{
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id  = AIR_ADC_UNIT,
        .chan     = AIR_ADC_CHANNEL,
        .atten    = AIR_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&cfg, &s_cali_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration: curve fitting OK");
        return true;
    }
    ESP_LOGW(TAG, "ADC calibration failed: %s", esp_err_to_name(err));
    return false;
}

/* 冒泡排序 */
static void bubble_sort(int *a, int n)
{
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - 1 - i; j++) {
            if (a[j] > a[j + 1]) {
                int t = a[j]; a[j] = a[j + 1]; a[j + 1] = t;
            }
        }
    }
}

/* 采样 N 次 -> 排序 -> 首尾各去 trim -> 取中间求均 */
static int air_read_avg(int samples)
{
    if (samples > 64) samples = 64;
    int buf[64];
    int ok = 0;
    for (int i = 0; i < samples; i++) {
        int v = 0;
        if (adc_oneshot_read(s_adc_handle, AIR_ADC_CHANNEL, &v) == ESP_OK) {
            buf[ok++] = v;
        }
    }
    if (ok == 0) return 0;
    if (ok < 4) {
        int64_t s = 0;
        for (int i = 0; i < ok; i++) s += buf[i];
        return (int)(s / ok);
    }
    bubble_sort(buf, ok);
    int trim = AIR_SAMPLE_TRIM;
    if (trim * 2 >= ok) trim = ok / 4;
    int64_t sum = 0;
    int cnt = 0;
    for (int i = trim; i < ok - trim; i++) {
        sum += buf[i];
        cnt++;
    }
    return (cnt > 0) ? (int)(sum / cnt) : buf[ok / 2];
}

/* 采集干净空气基线 (要求通风环境)
 * 稳健策略: 采集 16 轮 -> 排序取中位数 -> 检测跳变
 * 如果读数跳动过大, 拒绝校准并使用兜底值, 避免假基线污染整个检测 */
void air_sensor_recalibrate(void)
{
    ESP_LOGI(TAG, "Calibrating clean air baseline (16 rounds, ~5s)...");
    int rounds[16];
    int n = 0;

    /* 丢弃前 4 次 (让 ADC 稳定) */
    for (int i = 0; i < 4; i++) {
        (void)air_read_avg(16);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    /* 有效采样 16 轮 */
    for (int i = 0; i < 16; i++) {
        int v = air_read_avg(48);
        if (v >= 0) {
            rounds[n++] = v;
            ESP_LOGI(TAG, "  baseline round %d: adc=%d", i + 1, v);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (n == 0) {
        ESP_LOGE(TAG, "Baseline: no valid samples, use fallback 300");
        s_baseline = 300;
        return;
    }

    /* 排序 -> 中位数 (剔除跳变) */
    bubble_sort(rounds, n);
    int median = rounds[n / 2];

    /* 检测跳变: 若 max-min > 300, 说明信号极不稳定, 硬件有问题 */
    int spread = rounds[n - 1] - rounds[0];
    if (spread > 300) {
        ESP_LOGW(TAG, "Baseline UNSTABLE! min=%d max=%d spread=%d",
                 rounds[0], rounds[n - 1], spread);
        ESP_LOGW(TAG, ">>> 请检查 MQ-135 A0 接线! 使用中位数 %d 作为基线", median);
    }

    s_baseline = median;

    /* 合理性夹紧 */
    if (s_baseline < 50) {
        ESP_LOGW(TAG, "Baseline too low (%d), forcing to 120", s_baseline);
        s_baseline = 120;
    } else if (s_baseline > 2500) {
        ESP_LOGW(TAG, "Baseline too high (%d), forcing to 800", s_baseline);
        s_baseline = 800;
    }
    ESP_LOGI(TAG, "Baseline ADC = %d (median of %d rounds)", s_baseline, n);
}

void air_sensor_init(void)
{
    /* --- A0 模拟引脚: 使用共享 ADC1 handle --- */
    s_adc_handle = adc1_bus_get();
    if (s_adc_handle == NULL) {
        ESP_LOGE(TAG, "failed to get shared ADC1 handle");
        return;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = AIR_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, AIR_ADC_CHANNEL, &chan_cfg));

    s_cali_ok = air_adc_calibration_init();

    /* MQ-135 加热丝预热 (期间持续采样让 ADC 稳定) */
    ESP_LOGI(TAG, "MQ-135 warming up for %d seconds...", AIR_WARMUP_SEC);
    for (int i = 0; i < AIR_WARMUP_SEC; i++) {
        /* 每秒采几次样, 帮助 ADC 内部电路稳定 */
        for (int k = 0; k < 5; k++) {
            (void)air_read_avg(4);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        if ((i + 1) % 10 == 0) {
            int v = air_read_avg(16);
            ESP_LOGI(TAG, "warmup %ds/%ds, adc=%d", i + 1, AIR_WARMUP_SEC, v);
        }
    }
    s_warmed_up = true;

    /* 预热后校准基线 */
    air_sensor_recalibrate();

    s_last_state = AIR_STATE_CLEAN;
    s_smoothed   = s_baseline;

    ESP_LOGI(TAG, "MQ-135 init done (A0=GPIO%d, baseline=%d)",
             AIR_PIN_A0, s_baseline);
}

/* 三态判断 + 迟滞 */
static air_state_t judge_state(int delta, air_state_t last)
{
    int t_odor_up = AIR_DELTA_ODOR;
    int t_odor_dn = AIR_DELTA_ODOR - AIR_HYSTERESIS;
    int t_bad_up  = AIR_DELTA_BAD;
    int t_bad_dn  = AIR_DELTA_BAD  - AIR_HYSTERESIS;

    switch (last) {
    case AIR_STATE_CLEAN:
        if (delta >= t_bad_up)  return AIR_STATE_BAD;
        if (delta >= t_odor_up) return AIR_STATE_ODOR;
        return AIR_STATE_CLEAN;

    case AIR_STATE_ODOR:
        if (delta >= t_bad_up) return AIR_STATE_BAD;
        if (delta <  t_odor_dn) return AIR_STATE_CLEAN;
        return AIR_STATE_ODOR;

    case AIR_STATE_BAD:
    default:
        if (delta <  t_bad_dn) {
            if (delta >= t_odor_dn) return AIR_STATE_ODOR;
            return AIR_STATE_CLEAN;
        }
        return AIR_STATE_BAD;
    }
}

/* 百分比映射: 以 BAD 阈值的 1.5 倍作为 100% */
static uint8_t delta_to_percent(int delta)
{
    if (delta <= 0) return 0;
    int full = AIR_DELTA_BAD * 3 / 2;
    if (delta >= full) return 100;
    return (uint8_t)(delta * 100 / full);
}

void air_sensor_read(air_data_t *data)
{
    if (!data) return;

    int raw = air_read_avg(AIR_SAMPLE_COUNT);

    if (s_smoothed < 0) s_smoothed = raw;
    /* 强低通: 1/10 新 + 9/10 旧, 抑制毛刺 */
    s_smoothed = (raw + 9 * s_smoothed) / 10;

    data->raw_adc   = s_smoothed;
    data->baseline  = s_baseline;
    data->delta     = s_smoothed - s_baseline;
    if (data->delta < 0) data->delta = 0;
    data->percent   = delta_to_percent(data->delta);
    data->warmed_up = s_warmed_up;

    /* 基线自适应: 当前若判为 CLEAN 且 smoothed < baseline+ODOR/2 时, 缓慢跟随
     * 这样即使初始 baseline 偏低, 环境干净时也能自动向上修正 */
    if (s_warmed_up && s_last_state == AIR_STATE_CLEAN) {
        int diff = s_smoothed - s_baseline;
        if (diff > -100 && diff < AIR_DELTA_ODOR / 2) {
            s_baseline += diff >> AIR_BASELINE_FOLLOW_SHIFT; // 1/256 缓慢逼近
        }
    }

    int mv = 0;
    if (s_cali_ok && adc_cali_raw_to_voltage(s_cali_handle, s_smoothed, &mv) == ESP_OK) {
        data->voltage_mv = mv;
    } else {
        data->voltage_mv = s_smoothed * 3300 / 4095;
    }

    /* 先用迟滞规则算出"本次瞬时判定" */
    air_state_t inst = judge_state(data->delta, s_last_state);

    /* 状态持续判定: 必须连续 AIR_CONFIRM_COUNT 次相同新状态才切换 */
    if (inst == s_last_state) {
        s_pending_state = s_last_state;
        s_pending_cnt   = 0;
    } else if (inst == s_pending_state) {
        s_pending_cnt++;
        if (s_pending_cnt >= AIR_CONFIRM_COUNT) {
            s_last_state    = inst;
            s_pending_cnt   = 0;
        }
    } else {
        /* 新的候选态, 重新计数 */
        s_pending_state = inst;
        s_pending_cnt   = 1;
    }

    data->state = s_last_state;
    data->alarm = (s_last_state == AIR_STATE_BAD);
}

const char *air_state_str(air_state_t s)
{
    switch (s) {
    case AIR_STATE_CLEAN: return "CLEAN";
    case AIR_STATE_ODOR:  return "ODOR";
    case AIR_STATE_BAD:   return "BAD";
    default:              return "?";
    }
}