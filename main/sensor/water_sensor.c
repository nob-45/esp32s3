/*
 * 浴室积水检测传感器驱动 - 实现
 *
 * 关键改进 (相较 v0.3):
 *   1) 多次采样求平均 (16 次), 抑制 ADC 抖动
 *   2) 启动时自动采集"干燥基线", 消除个体差异
 *   3) 三态判断: 干燥 / 潮湿 / 积水
 *   4) 迟滞去抖: 避免临界值反复跳变报警
 *   5) percent 基于相对基线的增量映射, 更贴合"多少水"直觉
 */
#include "water_sensor.h"
#include "adc1_bus.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "water";

/* GPIO6 在 ESP32-S3 上属于 ADC1 通道 5 */
#define WATER_ADC_UNIT      ADC_UNIT_1
#define WATER_ADC_CHANNEL   ADC_CHANNEL_5
#define WATER_ADC_ATTEN     ADC_ATTEN_DB_12    // 量程 ~0-3.1V

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t         s_cali_handle = NULL;
static bool                      s_cali_ok = false;

/* 干燥基线 (启动时校准) */
static int  s_baseline = 0;
/* 上一次状态 (用于迟滞去抖) */
static water_state_t s_last_state = WATER_STATE_DRY;
/* 指数平滑后的 raw (跨采样周期低通, 抑制水面波动) */
static int  s_smoothed = -1;

/* ADC 校准 (曲线拟合) */
static bool water_adc_calibration_init(void)
{
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id  = WATER_ADC_UNIT,
        .chan     = WATER_ADC_CHANNEL,
        .atten    = WATER_ADC_ATTEN,
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

/* 冒泡排序 (小数量, 简单可靠) */
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

/* 采样 N 次 -> 排序 -> 首尾各去 trim 个 -> 取中间求均
 * 强力抗尖峰: 水面波动导致的 3000+ 单次尖峰会被彻底剔除
 */
static int water_read_avg(int samples)
{
    /* 限制上限, 避免栈溢出 */
    if (samples > 64) samples = 64;
    int buf[64];
    int ok = 0;
    for (int i = 0; i < samples; i++) {
        int v = 0;
        if (adc_oneshot_read(s_adc_handle, WATER_ADC_CHANNEL, &v) == ESP_OK) {
            buf[ok++] = v;
        }
    }
    if (ok == 0) return 0;
    if (ok < 4) {
        /* 样本太少直接均值 */
        int64_t s = 0;
        for (int i = 0; i < ok; i++) s += buf[i];
        return (int)(s / ok);
    }
    bubble_sort(buf, ok);
    /* 首尾各去 trim 个 (但不能超过一半) */
    int trim = WATER_SAMPLE_TRIM;
    if (trim * 2 >= ok) trim = ok / 4;
    int64_t sum = 0;
    int cnt = 0;
    for (int i = trim; i < ok - trim; i++) {
        sum += buf[i];
        cnt++;
    }
    return (cnt > 0) ? (int)(sum / cnt) : buf[ok / 2];
}

/* 采集干燥基线 (启动时调用) */
void water_sensor_recalibrate(void)
{
    ESP_LOGI(TAG, "Calibrating dry baseline, please keep sensor dry...");
    /* 稳定后再采样: 先丢弃前几次 */
    for (int i = 0; i < 4; i++) {
        (void)water_read_avg(4);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    /* 连续采样 64 次求平均作为基线 */
    s_baseline = water_read_avg(64);
    ESP_LOGI(TAG, "Baseline ADC = %d", s_baseline);
}

void water_sensor_init(void)
{
    /* --- D0 数字引脚 --- */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << WATER_PIN_D0,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    /* --- A0 模拟引脚: 使用共享 ADC1 handle --- */
    s_adc_handle = adc1_bus_get();
    if (s_adc_handle == NULL) {
        ESP_LOGE(TAG, "failed to get shared ADC1 handle");
        return;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = WATER_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, WATER_ADC_CHANNEL, &chan_cfg));

    s_cali_ok = water_adc_calibration_init();

    /* 启动时采集干燥基线 (要求上电时传感器无水) */
    water_sensor_recalibrate();

    s_last_state = WATER_STATE_DRY;
    s_smoothed   = s_baseline;   // 初始平滑值 = 基线

    ESP_LOGI(TAG, "water sensor init done (D0=GPIO%d, A0=GPIO%d, baseline=%d)",
             WATER_PIN_D0, WATER_PIN_A0, s_baseline);
}

/* 三态判断 + 迟滞 */
static water_state_t judge_state(int delta, water_state_t last)
{
    /* 迟滞: 当前状态判定使用不同阈值, 避免反复跳变 */
    int t_damp_up   = WATER_DELTA_DAMP;
    int t_damp_dn   = WATER_DELTA_DAMP  - WATER_HYSTERESIS;
    int t_flood_up  = WATER_DELTA_FLOOD;
    int t_flood_dn  = WATER_DELTA_FLOOD - WATER_HYSTERESIS;

    switch (last) {
    case WATER_STATE_DRY:
        if (delta >= t_flood_up) return WATER_STATE_FLOOD;
        if (delta >= t_damp_up)  return WATER_STATE_DAMP;
        return WATER_STATE_DRY;

    case WATER_STATE_DAMP:
        if (delta >= t_flood_up) return WATER_STATE_FLOOD;
        if (delta <  t_damp_dn)  return WATER_STATE_DRY;
        return WATER_STATE_DAMP;

    case WATER_STATE_FLOOD:
    default:
        if (delta <  t_flood_dn) {
            /* 从积水下降, 但仍可能潮湿 */
            if (delta >= t_damp_dn) return WATER_STATE_DAMP;
            return WATER_STATE_DRY;
        }
        return WATER_STATE_FLOOD;
    }
}

/* 百分比映射: 以 FLOOD 阈值的 1.5 倍作为 100% */
static uint8_t delta_to_percent(int delta)
{
    if (delta <= 0) return 0;
    int full = (int)(WATER_DELTA_FLOOD * 3 / 2);   // 1200
    if (delta >= full) return 100;
    return (uint8_t)(delta * 100 / full);
}

void water_sensor_read(water_data_t *data)
{
    if (!data) return;

    /* 1) 空间维度: 中位数滤波剔除尖峰 */
    int raw = water_read_avg(WATER_SAMPLE_COUNT);

    /* 2) 时间维度: 指数平滑 (EMA), alpha=0.3, 抑制水面周期性波动
     *    smoothed = alpha*raw + (1-alpha)*smoothed
     *    整数运算: smoothed = (3*raw + 7*smoothed) / 10
     */
    if (s_smoothed < 0) s_smoothed = raw;
    s_smoothed = (3 * raw + 7 * s_smoothed) / 10;

    data->raw_adc  = s_smoothed;    // 对外呈现平滑后的值
    data->baseline = s_baseline;
    data->delta    = s_smoothed - s_baseline;
    if (data->delta < 0) data->delta = 0;
    data->percent  = delta_to_percent(data->delta);

    /* 电压 (基于平滑后的值) */
    int mv = 0;
    if (s_cali_ok && adc_cali_raw_to_voltage(s_cali_handle, s_smoothed, &mv) == ESP_OK) {
        data->voltage_mv = mv;
    } else {
        data->voltage_mv = s_smoothed * 3300 / 4095;
    }

    /* D0 数字判断 (有水通常 LOW) */
    data->d0_alert = (gpio_get_level(WATER_PIN_D0) == 0);

    /* 三态 + 迟滞 */
    water_state_t st = judge_state(data->delta, s_last_state);
    /* 如果 D0 触发, 强制至少 DAMP */
    if (data->d0_alert && st == WATER_STATE_DRY) {
        st = WATER_STATE_DAMP;
    }
    s_last_state = st;
    data->state  = st;
    data->alarm  = (st == WATER_STATE_FLOOD);
}

const char *water_state_str(water_state_t s)
{
    switch (s) {
    case WATER_STATE_DRY:   return "DRY";
    case WATER_STATE_DAMP:  return "DAMP";
    case WATER_STATE_FLOOD: return "FLOOD";
    default:                return "?";
    }
}