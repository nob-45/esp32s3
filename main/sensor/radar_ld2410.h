/*
 * HLK-LD2410B-P 24GHz 毫米波雷达人体存在检测驱动
 * 目标板: ESP32-S3-DevKitC-1 v1.1
 *
 * 当前实现: 玩法A - 只读 OUT 引脚数字电平 (有人=高, 无人=低)
 *   - 简单可靠, 不依赖串口协议解析
 *   - 距离/灵敏度/无人延时通过官方手机APP HLKRadarTool一次性配置
 *
 * 未来升级: 玩法B - 加接 UART (RX->GPIO18, TX->GPIO17), 读取实时距离/能量
 *   本文件预留接口 radar_uart_read(), 后续实现
 *
 * 接线:
 *   VCC -> 5V   (雷达标称 5-12V, 用 DevKit 板 5V 引脚即可)
 *   GND -> GND
 *   OUT -> GPIO4  (数字输入, 有人=HIGH, 无人=LOW)
 *   RX  -> 悬空 (玩法A 不接)
 *   TX  -> 悬空 (玩法A 不接)
 */
#ifndef RADAR_LD2410_H
#define RADAR_LD2410_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 引脚配置 ==================== */
#define RADAR_PIN_OUT    4     // OUT 数字输出引脚 (有人=HIGH)

/* ==================== 去抖参数 ==================== */
/* 电平必须稳定超过此毫秒数, 才认为状态真正切换
 * 防止 OUT 引脚在临界距离处频繁抖动
 */
#define RADAR_STABLE_MS  200

/* ==================== 数据结构 ==================== */
typedef struct {
    bool     raw_level;       // OUT 引脚原始电平 (未去抖)
    bool     presence;        // 稳态: 是否检测到人
    uint32_t stable_ms;       // 当前稳态已持续的时间 (毫秒)
} radar_data_t;

/* ==================== 对外接口 ==================== */
/* 初始化雷达 OUT 引脚 (数字输入 + 下拉) */
void radar_init(void);

/* 读取当前雷达状态 (含去抖) - 每次主循环调用一次即可 */
void radar_read(radar_data_t *d);

/* 状态字符串: "OCCUPIED" / "EMPTY" */
const char *radar_state_str(bool presence);

#ifdef __cplusplus
}
#endif

#endif // RADAR_LD2410_H