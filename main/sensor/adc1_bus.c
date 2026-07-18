/*
 * ADC1 共享总线 (单例) - 实现
 */
#include "adc1_bus.h"
#include "esp_log.h"

static const char *TAG = "adc1_bus";

static adc_oneshot_unit_handle_t s_handle = NULL;

adc_oneshot_unit_handle_t adc1_bus_get(void)
{
    if (s_handle != NULL) {
        return s_handle;
    }
    adc_oneshot_unit_init_cfg_t cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&cfg, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        s_handle = NULL;
    } else {
        ESP_LOGI(TAG, "ADC1 unit created (shared)");
    }
    return s_handle;
}