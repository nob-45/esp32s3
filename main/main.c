/*
 * SmartElderCare 智慧养老网关 - 主程序
 * 目标板: ESP32-S3-DevKitC-1 v1.1
 *
 * 当前阶段 (P3.2): LCD + 浴室积水检测 (三态: DRY / DAMP / FLOOD)
 *   算法: 中位数滤波(空间) + EMA指数平滑(时间) + 迟滞三态判断
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "lcd_st7789.h"
#include "water_sensor.h"

static const char *TAG = "main";

/* ===== 主题色 ===== */
#define UI_BG           0x0000
#define UI_TITLE_BG     0x02B5
#define UI_TITLE_FG     COLOR_WHITE
#define UI_CARD_BG      0x2124
#define UI_CARD_BORDER  0x4A69
#define UI_LABEL        0x7BEF
#define UI_VALUE        COLOR_WHITE
#define UI_ACCENT       0x07FF
#define UI_OK           0x2FE4  // DRY   绿
#define UI_WARN         0xFD20  // DAMP  橙
#define UI_ALARM        0xF9E7  // FLOOD 红

/* 布局 */
#define TITLE_H     40
#define STATUS_Y    44
#define STATUS_H    80
#define PCT_Y       128
#define BAR_Y       176
#define BAR_H       28
#define INFO_Y      212
#define INFO_H      80
#define FOOT_Y      296
#define FOOT_H      24

static uint16_t state_to_color(water_state_t s)
{
    switch (s) {
    case WATER_STATE_FLOOD: return UI_ALARM;
    case WATER_STATE_DAMP:  return UI_WARN;
    default:                return UI_OK;
    }
}

static const char *state_to_big(water_state_t s)
{
    switch (s) {
    case WATER_STATE_FLOOD: return "FLOOD!";
    case WATER_STATE_DAMP:  return " DAMP ";
    default:                return " DRY  ";
    }
}

static const char *state_to_sub(water_state_t s)
{
    switch (s) {
    case WATER_STATE_FLOOD: return "BATHROOM WATER LEAK";
    case WATER_STATE_DAMP:  return "Slight moisture";
    default:                return "Floor is dry";
    }
}

static void ui_draw_card(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                         uint16_t bg, uint16_t border)
{
    lcd_fill_rect(x, y, w, h, bg);
    lcd_fill_rect(x, y, w, 1, border);
    lcd_fill_rect(x, y + h - 1, w, 1, border);
    lcd_fill_rect(x, y, 1, h, border);
    lcd_fill_rect(x + w - 1, y, 1, h, border);
}

static void ui_draw_static(void)
{
    lcd_fill_screen(UI_BG);

    lcd_fill_rect(0, 0, LCD_WIDTH, TITLE_H, UI_TITLE_BG);
    lcd_draw_string_2x(10, 4, "SmartCare", UI_TITLE_FG, UI_TITLE_BG);
    lcd_draw_string(178, 20, "v0.5", UI_ACCENT, UI_TITLE_BG);
    lcd_fill_rect(0, TITLE_H, LCD_WIDTH, 2, UI_ACCENT);

    lcd_draw_string(8, PCT_Y + 12, "WATER LEVEL", UI_LABEL, UI_BG);

    ui_draw_card(6, INFO_Y, LCD_WIDTH - 12, INFO_H, UI_CARD_BG, UI_CARD_BORDER);
    lcd_draw_string(14, INFO_Y +  8, "ADC(EMA):", UI_LABEL, UI_CARD_BG);
    lcd_draw_string(14, INFO_Y + 28, "Baseline:", UI_LABEL, UI_CARD_BG);
    lcd_draw_string(14, INFO_Y + 48, "Delta   :", UI_LABEL, UI_CARD_BG);
    lcd_draw_string(14, INFO_Y + 68, "Th D/F  :", UI_LABEL, UI_CARD_BG);

    lcd_fill_rect(0, FOOT_Y, LCD_WIDTH, FOOT_H, UI_TITLE_BG);
    lcd_draw_string(8, FOOT_Y + 6, "Bathroom Leak Guard", UI_TITLE_FG, UI_TITLE_BG);
}

static void ui_update_status(water_state_t s)
{
    uint16_t bg = state_to_color(s);
    ui_draw_card(6, STATUS_Y, LCD_WIDTH - 12, STATUS_H, bg, COLOR_WHITE);

    const char *big = state_to_big(s);
    const char *sub = state_to_sub(s);

    uint16_t w_big = lcd_measure_string_2x(big);
    int bx = (LCD_WIDTH - w_big) / 2;
    if (bx < 0) bx = 0;
    lcd_draw_string_2x(bx, STATUS_Y + 12, big, COLOR_WHITE, bg);

    int sw = (int)strlen(sub) * 8;
    int sx = (LCD_WIDTH - sw) / 2;
    if (sx < 0) sx = 0;
    lcd_draw_string(sx, STATUS_Y + 56, sub, COLOR_WHITE, bg);
}

static void ui_update_level(uint8_t percent, water_state_t s)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%3d%%", percent);

    uint16_t px = LCD_WIDTH - 4 * 16 - 10;
    lcd_fill_rect(px, PCT_Y, 4 * 16 + 10, 32, UI_BG);

    uint16_t color = state_to_color(s);
    lcd_draw_string_2x(px, PCT_Y, buf, color, UI_BG);

    lcd_draw_progress(10, BAR_Y, LCD_WIDTH - 20, BAR_H,
                      percent, color, UI_CARD_BG, UI_CARD_BORDER);
}

static void ui_update_info(const water_data_t *d)
{
    char buf[32];

    /* 第1行: 平滑后的 ADC 值 (EMA滤波结果) */
    snprintf(buf, sizeof(buf), "%4d", d->raw_adc);
    lcd_fill_rect(100, INFO_Y + 8, 130, 16, UI_CARD_BG);
    lcd_draw_string(100, INFO_Y + 8, buf, UI_VALUE, UI_CARD_BG);

    /* 第2行: 干燥基线 */
    snprintf(buf, sizeof(buf), "%4d", d->baseline);
    lcd_fill_rect(100, INFO_Y + 28, 130, 16, UI_CARD_BG);
    lcd_draw_string(100, INFO_Y + 28, buf, UI_LABEL, UI_CARD_BG);

    /* 第3行: Delta = 平滑ADC - baseline, 颜色反映状态 */
    snprintf(buf, sizeof(buf), "%+4d", d->delta);
    lcd_fill_rect(100, INFO_Y + 48, 130, 16, UI_CARD_BG);
    uint16_t dcol = (d->delta >= WATER_DELTA_FLOOD) ? UI_ALARM
                  : (d->delta >= WATER_DELTA_DAMP)  ? UI_WARN
                  :                                    UI_OK;
    lcd_draw_string(100, INFO_Y + 48, buf, dcol, UI_CARD_BG);

    /* 第4行: 双阈值提示 DAMP/FLOOD, 方便现场调参 */
    snprintf(buf, sizeof(buf), "%d/%d", WATER_DELTA_DAMP, WATER_DELTA_FLOOD);
    lcd_fill_rect(100, INFO_Y + 68, 130, 16, UI_CARD_BG);
    lcd_draw_string(100, INFO_Y + 68, buf, UI_ACCENT, UI_CARD_BG);
}

static void ui_update_footer(uint32_t sec)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "Uptime %02lum%02lus",
             (unsigned long)(sec / 60), (unsigned long)(sec % 60));
    int w = (int)strlen(buf) * 8;
    int x = LCD_WIDTH - w - 8;
    lcd_fill_rect(x - 2, FOOT_Y + 6, w + 4, 16, UI_TITLE_BG);
    lcd_draw_string(x, FOOT_Y + 6, buf, UI_ACCENT, UI_TITLE_BG);
}

void app_main(void)
{
    ESP_LOGI(TAG, "===== SmartElderCare boot =====");

    lcd_init();
    lcd_set_backlight(90);
    ui_draw_static();

    /* 校准提示 */
    ui_draw_card(6, STATUS_Y, LCD_WIDTH - 12, STATUS_H, UI_CARD_BG, UI_CARD_BORDER);
    lcd_draw_string_2x(24, STATUS_Y + 12, "CALIBRATING", UI_ACCENT, UI_CARD_BG);
    lcd_draw_string(28, STATUS_Y + 56, "Keep sensor DRY please...", COLOR_WHITE, UI_CARD_BG);

    water_sensor_init();

    water_data_t data = {0};
    water_state_t last_state = (water_state_t)0xFF;
    uint8_t last_pct = 255;
    uint32_t last_sec = 0xFFFFFFFF;
    int64_t t0 = esp_timer_get_time();

    while (1) {
        water_sensor_read(&data);

        if (data.state != last_state) {
            ui_update_status(data.state);
            last_state = data.state;
        }

        if (data.percent != last_pct) {
            ui_update_level(data.percent, data.state);
            last_pct = data.percent;
        }

        ui_update_info(&data);

        uint32_t sec = (uint32_t)((esp_timer_get_time() - t0) / 1000000ULL);
        if (sec != last_sec) {
            ui_update_footer(sec);
            last_sec = sec;
        }

        ESP_LOGI(TAG, "adc=%d base=%d delta=%d pct=%d%% d0=%d state=%s",
                 data.raw_adc, data.baseline, data.delta, data.percent,
                 data.d0_alert, water_state_str(data.state));

        vTaskDelay(pdMS_TO_TICKS(300));
    }
}