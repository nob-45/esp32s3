/*
 * HLK-LD2410B-P 雷达驱动实现 (玩法A: 只读 OUT 引脚)
 */
#include "radar_ld2410.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "radar";

/* 稳态状态机内部变量 */
static bool     s_stable_presence = false;   // 已确认的稳态
static bool     s_candidate       = false;   // 候选新状态
static int64_t  s_candidate_since = 0;       // 候选状态起始时间(us)
static int64_t  s_stable_since    = 0;       // 稳态起始时间(us)
static int64_t  s_last_beat_us    = 0;       // 心跳日志上次打印时间

/* GPIO 通路自检: 分别用内部上拉/下拉观察电平
 *   - 若开上拉读到 HIGH, 开下拉读到 LOW  => 引脚悬空(雷达 OUT 未接入或雷达没供电)
 *   - 若开上拉/下拉都读到 LOW           => 雷达 OUT 已连入且当前无人 (正常)
 *   - 若开上拉/下拉都读到 HIGH          => 雷达 OUT 已连入且当前检测到人
 */
static void radar_self_check(void)
{
    /* 先用内部上拉 */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << RADAR_PIN_OUT,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    vTaskDelay(pdMS_TO_TICKS(50));
    int lv_pu = gpio_get_level(RADAR_PIN_OUT);

    /* 再用内部下拉 */
    io.pull_up_en   = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_ENABLE;
    gpio_config(&io);
    vTaskDelay(pdMS_TO_TICKS(50));
    int lv_pd = gpio_get_level(RADAR_PIN_OUT);

    ESP_LOGI(TAG, "GPIO%d self-check: pull-up=%d, pull-down=%d",
             RADAR_PIN_OUT, lv_pu, lv_pd);
    if (lv_pu == 1 && lv_pd == 0) {
        ESP_LOGW(TAG, ">>> pin FLOATING! Check wiring: LD2410 OUT -> GPIO%d, and common GND",
                 RADAR_PIN_OUT);
    } else if (lv_pu == 0 && lv_pd == 0) {
        ESP_LOGI(TAG, ">>> pin driven LOW  (radar connected, currently EMPTY)");
    } else if (lv_pu == 1 && lv_pd == 1) {
        ESP_LOGI(TAG, ">>> pin driven HIGH (radar connected, currently OCCUPIED)");
    }
}

void radar_init(void)
{
    /* 上电自检 (阻塞 ~120ms) */
    radar_self_check();

    /* 正式配置: 下拉, 防止悬空误判 */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << RADAR_PIN_OUT,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,  // 悬空时默认低电平, 避免误判
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    int64_t now = esp_timer_get_time();
    s_stable_presence = false;
    s_candidate       = false;
    s_candidate_since = now;
    s_stable_since    = now;

    ESP_LOGI(TAG, "LD2410 OUT pin GPIO%d ready", RADAR_PIN_OUT);
}

void radar_read(radar_data_t *d)
{
    if (!d) return;

    int64_t now = esp_timer_get_time();
    bool level  = gpio_get_level(RADAR_PIN_OUT) != 0;
    d->raw_level = level;

    /* 去抖状态机:
     * 若当前采样与稳态不同, 记为候选; 候选连续维持超过 RADAR_STABLE_MS 才切换稳态
     */
    if (level == s_stable_presence) {
        /* 与稳态一致, 重置候选 */
        s_candidate       = level;
        s_candidate_since = now;
    } else {
        if (level != s_candidate) {
            /* 候选发生变化, 重新计时 */
            s_candidate       = level;
            s_candidate_since = now;
        } else {
            /* 候选持续中, 检查是否够稳定 */
            if ((now - s_candidate_since) / 1000 >= RADAR_STABLE_MS) {
                s_stable_presence = level;
                s_stable_since    = now;
                ESP_LOGI(TAG, "presence -> %s", level ? "OCCUPIED" : "EMPTY");
            }
        }
    }

    d->presence  = s_stable_presence;
    d->stable_ms = (uint32_t)((now - s_stable_since) / 1000);

    /* 心跳: 每 3 秒无条件打印一次原始电平, 用于确认驱动在跑 */
    if ((now - s_last_beat_us) >= 3000000LL) {
        s_last_beat_us = now;
        ESP_LOGI(TAG, "beat: raw=%d presence=%d idle=%lus",
                 level ? 1 : 0,
                 s_stable_presence ? 1 : 0,
                 (unsigned long)(d->stable_ms / 1000));
    }
}

const char *radar_state_str(bool presence)
{
    return presence ? "OCCUPIED" : "  EMPTY ";
}