/*
 * ADC1 共享总线 (单例)
 * 目标: ESP-IDF 5.x 中同一个 ADC unit 只能 new_unit 一次,
 *       多个传感器 (water_sensor / air_mq135) 共用 ADC1 时,
 *       通过本模块获取同一个 handle, 避免重复初始化。
 */
#ifndef ADC1_BUS_H
#define ADC1_BUS_H

#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 获取 ADC1 单例 handle (线程安全, 首次调用时创建)
 * 返回 NULL 表示创建失败 */
adc_oneshot_unit_handle_t adc1_bus_get(void);

#ifdef __cplusplus
}
#endif

#endif // ADC1_BUS_H