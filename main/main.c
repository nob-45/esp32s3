/* SmartElderCare - 中文UI版, 行级差异刷新 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lcd_st7789.h"
#include "water_sensor.h"
#include "radar_ld2410.h"
#include "air_mq135.h"
#include "camera_ov2640.h"
#include "camera_diag.h"
#include "esp_camera.h"
#include "wifi_mjpeg.h"

/* ============================================================
 * 诊断开关: 想跑 UART 透传诊断时, 把下面这行改为 1, 重新烧录.
 * 诊断模式会跳过 LCD 和所有传感器初始化, 只跑 UART 读字节并打印 HEX.
 * ============================================================ */
#define RUN_UART_PROBE_ONLY   0

/* ============================================================
 * 摄像头硬件诊断开关: 摄像头初始化返回 0x105(ESP_ERR_NOT_FOUND) 时,
 * 把下面这行改为 1, 重新烧录, 串口会输出 SCCB 扫描 / PID 读取结果,
 * 用来定位接线问题. 调试完成后改回 0.
 * ============================================================ */
#define RUN_CAMERA_DIAG_ONLY  0

/* ============================================================
 * 摄像头 + Wi-Fi 图传开关
 *   1 = 启用摄像头采集 + Wi-Fi MJPEG 图传 + 上位机 YOLO 跌倒识别
 *   0 = 仅本地传感器 UI (硬件没插摄像头时用此模式)
 * ============================================================ */
#define ENABLE_CAMERA_STREAM  1

#if RUN_UART_PROBE_ONLY
extern void uart_probe_forever(void);
#endif

static const char *TAG = "main";

#define UI_BG           0x0000
#define UI_TITLE_BG     0x02B5
#define UI_CARD_BG      0x2124
#define UI_CARD_BORDER  0x4A69
#define UI_LABEL        0xBDF7
#define UI_OK           0x2FE4
#define UI_WARN         0xFD20
#define UI_ALARM        0xF9E7
#define UI_IDLE         0x07FF
#define UI_PERSON_ON    0x2FE4

#define TITLE_H     40
#define STATUS_Y    44
#define STATUS_H    82
#define BAR_Y       130
#define BAR_H       26
#define INFO_Y      160
#define INFO_H      130
#define FOOT_Y      294
#define FOOT_H      26
#define INFO_X_LABEL   12
#define INFO_X_VALUE   118

typedef struct { uint8_t level; const char *big; const char *sub; } combo_status_t;

static combo_status_t combine_status(water_state_t ws, air_state_t as, bool warmed) {
    combo_status_t r = { 0, "全部正常", "地面干燥 空气洁净" };
    if (ws==WATER_STATE_FLOOD){r.level=2;r.big="浴室积水";r.sub="危险 地面积水警报";return r;}
    if (as==AIR_STATE_BAD)    {r.level=2;r.big="空气异常";r.sub="危险 空气质量污染";return r;}
    if (ws==WATER_STATE_DAMP && as==AIR_STATE_ODOR){r.level=1;r.big="地面潮湿";r.sub="潮湿并伴有异味";return r;}
    if (ws==WATER_STATE_DAMP) {r.level=1;r.big="地面潮湿";r.sub="警告 请注意地面";return r;}
    if (as==AIR_STATE_ODOR)   {r.level=1;r.big="空气异味";r.sub="警告 空气质量异常";return r;}
    if (!warmed)              {r.level=0;r.big="预热中  ";r.sub="传感器校准 请稍候";return r;}
    return r;
}

static uint16_t level_color(uint8_t lv){return (lv==2)?UI_ALARM:(lv==1)?UI_WARN:UI_OK;}
static uint16_t water_bar_color(water_state_t s){return (s==WATER_STATE_FLOOD)?UI_ALARM:(s==WATER_STATE_DAMP)?UI_WARN:UI_OK;}
static const char *water_state_cn(water_state_t s){return (s==WATER_STATE_FLOOD)?"积水":(s==WATER_STATE_DAMP)?"潮湿":"干燥";}
static const char *air_state_cn(air_state_t s, bool w){if(!w)return "预热";return (s==AIR_STATE_BAD)?"污染":(s==AIR_STATE_ODOR)?"异味":"洁净";}
static uint16_t water_color(water_state_t s){return (s==WATER_STATE_FLOOD)?UI_ALARM:(s==WATER_STATE_DAMP)?UI_WARN:UI_OK;}
static uint16_t air_color(air_state_t s, bool w){if(!w)return UI_IDLE;return (s==AIR_STATE_BAD)?UI_ALARM:(s==AIR_STATE_ODOR)?UI_WARN:UI_OK;}

static void ui_card_border(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t bg,uint16_t bd){
    lcd_fill_rect(x,y,w,h,bg);
    lcd_fill_rect(x,y,w,1,bd);
    lcd_fill_rect(x,y+h-1,w,1,bd);
    lcd_fill_rect(x,y,1,h,bd);
    lcd_fill_rect(x+w-1,y,1,h,bd);
}

typedef struct {
    uint8_t combo_level;
    char combo_big[32], combo_sub[48];
    uint8_t water_pct;
    water_state_t water_st;
    water_state_t info_ws;
    uint8_t info_wpct;
    air_state_t info_as;
    int info_araw;
    bool info_warm;
    bool info_has_person;
    bool info_raw_level;
    uint32_t info_idle_sec;
    bool person;
    uint32_t foot_min;
    bool fall_active;
    bool valid;
} ui_cache_t;
static ui_cache_t s_cache = { .valid=false };

static void ui_draw_static(void){
    lcd_fill_screen(UI_BG);
    lcd_fill_rect(0,0,LCD_WIDTH,TITLE_H,UI_TITLE_BG);
    lcd_draw_utf8_2x(8,4,"智慧养老",COLOR_WHITE,UI_TITLE_BG);
    ui_card_border(4,STATUS_Y,LCD_WIDTH-8,STATUS_H,UI_CARD_BG,UI_CARD_BORDER);
    ui_card_border(4,BAR_Y,LCD_WIDTH-8,BAR_H,UI_CARD_BG,UI_CARD_BORDER);
    ui_card_border(4,INFO_Y,LCD_WIDTH-8,INFO_H,UI_CARD_BG,UI_CARD_BORDER);
    lcd_fill_rect(0,FOOT_Y,LCD_WIDTH,FOOT_H,UI_TITLE_BG);
    lcd_draw_utf8(INFO_X_LABEL,INFO_Y+8   ,"水位:",UI_LABEL,UI_CARD_BG);
    lcd_draw_utf8(INFO_X_LABEL,INFO_Y+8+30,"空气:",UI_LABEL,UI_CARD_BG);
    lcd_draw_utf8(INFO_X_LABEL,INFO_Y+8+60,"人体:",UI_LABEL,UI_CARD_BG);
    lcd_draw_utf8(INFO_X_LABEL,INFO_Y+8+90,"静止:",UI_LABEL,UI_CARD_BG);
}

static void ui_draw_person(bool has, bool force){
    if(!force && s_cache.valid && s_cache.person==has)return;
    s_cache.person=has;
    const char *t=has?"有人":"无人";
    uint16_t w=lcd_measure_utf8_2x(t);
    uint16_t cx=LCD_WIDTH-8-64-4;
    lcd_fill_rect(cx,4,LCD_WIDTH-cx,32,UI_TITLE_BG);
    lcd_draw_utf8_2x(LCD_WIDTH-8-w,4,t,has?UI_PERSON_ON:UI_IDLE,UI_TITLE_BG);
}

static void ui_draw_status_card(const combo_status_t *st, bool force){
    if(!force && s_cache.valid
       && s_cache.combo_level==st->level
       && strcmp(s_cache.combo_big,st->big)==0
       && strcmp(s_cache.combo_sub,st->sub)==0) return;
    s_cache.combo_level=st->level;
    strncpy(s_cache.combo_big,st->big,sizeof(s_cache.combo_big)-1);
    s_cache.combo_big[sizeof(s_cache.combo_big)-1]=0;
    strncpy(s_cache.combo_sub,st->sub,sizeof(s_cache.combo_sub)-1);
    s_cache.combo_sub[sizeof(s_cache.combo_sub)-1]=0;
    uint16_t bg=level_color(st->level);
    lcd_fill_rect(5,STATUS_Y+1,LCD_WIDTH-10,STATUS_H-2,bg);
    uint16_t bw=lcd_measure_utf8_2x(st->big);
    lcd_draw_utf8_2x((LCD_WIDTH-bw)/2,STATUS_Y+8,st->big,COLOR_WHITE,bg);
    uint16_t sw=lcd_measure_utf8(st->sub);
    lcd_draw_utf8((LCD_WIDTH-sw)/2,STATUS_Y+50,st->sub,COLOR_WHITE,bg);
}

static void ui_draw_water_bar(uint8_t pct, water_state_t st, bool force){
    if(!force && s_cache.valid && s_cache.water_pct==pct && s_cache.water_st==st)return;
    s_cache.water_pct=pct; s_cache.water_st=st;
    lcd_draw_progress(10,BAR_Y+4,LCD_WIDTH-20,BAR_H-8,pct,water_bar_color(st),0x2965,UI_CARD_BORDER);
}

static void ui_info_value(uint16_t y, const char *val, uint16_t color){
    lcd_fill_rect(INFO_X_VALUE,y,LCD_WIDTH-8-INFO_X_VALUE,16,UI_CARD_BG);
    lcd_draw_utf8(INFO_X_VALUE,y,val,color,UI_CARD_BG);
}

static void ui_draw_info(water_state_t ws, uint8_t wpct, air_state_t as, int araw,
                         bool warm, bool has_person, uint32_t idle_sec,
                         bool raw_level, bool force){
    if(force || !s_cache.valid || s_cache.info_ws!=ws || s_cache.info_wpct!=wpct){
        char buf[32]; snprintf(buf,sizeof(buf),"%s  %u%%",water_state_cn(ws),wpct);
        ui_info_value(INFO_Y+8, buf, water_color(ws));
        s_cache.info_ws=ws; s_cache.info_wpct=wpct;
    }
    if(force || !s_cache.valid || s_cache.info_as!=as || s_cache.info_araw!=araw || s_cache.info_warm!=warm){
        char buf[32];
        if(!warm) snprintf(buf,sizeof(buf),"预热 %d",araw);
        else      snprintf(buf,sizeof(buf),"%s  %d",air_state_cn(as,warm),araw);
        ui_info_value(INFO_Y+8+30, buf, air_color(as,warm));
        s_cache.info_as=as; s_cache.info_araw=araw; s_cache.info_warm=warm;
    }
    if(force || !s_cache.valid || s_cache.info_has_person!=has_person || s_cache.info_raw_level!=raw_level){
        char buf[24];
        snprintf(buf,sizeof(buf),"%s L=%d", has_person?"有人":"无人", raw_level?1:0);
        ui_info_value(INFO_Y+8+60, buf,
                      has_person?UI_PERSON_ON:(raw_level?UI_WARN:UI_IDLE));
        s_cache.info_has_person=has_person;
        s_cache.info_raw_level=raw_level;
    }
    if(force || !s_cache.valid || s_cache.info_idle_sec/5 != idle_sec/5){
        char buf[32]; snprintf(buf,sizeof(buf),"%lus", (unsigned long)idle_sec);
        ui_info_value(INFO_Y+8+90, buf, UI_LABEL);
        s_cache.info_idle_sec=idle_sec;
    }
}

static void ui_draw_foot(uint32_t up_sec, bool force){
    uint32_t m = up_sec/60;
    if(!force && s_cache.valid && s_cache.foot_min==m) return;
    s_cache.foot_min = m;
    char buf[48];
    snprintf(buf,sizeof(buf),"运行 %luh %02lum",
             (unsigned long)(m/60), (unsigned long)(m%60));
    lcd_fill_rect(0,FOOT_Y,LCD_WIDTH,FOOT_H,UI_TITLE_BG);
    uint16_t w = lcd_measure_utf8(buf);
    lcd_draw_utf8((LCD_WIDTH-w)/2, FOOT_Y+5, buf, COLOR_WHITE, UI_TITLE_BG);
}

void app_main(void)
{
    ESP_LOGI(TAG,"SmartElderCare 启动");

#if RUN_UART_PROBE_ONLY
    /* 诊断模式: 只跑 UART 探测, 不初始化 LCD 和传感器 */
    ESP_LOGW(TAG, "*** UART PROBE MODE (RUN_UART_PROBE_ONLY=1) ***");
    ESP_LOGW(TAG, "*** LCD & sensors are DISABLED in this mode ***");
    uart_probe_forever();   /* 死循环, 不会返回 */
    return;
#endif

#if RUN_CAMERA_DIAG_ONLY
    /* 摄像头硬件诊断模式: 只做 SCCB 扫描 + PID 读取 */
    ESP_LOGW(TAG, "*** CAMERA DIAG MODE (RUN_CAMERA_DIAG_ONLY=1) ***");
    ESP_LOGW(TAG, "*** normal camera init is SKIPPED ***");
    camera_diag_run();
    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    return;
#endif

    lcd_init();
    lcd_fill_screen(UI_BG);

    /* 先画静态骨架, 让用户在预热期间也能看到界面 */
    ui_draw_static();
    s_cache.valid = false;

    /* 启动状态卡片: 显示 "系统启动 / 正在初始化传感器" */
    combo_status_t boot = { .level = 0,
                            .big = "系统启动",
                            .sub = "正在初始化传感器" };
    ui_draw_status_card(&boot, true);
    ui_draw_person(false, true);
    ui_draw_water_bar(0, WATER_STATE_DRY, true);
    ui_draw_info(WATER_STATE_DRY, 0, AIR_STATE_CLEAN, 0,
                 false, false, 0, false, true);
    ui_draw_foot(0, true);

    water_sensor_init();
    radar_init();
    air_sensor_init();      /* 会阻塞 ~60s 预热, 期间屏幕保持骨架显示 */

#if ENABLE_CAMERA_STREAM
    /* 摄像头 + Wi-Fi 图传：失败不影响本地监护功能
     *
     * 2026-07-17 关键修正：
     *   移除 camera_diag_probe_signals() 调用！
     *   原诊断代码会把 PCLK/VSYNC/HREF 引脚从 DVP 功能切成 GPIO input，
     *   导致 GDMA disconnect、GPIO matrix 路由被污染，后续 re-init 虽然
     *   返回 OK，但实际抓帧全部超时 (cam_hal: Failed to get the frame on time!)。
     *   实际证据: SCCB 能读到 PID=0x2642 说明摄像头本体和板载晶振都是好的，
     *   问题只出在诊断代码本身。 */
    ESP_LOGI(TAG, "启动摄像头与 Wi-Fi 图传 ...");
    if (camera_ov2640_init() == ESP_OK) {
        wifi_mjpeg_wifi_start();
        wifi_mjpeg_server_start();
        ESP_LOGI(TAG, "视频流已就绪: http://<板子IP>/stream");
    } else {
        ESP_LOGW(TAG, "摄像头初始化失败, 跳过 Wi-Fi 图传, 仅本地监护");
    }
#endif

    /* 预热完成, 强制刷新一次真实数据 */
    s_cache.valid = false;

    uint64_t t0 = esp_timer_get_time();
    uint64_t last_person_change_us = t0;
    bool     prev_person = false;
    uint64_t fall_alarm_end_us = 0;   /* 跌倒报警显示到期时间 (0 = 未报警) */

    while(1) {
        water_data_t wd;
        air_data_t   ad;
        radar_data_t rd;
        water_sensor_read(&wd);
        air_sensor_read(&ad);
        radar_read(&rd);

        uint64_t now = esp_timer_get_time();
        if(rd.presence != prev_person){
            last_person_change_us = now;
            prev_person = rd.presence;
        }
        uint32_t idle = rd.presence ? 0
                        : (uint32_t)((now - last_person_change_us)/1000000ULL);

        uint32_t up_sec = (uint32_t)((now - t0) / 1000000ULL);

        /* 检查上位机 YOLO 是否触发了跌倒报警 (POST /alarm) */
#if ENABLE_CAMERA_STREAM
        if (wifi_mjpeg_pop_fall_alarm()) {
            ESP_LOGW(TAG, "*** 上位机识别到跌倒, 触发本地声光报警 ***");
            fall_alarm_end_us = now + 15ULL * 1000000ULL; /* 报警持续 15s */
        }
#endif
        bool fall_active = (fall_alarm_end_us != 0) && (now < fall_alarm_end_us);
        if (fall_alarm_end_us != 0 && now >= fall_alarm_end_us) {
            fall_alarm_end_us = 0;
        }

        combo_status_t cs;
        if (fall_active) {
            /* 跌倒报警是最高优先级, 覆盖其它状态 */
            cs.level = 2;
            cs.big   = "老人跌倒";
            cs.sub   = "AI 识别 请立即查看";
        } else {
            cs = combine_status(wd.state, ad.state, ad.warmed_up);
        }

        ui_draw_person(rd.presence, false);
        ui_draw_status_card(&cs, false);
        ui_draw_water_bar(wd.percent, wd.state, false);
        ui_draw_info(wd.state, wd.percent, ad.state, ad.raw_adc,
                     ad.warmed_up, rd.presence, idle, rd.raw_level, false);
        ui_draw_foot(up_sec, false);

        s_cache.fall_active = fall_active;
        s_cache.valid = true;
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}
