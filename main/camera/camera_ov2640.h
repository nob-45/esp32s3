/**
 * SmartElderCare - OV2640 摄像头驱动封装
 * 基于 espressif/esp32-camera 组件
 */
#ifndef CAMERA_OV2640_H
#define CAMERA_OV2640_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化 OV2640 摄像头
 * @return ESP_OK 成功；其它值失败（此时应回退到无摄像头模式）
 */
esp_err_t camera_ov2640_init(void);

/**
 * 判断摄像头是否已就绪（init 成功后为 true）
 */
bool camera_ov2640_is_ready(void);

/**
 * 抓取一帧 JPEG（同步接口，用完必须调用 camera_ov2640_release）
 * @return 成功返回帧指针，失败返回 NULL
 */
camera_fb_t *camera_ov2640_capture(void);

/**
 * 释放帧缓冲，归还给驱动
 */
void camera_ov2640_release(camera_fb_t *fb);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_OV2640_H */