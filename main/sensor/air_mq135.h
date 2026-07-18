/*
 * MQ-135 空气质量传感器驱动 (A0 模拟量, 相对基线 delta 三态判断)
 * 目标板: ESP32-S3-DevKitC-1 v1.1
 *
 * 应用场景: 独居老人室内空气监测
 *   - 干净空气基线记录后, 检测烟雾/燃气泄漏/异味/挥发有机物 (VOC/氨/苯)
 *   - MQ-135 需要 5V 供电 (加热丝), 上电需预热 ~20s 才稳定
 *   - 空气变差 -> 电阻下降 -> 分压电压上升 -> ADC 读数上升
 *
 * 接线:
 *   VCC -> 5V     (⚠️ 必须 5V, 加热丝需要)
 *   GND -> GND
 *   AO  -> GPIO7  (模拟输出, ADC1_CH6)
 *   DO  -> 不接   (软件阈值更灵活)
 */
#ifndef AIR_MQ135_H
#define AIR_MQ135_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 引脚配置 ==================== */
#define AIR_PIN_A0        7    // 模拟量输出 (GPIO7 = ADC1_CH6)

/* ==================== 检测阈值 (相对基线的增量, 单位: ADC counts) ==================== */
/* MQ-135 特性: 干净空气 baseline 约 500-1500 (个体差异大)
 * 有异味/烟雾时 delta 可轻松 +300~+1000
 * 保守阈值:  ODOR 150  (轻微异味/香水/酒精)
 *           BAD  400  (严重污染/浓烟/燃气)
 */
/* 实测本模块 (VCC=5V 稳定供电): baseline 约 100-150 (RL 较小的廉价模块特性)
 * 自然噪声 ±15 counts, 真实浓烟/吹气事件 delta 可到 +200~+300
 * 阈值按实测事件数据缩放, 保证既能触发也不误报 */
#define AIR_DELTA_ODOR      60     // 相对基线增量 >60  => 异味 (提醒)
#define AIR_DELTA_BAD      180     // 相对基线增量 >180 => 严重 (报警)

/* 迟滞值: 避免临界值反复跳变 */
#define AIR_HYSTERESIS     20

/* 状态持续判定: 必须连续 N 次超阈值才切换状态 (强抗抖动) */
#define AIR_CONFIRM_COUNT  3

/* 采样次数: 中位数滤波用 */
#define AIR_SAMPLE_COUNT   48
#define AIR_SAMPLE_TRIM    12      // 首尾各去掉 12 个极端值 (更强抗抖)

/* 预热时间: MQ-135 上电到读数稳定, 建议 >= 60 秒 (全新模块建议 24h burn-in) */
#define AIR_WARMUP_SEC     60

/* 基线自适应: CLEAN 状态下缓慢跟随环境漂移 (每次读取更新 1/64, 加快跟随) */
#define AIR_BASELINE_FOLLOW_SHIFT  6

/* ==================== 状态定义 ==================== */
typedef enum {
    AIR_STATE_CLEAN = 0,   // 清新
    AIR_STATE_ODOR  = 1,   // 异味 (提醒)
    AIR_STATE_BAD   = 2,   // 严重污染 (报警)
} air_state_t;

/* 传感器数据结构 */
typedef struct {
    int         raw_adc;     // 平滑后的 ADC 读数 (0~4095)
    int         voltage_mv;  // 电压值 (mV)
    int         baseline;    // 干净空气基线 ADC 值
    int         delta;       // 相对基线的增量 (raw_adc - baseline)
    uint8_t     percent;     // 相对污染百分比 (0~100), 用于进度条
    air_state_t state;       // 三态判断结果
    bool        alarm;       // 是否报警 (= state == AIR_STATE_BAD)
    bool        warmed_up;   // 是否已完成预热
} air_data_t;

/* ==================== 对外接口 ==================== */
/* 初始化传感器 (GPIO + ADC + 预热 + 采集干净空气基线)
 * 阻塞约 AIR_WARMUP_SEC 秒完成预热 */
void air_sensor_init(void);

/* 读取一次传感器数据 (内部多次采样求平均) */
void air_sensor_read(air_data_t *data);

/* 手动重新校准干净空气基线 (在通风环境下调用) */
void air_sensor_recalibrate(void);

/* 获取当前状态的字符串描述 (用于显示) */
const char *air_state_str(air_state_t s);

#ifdef __cplusplus
}
#endif

#endif // AIR_MQ135_H