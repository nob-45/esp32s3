/*
 * 浴室积水检测传感器驱动 (D0 数字阈值 + A0 模拟量)
 * 目标板: ESP32-S3-DevKitC-1 v1.1
 *
 * 应用场景: 浴室 / 卫生间地面积水检测
 *   - 传感器平铺在地面, 干燥时 ADC 读数很低 (~50-300)
 *   - 有水膜覆盖时 ADC 读数上升, 触发报警
 *
 * 接线:
 *   VCC -> 3.3V
 *   GND -> GND
 *   AO  -> GPIO6  (模拟输出, ADC1_CH5)
 *   DO  -> GPIO5  (数字输出, 有水=LOW)
 */
#ifndef WATER_SENSOR_H
#define WATER_SENSOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 引脚配置 ==================== */
#define WATER_PIN_A0     6    // 模拟量输出 (GPIO6 = ADC1_CH5)
#define WATER_PIN_D0     5    // 数字阈值输出 (GPIO5)

/* ==================== 检测阈值 (相对基线的增量, 单位: ADC counts) ==================== */
/* 启动时会自动记录"干燥基线", 之后按相对增量判断
 * 实测: 干燥 baseline≈285, 半浸水 delta≈300, 深水 delta≈500-600
 * 因此阈值:  DAMP 80  (轻微沾水/水膜)
 *           FLOOD 250 (明显积水)
 */
#define WATER_DELTA_DAMP    80     // 相对基线增量 >80  => 潮湿
#define WATER_DELTA_FLOOD   250    // 相对基线增量 >250 => 积水报警

/* 迟滞值: 避免临界值反复跳变 */
#define WATER_HYSTERESIS    30

/* 采样次数: 中位数滤波用, 采 32 次排序后取中间 16 个求均 (强力抗尖峰) */
#define WATER_SAMPLE_COUNT  32
#define WATER_SAMPLE_TRIM   8      // 首尾各去掉 8 个极端值

/* ==================== 状态定义 ==================== */
typedef enum {
    WATER_STATE_DRY    = 0,   // 干燥
    WATER_STATE_DAMP   = 1,   // 潮湿 (少量水)
    WATER_STATE_FLOOD  = 2,   // 积水 (报警)
} water_state_t;

/* 传感器数据结构 */
typedef struct {
    int           raw_adc;      // 平均后的 ADC 读数 (0~4095)
    int           voltage_mv;   // 电压值 (mV)
    int           baseline;     // 干燥基线 ADC 值
    int           delta;        // 相对基线的增量 (raw_adc - baseline)
    uint8_t       percent;      // 相对水量百分比 (0~100), 用于进度条显示
    bool          d0_alert;     // D0 数字阈值是否触发
    water_state_t state;        // 三态判断结果
    bool          alarm;        // 是否报警 (= state == WATER_STATE_FLOOD)
} water_data_t;

/* ==================== 对外接口 ==================== */
/* 初始化传感器 (GPIO + ADC + 采集干燥基线) */
void water_sensor_init(void);

/* 读取一次传感器数据 (内部多次采样求平均) */
void water_sensor_read(water_data_t *data);

/* 手动重新校准干燥基线 (在传感器无水时调用) */
void water_sensor_recalibrate(void);

/* 获取当前状态的字符串描述 (用于显示) */
const char *water_state_str(water_state_t s);

#ifdef __cplusplus
}
#endif

#endif // WATER_SENSOR_H