# -*- coding: utf-8 -*-
"""补齐 main.c 尾部被截断的内容"""
import io

TAIL = '''idle_sec / 60;
        uint32_t s = idle_sec % 60;
        uint16_t color = UI_LABEL;
        if (idle_sec >= 1800) color = UI_ALARM;
        else if (idle_sec >= 600) color = UI_WARN;
        snprintf(line, sizeof(line), "%lum%02lus",
                 (unsigned long)m, (unsigned long)s);
        lcd_draw_utf8(x_val+8, INFO_Y+8+row_h*3, line, color, bg);
    }
}

/* 底栏: 运行时长 */
static void ui_draw_foot(uint32_t uptime_sec)
{
    char line[48];
    uint32_t h = uptime_sec / 3600;
    uint32_t m = (uptime_sec / 60) % 60;
    uint32_t s = uptime_sec % 60;
    snprintf(line, sizeof(line), "\xe8\xbf\x90\xe8\xa1\x8c %02lu:%02lu:%02lu",
             (unsigned long)h, (unsigned long)m, (unsigned long)s);
    lcd_fill_rect(0, FOOT_Y, LCD_WIDTH, FOOT_H, UI_TITLE_BG);
    uint16_t w = lcd_measure_utf8(line);
    uint16_t x = (LCD_WIDTH - w) / 2;
    lcd_draw_utf8(x, FOOT_Y+5, line, UI_TITLE_FG, UI_TITLE_BG);
}

/* ==================== 主循环 ==================== */
void app_main(void)
{
    ESP_LOGI(TAG, "===== SmartElderCare Gateway Boot =====");

    lcd_init();
    lcd_set_backlight(80);
    ui_draw_static();

    /* 启动画面 */
    lcd_fill_rect(5, STATUS_Y+1, LCD_WIDTH-10, STATUS_H-2, UI_OK);
    lcd_draw_utf8_2x((LCD_WIDTH - lcd_measure_utf8_2x("\xe7\xb3\xbb\xe7\xbb\x9f\xe5\x90\xaf\xe5\x8a\xa8")) / 2,
                     STATUS_Y+8, "\xe7\xb3\xbb\xe7\xbb\x9f\xe5\x90\xaf\xe5\x8a\xa8", COLOR_WHITE, UI_OK);
    lcd_draw_utf8((LCD_WIDTH - lcd_measure_utf8("\xe6\xad\xa3\xe5\x9c\xa8\xe5\x88\x9d\xe5\xa7\x8b\xe5\x8c\x96\xe4\xbc\xa0\xe6\x84\x9f\xe5\x99\xa8")) / 2,
                  STATUS_Y+50, "\xe6\xad\xa3\xe5\x9c\xa8\xe5\x88\x9d\xe5\xa7\x8b\xe5\x8c\x96\xe4\xbc\xa0\xe6\x84\x9f\xe5\x99\xa8", COLOR_WHITE, UI_OK);

    water_sensor_init();
    radar_init();
    air_sensor_init();

    ui_draw_static();
    s_cache.valid = false;
    ui_draw_person(false, true);

    uint32_t last_person_ms = esp_timer_get_time() / 1000;
    uint32_t boot_ms = esp_timer_get_time() / 1000;

    water_data_t wd;
    radar_data_t rd;
    air_data_t   ad;

    while (1) {
        water_sensor_read(&wd);
        radar_read(&rd);
        air_sensor_read(&ad);

        uint32_t now_ms = esp_timer_get_time() / 1000;
        if (rd.presence) {
            last_person_ms = now_ms;
        }
        uint32_t idle_sec = (now_ms - last_person_ms) / 1000;
        uint32_t uptime_sec = (now_ms - boot_ms) / 1000;

        combo_status_t st = combine_status(wd.state, ad.state, ad.warmed_up);

        ui_draw_person(rd.presence, false);
        ui_draw_status_card(&st, false);
        ui_draw_water_bar(wd.percent, wd.state, false);
        ui_draw_info_card(wd.state, wd.percent,
                          ad.state, ad.raw_adc, ad.warmed_up,
                          rd.presence, idle_sec);
        ui_draw_foot(uptime_sec);
        s_cache.valid = true;

        ESP_LOGI(TAG, "W=%s(%d%%,d%d) A=%s(adc%d,d%d,warm%d) P=%d idle=%lus",
                 water_state_str(wd.state), wd.percent, wd.delta,
                 air_state_str(ad.state), ad.raw_adc, ad.delta, ad.warmed_up,
                 rd.presence, (unsigned long)idle_sec);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
'''

# 读取原文件, 找到截断点, 拼接尾部
path = "main/main.c"
with open(path, "rb") as f:
    data = f.read().decode("utf-8")

marker = "uint32_t m = idle_s"
idx = data.rfind(marker)
if idx < 0:
    raise SystemExit("marker not found, file already complete?")

# 保留到 "idle_s" 结束前 (含 "idle_s"), 后面补 "ec / 60..." (即 TAIL 已经从 'idle_sec/60...' 开始, 我们把 "idle_s" 也去掉重新拼)
# 简单做法: 去掉 "uint32_t m = idle_s" 及之后所有内容, 用完整段替换
keep = data[:idx]
full = keep + "uint32_t m = " + TAIL

with open(path, "wb") as f:
    f.write(full.encode("utf-8"))

print("OK, new length =", len(full))