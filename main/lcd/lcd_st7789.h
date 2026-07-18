/*
 * ATK-MD0240 V1.2 (240x320 SPI TFT, ST7789 driver) 驱动头文件
 * 目标板: ESP32-S3-DevKitC-1 v1.1
 */
#ifndef LCD_ST7789_H
#define LCD_ST7789_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 引脚配置 ==================== */
/* 正点原子 ATK-MD0240 SPI TFT 屏 (ST7789, 240x320)
 * 目标板: ESP32-S3-DevKitC-1 v1.1
 *
 * 屏幕丝印 8pin 顺序: WR SCK PWR GND RST SDA CS 3V3
 *
 * 屏丝印 -> 代码定义 -> ESP32-S3 GPIO  (用户实际接线)
 *   3V3   -> VCC       -> 3.3V
 *   GND   -> GND       -> GND
 *   SCK   -> LCD_PIN_SCLK -> GPIO12  (SPI 时钟)
 *   SDA   -> LCD_PIN_MOSI -> GPIO11  (SPI 数据 MOSI)
 *   CS    -> LCD_PIN_CS   -> GPIO10  (片选)
 *   WR    -> LCD_PIN_DC   -> GPIO13  (⚠️正点原子把DC标成WR！)
 *   RST   -> LCD_PIN_RST  -> GPIO14  (复位)
 *   PWR   -> LCD_PIN_BL   -> GPIO2   (背光, 也可直接接 3.3V 常亮)
 */
#define LCD_PIN_SCLK    12   // SCK - SPI 时钟
#define LCD_PIN_MOSI    11   // SDA - SPI 数据 (MOSI)
#define LCD_PIN_CS      10   // CS  - 片选
#define LCD_PIN_DC      13   // WR  - 数据/命令 (正点原子丝印叫WR，其实就是DC)
#define LCD_PIN_RST     14   // RST - 复位
#define LCD_PIN_BL       2   // PWR - 背光 (GPIO2 PWM 控制)

/* ==================== 屏幕参数 ==================== */
#define LCD_WIDTH       240
#define LCD_HEIGHT      320

/* ==================== 常用颜色 (RGB565) ==================== */
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F
#define COLOR_GRAY      0x8410
#define COLOR_ORANGE    0xFD20

/* ==================== 对外接口 ==================== */
/* 初始化 LCD (含 SPI + 背光 PWM) */
void lcd_init(void);

/* 设置背光亮度 0~100 */
void lcd_set_backlight(uint8_t percent);

/* 全屏填充单色 */
void lcd_fill_screen(uint16_t color);

/* 矩形区域填充 */
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/* 画点 */
void lcd_draw_pixel(uint16_t x, uint16_t y, uint16_t color);

/* 显示 8x16 ASCII 字符 */
void lcd_draw_char(uint16_t x, uint16_t y, char ch, uint16_t fg, uint16_t bg);

/* 显示 ASCII 字符串 */
void lcd_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg);

/* 显示 16x32 ASCII 字符 (2倍放大, 更清晰醒目) */
void lcd_draw_char_2x(uint16_t x, uint16_t y, char ch, uint16_t fg, uint16_t bg);

/* 显示 16x32 ASCII 字符串 (2倍放大) */
void lcd_draw_string_2x(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg);

/* 计算 16x32 字符串宽度 (像素) */
uint16_t lcd_measure_string_2x(const char *str);

/* ==================== 中文 16x16 显示 (UTF-8 输入) ==================== */
/* 显示一个 16x16 中文字符 (传入 UTF-8 3 字节) */
void lcd_draw_char_cn(uint16_t x, uint16_t y, const uint8_t *utf8_3bytes,
                      uint16_t fg, uint16_t bg);

/* 显示 UTF-8 混排字符串:
 *   - ASCII 用 8x16
 *   - 中文 用 16x16
 * 未收录的字会显示 '?' 方块。
 */
void lcd_draw_utf8(uint16_t x, uint16_t y, const char *utf8_str,
                   uint16_t fg, uint16_t bg);

/* 2x 放大 UTF-8 混排显示:
 *   - ASCII 变为 16x32
 *   - 中文 变为 32x32
 */
void lcd_draw_utf8_2x(uint16_t x, uint16_t y, const char *utf8_str,
                      uint16_t fg, uint16_t bg);

/* 测量 UTF-8 字符串的像素宽度 (1x) */
uint16_t lcd_measure_utf8(const char *utf8_str);

/* 测量 UTF-8 字符串的像素宽度 (2x) */
uint16_t lcd_measure_utf8_2x(const char *utf8_str);

/* 画水平进度条 (含外边框)
 *   x,y,w,h   : 外接矩形
 *   percent   : 0~100 填充百分比
 *   fg        : 已填充部分颜色
 *   bg        : 未填充部分颜色
 *   border    : 边框颜色
 */
void lcd_draw_progress(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint8_t percent, uint16_t fg, uint16_t bg, uint16_t border);

#ifdef __cplusplus
}
#endif

#endif // LCD_ST7789_H