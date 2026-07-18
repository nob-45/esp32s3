/*
 * uart_probe.c —— LD2410 串口透传诊断
 *
 * 目的: 当雷达蓝牙搜不到时, 用 UART 直接读原始字节, 确认雷达是否活着.
 *
 * 接线:
 *   雷达 VCC -> 5V
 *   雷达 GND -> GND (必须共地!)
 *   雷达 TX  -> ESP32-S3 GPIO18  (RX)   <-- 交叉!
 *   雷达 RX  -> ESP32-S3 GPIO17  (TX)   <-- 交叉!
 *
 * 预期现象:
 *   雷达正常时, 每 ~100ms 会自动发一帧数据, 帧头 F4 F3 F2 F1, 帧尾 F8 F7 F6 F5.
 *   本程序把收到的每个字节以 HEX 打印, 你应该能看到滚动的 F4 F3 F2 F1 ... F8 F7 F6 F5.
 *
 * 无数据时的排查:
 *   1) 雷达 VCC/GND 是否插紧?     (万用表量雷达板上 5V 到 GND 是否 4.9V+)
 *   2) TX/RX 是否接反?             (交换 GPIO17 和 GPIO18 试试)
 *   3) 波特率是否正确?             (LD2410 默认 256000, 我们已按此配置)
 *   4) 是否是山寨 LD2410 无固件?   (用另一根线把雷达接到 USB 转 TTL 看能否直接抓到)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "uart_probe";

#define PROBE_UART_NUM      UART_NUM_1
#define PROBE_UART_TX_PIN   17     /* -> 雷达 RX */
#define PROBE_UART_RX_PIN   18     /* <- 雷达 TX */
#define PROBE_UART_BAUD     256000 /* LD2410 出厂默认 */
#define PROBE_RX_BUF_SIZE   1024

void uart_probe_forever(void)
{
    ESP_LOGI(TAG, "=== LD2410 UART PROBE START ===");
    ESP_LOGI(TAG, "Pins: TX=GPIO%d (->radar RX), RX=GPIO%d (<-radar TX)",
             PROBE_UART_TX_PIN, PROBE_UART_RX_PIN);
    ESP_LOGI(TAG, "Baud: %d 8N1", PROBE_UART_BAUD);

    /* 1) 配置 UART */
    uart_config_t uart_cfg = {
        .baud_rate  = PROBE_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(PROBE_UART_NUM,
                                        PROBE_RX_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(PROBE_UART_NUM, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(PROBE_UART_NUM,
                                 PROBE_UART_TX_PIN,
                                 PROBE_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART installed. Waiting for radar bytes...");
    ESP_LOGI(TAG, "Expected frame: F4 F3 F2 F1 ... F8 F7 F6 F5");

    uint8_t buf[128];
    uint64_t total_bytes = 0;
    int      empty_ticks = 0;

    while (1) {
        int len = uart_read_bytes(PROBE_UART_NUM, buf, sizeof(buf),
                                  pdMS_TO_TICKS(200));
        if (len > 0) {
            total_bytes += len;
            /* 打印 HEX (每行最多 32 字节, 便于阅读) */
            char line[32 * 3 + 8];
            int  off = 0;
            for (int i = 0; i < len; i++) {
                off += snprintf(line + off, sizeof(line) - off, "%02X ", buf[i]);
                if ((i & 31) == 31 || i == len - 1) {
                    ESP_LOGI(TAG, "RX[%d]: %s", len, line);
                    off = 0;
                }
            }
            empty_ticks = 0;
        } else {
            empty_ticks++;
            /* 每 ~2 秒无数据时提醒一次 */
            if (empty_ticks % 10 == 0) {
                ESP_LOGW(TAG, "no data for %d x 200ms. total_bytes=%llu",
                         empty_ticks, (unsigned long long)total_bytes);
                ESP_LOGW(TAG, "  -> check: 1) VCC/GND  2) TX/RX swap  3) baud 256000");
            }
        }
    }
}