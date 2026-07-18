/**
 * SmartElderCare - Wi-Fi Station + MJPEG HTTP 图传服务
 *
 * 工作机制：
 *   1) 以 Station 模式连接家里 Wi-Fi
 *   2) 启动一个 HTTP 服务器，监听 80 端口
 *   3) 电脑访问 http://<ESP32IP>/stream 就能拉到 MJPEG 视频流
 *   4) 电脑端 Python (OpenCV+YOLO) 直接解析这个流做跌倒识别
 *   5) 识别到跌倒后，电脑再 HTTP POST 回 /alarm 让 ESP32 触发本地声光报警
 */
#ifndef WIFI_MJPEG_H
#define WIFI_MJPEG_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Wi-Fi 账号密码通过编译期宏配置，避免硬编码到仓库 */
#ifndef ELDER_WIFI_SSID
#define ELDER_WIFI_SSID  "CMCC-2myg"
#endif
#ifndef ELDER_WIFI_PASS
#define ELDER_WIFI_PASS  "4yk36u2y"
#endif

/**
 * 初始化 Wi-Fi 并连接热点（非阻塞，内部起 task 重连）
 */
esp_err_t wifi_mjpeg_wifi_start(void);

/**
 * 启动 HTTP 图传服务器
 * 需要在 camera_ov2640_init 成功之后调用
 */
esp_err_t wifi_mjpeg_server_start(void);

/**
 * 查询当前是否已从上位机收到"跌倒"报警指令
 * 每次读取后自动清零，供 main 循环轮询触发本地声光
 */
bool wifi_mjpeg_pop_fall_alarm(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MJPEG_H */