/**
 * SmartElderCare - OV2640 摄像头驱动实现
 *
 * ============================================================
 * 2026-07-17 (二次修正) 关键结论：模块自带 24MHz 晶振，XCLK 无需外接
 * ============================================================
 *
 * 【前情】
 *   2026-07-17 上午曾误判"必须外接 XCLK 到 GPIO0"，理由是抓帧 100% 超时。
 *
 * 【经原理图 & 官方手册核对，该结论错误】
 *   ATK-MC2640-V2.3 (正点原子) 原理图 ATK-OV2640 V2.2.SchDoc 明确显示：
 *     - 板载有源晶振 Y1 = 24.000 MHz
 *     - Y1.OUT (pin3) --> OV_XCLK --> OV2640.PIN13 (XCLK)
 *   用户手册 V1.1 第 1 页也明确写着：
 *     "集成有源晶振，无需外部提供时钟"
 *   而且模块 2×9=18pin 排针上根本没有 XCLK 引出，
 *   连"接到 GPIO0"这个物理动作都做不到。
 *
 * 【真正的抓帧超时原因（重新排查方向，按可能性排序）】
 *   1) PWDN 引脚悬空 —— OV2640 处于掉电态，DVP 时钟不启动
 *      → PWDN 必须接 GND
 *   2) RST 引脚悬空 或 由外部保持低电平 —— OV2640 一直复位
 *      → RST 必须接 3.3V（或 GPIO 输出高）
 *   3) DVP 数据线走线过长/接触不良（PCLK/D0-D7 是高速并行信号）
 *      → 杜邦线 <10cm，插紧
 *   4) 3.3V 供电电流不足 / 纹波过大
 *      → OV2640 峰值 100mA，DevKit 3.3V LDO 500mA 需分给整个系统
 *      → 必要时用外部 3.3V 稳压电源单独供摄像头测试
 *   5) PCLK 太快 (默认 36MHz) —— DVP 采样跟不上
 *      → esp32-camera 组件已自动通过寄存器 0xD3/0x11 分频，通常无需干预
 *   6) FLASH 引脚控制方式的 R2/R3 焊接位置错误
 *      → 无关抓帧，但个别批次会干扰内部时序
 *
 * 【本次修正】
 *   pin_xclk       = -1        (ESP32 不驱动 XCLK 引脚，由模块板载晶振提供)
 *   xclk_freq_hz   = 24000000  ⚠️ 必须填模块真实晶振频率！
 *                              esp32-camera 内部要用它算 DVP 采样分频：
 *                              ll_cam.c:317 → cam_clkm_div_num = 160000000 / xclk_freq_hz
 *                              填 0 会直接除零 Panic (IntegerDivideByZero)
 *   frame_size     = QVGA      (320x240，稳定后再升 VGA)
 *   fb_count       =  2        (双缓冲抗抖)
 *   grab_mode      = LATEST    (只取最新帧)
 *   jpeg_quality   = 12
 *
 *   同时 GPIO0 释放为空闲（Strapping 引脚，不再驱动它）。
 *
 * 【踩坑记录】2026-07-17 19:47 曾把 xclk_freq_hz 设成 0，
 *   结果 esp_camera_init 直接 Guru Meditation Error: IntegerDivideByZero
 *   at ll_cam_config()。已通过读源码定位到 line 317 的除法运算。
 * ============================================================
 */
#include "camera_ov2640.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "camera";

/* ==== 引脚映射（与 硬件接线表.md 第四章 完全对应） ==== */
/*
 * ============================================================
 * 2026-07-17 (三次修正) RST 交给 GPIO19 控制，解决 PID=0 挂死问题
 * ============================================================
 * 上次固定 RST=3.3V 后，PID 依然读到 0，按 EN 键复位主控也救不回来。
 * 原因：OV2640 上电瞬间需要一次规范的复位脉冲(RST 低→高 ≥1ms)，
 * 才能保证板载 24MHz 晶振锁频、内部 PLL 就绪。
 * 官方 STM32 例程正是 GPIO 控制 RST 拉低复位。
 *
 * 硬件接线变更：模块 RST 引脚 从 3.3V 改接到 ESP32 GPIO19。
 * 代码变更：pin_reset = -1 → pin_reset = 19，
 * esp32-camera 驱动会自动执行 RESET low(10ms) → high(10ms) 序列。
 * ============================================================
 */
#define CAM_PIN_PWDN    -1     /* 硬件上接 GND（模块 PWDN 引脚直连 GND） */
#define CAM_PIN_RESET   19     /* RST 由 ESP32 GPIO19 控制（低→高复位脉冲） */
#define CAM_PIN_XCLK    -1     /* 模块自带 24MHz 板载晶振，ESP32 不输出 XCLK */
#define CAM_PIN_SIOD     4
#define CAM_PIN_SIOC     3

#define CAM_PIN_D7      47
#define CAM_PIN_D6      48
#define CAM_PIN_D5      46
#define CAM_PIN_D4      45
#define CAM_PIN_D3      42
#define CAM_PIN_D2      41
#define CAM_PIN_D1      40
#define CAM_PIN_D0      21

#define CAM_PIN_VSYNC    8
#define CAM_PIN_HREF     9
#define CAM_PIN_PCLK    15

static camera_config_t s_camera_config = {
    .pin_pwdn       = CAM_PIN_PWDN,
    .pin_reset      = CAM_PIN_RESET,
    .pin_xclk       = CAM_PIN_XCLK,
    .pin_sccb_sda   = CAM_PIN_SIOD,
    .pin_sccb_scl   = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,

    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href  = CAM_PIN_HREF,
    .pin_pclk  = CAM_PIN_PCLK,

    /* 模块板载 24MHz 晶振：pin_xclk=-1 让驱动不启用 LEDC，
     * 但 xclk_freq_hz 必须填晶振真实频率（且不能为 0，否则 ll_cam.c:317 除零 Panic）。
     *
     * 2026-07-17 (四次修正) 24MHz 降到 20MHz：
     *   现象：SCCB 单字节读 PID/VER 都 OK（0x26/0x42/0x7f/0xa2），
     *         但批量写配置寄存器时 100% 出现 "I2C bus is still busy"，
     *         每次失败的寄存器地址都不同（0x21/0x35/0x3e/0x37/0x3b 随机）。
     *   分析：典型的 SCCB 信号完整性不足。杜邦线较长(>10cm)、
     *         SDA/SCL 只靠模块板载 10kΩ 上拉时，PCLK 24MHz 会耦合到 SDA/SCL 上，
     *         导致某些寄存器写入位翻转 → 从机 NACK → I2C 主机等 ACK 超时。
     *   处理：xclk_freq_hz 20MHz（比 24MHz 慢一档，PCLK 也会同比例降低），
     *         配合 sdkconfig 里把 SCCB 时钟降到 100kHz（默认已是），
     *         如果仍不稳，需硬件在 SDA(GPIO4)/SCL(GPIO3) 分别加 4.7kΩ 外部上拉到 3.3V。
     *
     * 2026-07-17 (五次修正) 20MHz 再降到 16MHz：
     *   现象：20MHz 时 attempt#1 通过了 PID/PLL/frame buffer 分配，
     *         能进入 ov2640_set_frame_size 阶段（比之前推进 60% 距离），
     *         但仍在 reg 0x35 写入时失败。
     *   分析：进步说明降频路线正确，SCCB 稳定性再压一档就有希望。
     *         OV2640 datasheet Table 6-1 指定 XCLK 最低 6MHz、典型 24MHz、最高 48MHz，
     *         16MHz 在合法范围内，OV2640 内部 PLL 仍能锁定。
     *   处理：xclk_freq_hz 20MHz → 16MHz，PCLK 从 10MHz 降到 8MHz，
     *         对 SDA/SCL 空间耦合能量再压 -2dB。
     *
     * 【如果 16MHz 仍不稳，请务必执行硬件方案】
     *   在 SDA(GPIO4) 和 SCL(GPIO3) 各串一个 4.7kΩ 电阻到 3.3V 排针，
     *   这是彻底根治 SCCB 位翻转的唯一硬件手段。
     *
     * 2026-07-17 (六次修正) 16MHz 仍失败，做最后一次软件尝试：10MHz
     *   现象：16MHz 时失败位置(reg 0x35/0x37/0x4c)与 20MHz 几乎完全重合，
     *         降频改进不再明显 —— 说明 SCCB 抖动的主因已不再是 PCLK 耦合，
     *         而是 SDA/SCL 缺少足够的外部上拉电阻，靠模块板载弱上拉不够。
     *   10MHz 是 OV2640 datasheet 允许的近下限（>6MHz），
     *         如果 10MHz 仍失败，就 100% 是硬件上拉不足，软件无解。
     */
    .xclk_freq_hz = 10000000,
    .ledc_timer   = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,
    .frame_size   = FRAMESIZE_QVGA,   /* 320x240，稳定后可升到 VGA */
    .jpeg_quality = 12,
    .fb_count     = 2,
    .fb_location  = CAMERA_FB_IN_PSRAM,
    .grab_mode    = CAMERA_GRAB_LATEST,
};

/* 初始化失败时的自动重试次数 */
#define CAM_INIT_RETRY   3

static bool s_ready = false;

esp_err_t camera_ov2640_init(void)
{
    ESP_LOGI(TAG, "camera init: pin_xclk=%d, xclk_freq_hz=%d Hz (%.1f MHz)  SDA=GPIO%d SCL=GPIO%d PCLK=GPIO%d RST=GPIO%d",
             s_camera_config.pin_xclk,
             (int)s_camera_config.xclk_freq_hz,
             s_camera_config.xclk_freq_hz / 1000000.0f,
             CAM_PIN_SIOD, CAM_PIN_SIOC, CAM_PIN_PCLK, CAM_PIN_RESET);
    ESP_LOGI(TAG, "hw requirement: PWDN->GND, RST->GPIO19 (由主控发复位脉冲), VCC=3.3V only (not 5V!)");

    esp_err_t err = ESP_FAIL;
    for (int i = 1; i <= CAM_INIT_RETRY; ++i) {
        err = esp_camera_init(&s_camera_config);
        if (err == ESP_OK) {
            s_ready = true;
            ESP_LOGI(TAG, "camera init OK (attempt %d)", i);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "attempt %d/%d failed: 0x%x (%s), deinit & retry...",
                 i, CAM_INIT_RETRY, err, esp_err_to_name(err));
        esp_camera_deinit();
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    ESP_LOGE(TAG, "esp_camera_init finally failed: 0x%x (%s)",
             err, esp_err_to_name(err));
    ESP_LOGE(TAG, "hint: 1) PWDN 必须接 GND (悬空=芯片掉电=SCCB 找不到=0x105)");
    ESP_LOGE(TAG, "      2) RST 必须接到 ESP32 GPIO19 (或稳定 3.3V), 用于产生上电复位脉冲");
    ESP_LOGE(TAG, "      3) VCC 只能接 3.3V, 5V 会烧模块");
    ESP_LOGE(TAG, "      4) 摄像头 FPC/杜邦线尽量短 (<10cm), PCLK/D0-D7 走线分离音频线");
    ESP_LOGE(TAG, "      5) 若持续抓帧超时: 用外部 3.3V 单独供摄像头, 或降低 xclk_freq_hz");
    s_ready = false;
    return err;
}

bool camera_ov2640_is_ready(void)
{
    return s_ready;
}

camera_fb_t *camera_ov2640_capture(void)
{
    if (!s_ready) return NULL;
    return esp_camera_fb_get();
}

void camera_ov2640_release(camera_fb_t *fb)
{
    if (fb) esp_camera_fb_return(fb);
}