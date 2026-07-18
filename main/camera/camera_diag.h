/**
 * SmartElderCare - 摄像头硬件诊断工具
 *
 * 当 camera_ov2640_init() 返回 0x105 (ESP_ERR_NOT_FOUND) 时，
 * 说明 SCCB 探测不到 OV2640，通常是接线/供电问题。
 * 调用 camera_diag_run() 会：
 *   1) 打印所有摄像头 GPIO 映射
 *   2) 在 XCLK 引脚上产生 20MHz 时钟
 *   3) 独立扫描 SCCB(I2C) 总线，报告发现的设备地址
 *   4) 若发现 OV2640(0x30)，读 PID 寄存器验证
 * 该函数与 camera_ov2640_init() 互斥（会占用 I2C0 与 LEDC1），
 * 使用完毕后请复位板子再走正常初始化流程。
 */
#ifndef CAMERA_DIAG_H
#define CAMERA_DIAG_H

#ifdef __cplusplus
extern "C" {
#endif

void camera_diag_run(void);

/**
 * 检测摄像头数据总线(PCLK/VSYNC/HREF)是否有信号翻转。
 * 在 esp_camera_init() 成功之后调用，用来判断"SCCB 通但抓不到帧"的根因：
 *   - PCLK 有翻转 → 摄像头正常工作，问题在 DMA/时序
 *   - PCLK 无翻转 → 摄像头处于省电或时钟死掉状态 (通常是 PWDN 悬空)
 */
void camera_diag_probe_signals(void);

#ifdef __cplusplus
}
#endif

#endif