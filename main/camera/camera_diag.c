/**
 * SmartElderCare - 摄像头硬件诊断工具实现
 * 详见 camera_diag.h
 * 适配 ATK-MC2640-V2.3 (模块自带 24MHz 晶振, 无 XCLK 引脚)
 *
 * ★ 注意 ★
 * ESP-IDF 5.x 起 esp_camera 组件已切换到新的 i2c_master (driver_ng),
 * 若本文件再包含 "driver/i2c.h" 使用老驱动, 会在启动阶段的
 * do_global_ctors 里触发 CONFLICT! driver_ng is not allowed...
 * 直接 abort, 板子进入无限重启.
 *
 * 因此这里已经彻底移除老 I2C API 的调用. SCCB 扫描 / PID 读取
 * 的诊断功能不再自己实现 (esp_camera_init 内部已经会做 SCCB 读 PID,
 * 失败时会返回 ESP_ERR_NOT_FOUND, 效果等价).
 *
 * 保留的诊断能力:
 *   - 引脚映射打印
 *   - SDA/SCL 静态电平检测 (纯 GPIO)
 *   - PCLK / VSYNC / HREF 动态翻转采样 (纯 GPIO)
 */
#include "camera_diag.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "cam_diag";

/* 与 camera_ov2640.c 保持一致 */
#define CAM_PIN_SIOD    4
#define CAM_PIN_SIOC    3
#define CAM_PIN_PCLK    15
#define CAM_PIN_VSYNC   8
#define CAM_PIN_HREF    9

static void diag_print_pins(void)
{
    ESP_LOGI(TAG, "===== camera GPIO map (ATK-MC2640-V2.3) =====");
    ESP_LOGI(TAG, "  PWDN  -> GND  (must be tied to GND!)");
    ESP_LOGI(TAG, "  RESET -> 3V3  (must be tied to 3V3!)");
    ESP_LOGI(TAG, "  XCLK  = NC (module has 24MHz XTAL onboard)");
    ESP_LOGI(TAG, "  SDA   = GPIO%d", CAM_PIN_SIOD);
    ESP_LOGI(TAG, "  SCL   = GPIO%d", CAM_PIN_SIOC);
    ESP_LOGI(TAG, "  VSYNC = GPIO%d", CAM_PIN_VSYNC);
    ESP_LOGI(TAG, "  HREF  = GPIO%d", CAM_PIN_HREF);
    ESP_LOGI(TAG, "  PCLK  = GPIO%d", CAM_PIN_PCLK);
    ESP_LOGI(TAG, "  D0..D7 = GPIO 21,40,41,42,45,46,48,47");
}

/* 检测 SDA/SCL 外部上拉是否存在 (关键: 判断电源和上拉是否OK) */
static void diag_check_pullup(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << CAM_PIN_SIOD) | (1ULL << CAM_PIN_SIOC),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    vTaskDelay(pdMS_TO_TICKS(20));
    int sda_ext = gpio_get_level(CAM_PIN_SIOD);
    int scl_ext = gpio_get_level(CAM_PIN_SIOC);
    ESP_LOGI(TAG, "external pullup test: SDA=%d SCL=%d (expect 1/1)",
             sda_ext, scl_ext);
    if (sda_ext == 0 || scl_ext == 0) {
        ESP_LOGW(TAG, "*** WARNING: SDA/SCL not pulled high ! ***");
        ESP_LOGW(TAG, "*** camera power or pull-up resistor missing ***");
        ESP_LOGW(TAG, "*** check 3V3 with meter, PWDN really to GND ? ***");
    } else {
        ESP_LOGI(TAG, "SDA/SCL both HIGH, external pull-up OK");
    }
}

/* SCCB 扫描 / PID 读取已移除:
 * ESP-IDF 5.x 的 esp_camera 组件使用新的 i2c_master 驱动 (driver_ng),
 * 若这里再用老 driver/i2c.h, 启动时会 abort. 而且 esp_camera_init()
 * 本身就会做 SCCB 读 PID 校验, 失败会返回明确错误码, 没必要重复实现.
 */

void camera_diag_run(void)
{
    ESP_LOGI(TAG, "========== camera diagnosis (GPIO only) ==========");
    diag_print_pins();
    diag_check_pullup();
    ESP_LOGI(TAG, "note: SCCB scan removed to avoid i2c driver conflict.");
    ESP_LOGI(TAG, "      run normal mode; esp_camera_init will report PID.");
    ESP_LOGI(TAG, "========== camera diagnosis end ==========");
}

/* ================================================================
 * 数据总线信号探测: 检查 PCLK / VSYNC / HREF 是否在翻转
 * 说明：
 *  - esp_camera_init() 会把这些 GPIO 复用给 LCD_CAM 外设，我们这里
 *    强制把它们重新配为普通 GPIO 输入，采样一段时间后计数电平变化。
 *  - PCLK 应该是几 MHz 高速方波，几毫秒内就能采到大量翻转。
 *  - VSYNC 每帧一次，采 100ms 内至少能看到 1-2 次翻转。
 *  - 如果三条线全是静止的（全 0 或全 1），说明摄像头虽然 SCCB 通
 *    但 DVP 输出停摆，99% 是 PWDN 引脚没接 GND、或模块晶振没起振。
 * 调用后需要 esp_camera_deinit() → esp_camera_init() 才能恢复正常抓帧，
 * 所以这个函数只用于一次性诊断。
 * ================================================================ */
void camera_diag_probe_signals(void)
{
    const int pins[3]     = { CAM_PIN_PCLK, CAM_PIN_VSYNC, CAM_PIN_HREF };
    const char *names[3]  = { "PCLK", "VSYNC", "HREF" };

    ESP_LOGI(TAG, "===== camera DVP signal probe =====");
    ESP_LOGW(TAG, "note: this will TEMPORARILY reconfig PCLK/VSYNC/HREF as GPIO input");
    ESP_LOGW(TAG, "      camera capture will BREAK after this probe until reset!");

    for (int i = 0; i < 3; i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = (1ULL << pins[i]),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
    }
    vTaskDelay(pdMS_TO_TICKS(5));

    /* 快速采样：200ms 内每 10us 采一次，共 20000 次 */
    const int SAMPLE_N = 20000;
    int transitions[3] = { 0, 0, 0 };
    int last[3]        = {
        gpio_get_level(pins[0]),
        gpio_get_level(pins[1]),
        gpio_get_level(pins[2]),
    };
    int64_t t0 = esp_timer_get_time();
    for (int k = 0; k < SAMPLE_N; k++) {
        for (int i = 0; i < 3; i++) {
            int lv = gpio_get_level(pins[i]);
            if (lv != last[i]) {
                transitions[i]++;
                last[i] = lv;
            }
        }
        /* 10us busy wait */
        int64_t target = t0 + (int64_t)(k + 1) * 10;
        while (esp_timer_get_time() < target) { }
    }
    int64_t dur = esp_timer_get_time() - t0;

    ESP_LOGI(TAG, "sampled for %lld us (target 200ms):", dur);
    for (int i = 0; i < 3; i++) {
        int lv_now = gpio_get_level(pins[i]);
        ESP_LOGI(TAG, "  %-5s (GPIO%2d): level=%d transitions=%d",
                 names[i], pins[i], lv_now, transitions[i]);
    }

    bool pclk_alive  = transitions[0] > 100;
    bool vsync_alive = transitions[1] > 0;
    bool href_alive  = transitions[2] > 0;

    ESP_LOGI(TAG, "-----------------------------------");
    if (pclk_alive && vsync_alive && href_alive) {
        ESP_LOGI(TAG, "[VERDICT] camera DVP is ALIVE. capture failure is a timing/DMA issue.");
        ESP_LOGI(TAG, "  try: lower XCLK to 20MHz, or use QQVGA, or check PSRAM speed");
    } else if (!pclk_alive) {
        ESP_LOGE(TAG, "[VERDICT] *** PCLK IS DEAD ! ***");
        ESP_LOGE(TAG, "  the OV2640 is not clocking out any pixels.");
        ESP_LOGE(TAG, "  most likely causes (in order of probability):");
        ESP_LOGE(TAG, "   1) PWDN pin FLOATING -> pull it to GND with a wire!");
        ESP_LOGE(TAG, "   2) module's onboard 24MHz XTAL not oscillating (bad solder)");
        ESP_LOGE(TAG, "   3) module actually needs external XCLK (check silk-screen)");
        ESP_LOGE(TAG, "   4) VCC not stable 3.3V under load");
    } else {
        ESP_LOGW(TAG, "[VERDICT] partial signal detected, camera may be in test mode.");
        ESP_LOGW(TAG, "  PCLK=%s VSYNC=%s HREF=%s",
                 pclk_alive?"OK":"DEAD",
                 vsync_alive?"OK":"DEAD",
                 href_alive?"OK":"DEAD");
    }
    ESP_LOGI(TAG, "===== signal probe end =====");
}
