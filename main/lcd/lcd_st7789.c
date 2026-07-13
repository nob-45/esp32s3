/*
 * ATK-MD0240 V1.2 (ST7789 240x320) SPI 驱动实现
 * 使用 ESP-IDF SPI Master + LEDC 背光
 */
#include "lcd_st7789.h"
#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LCD";

/* 使用 SPI2 (HSPI) */
#define LCD_SPI_HOST    SPI2_HOST
#define LCD_SPI_CLOCK   (10 * 1000 * 1000)  // 10MHz (飞线降频，稳定优先)

/* 背光 LEDC */
#define BL_LEDC_TIMER   LEDC_TIMER_0
#define BL_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define BL_LEDC_CHANNEL LEDC_CHANNEL_0
#define BL_LEDC_DUTY_RES LEDC_TIMER_10_BIT
#define BL_LEDC_FREQ    5000

static spi_device_handle_t s_spi = NULL;

/* ==================== 引脚 & 传输封装 ==================== */
static inline void lcd_dc_cmd(void)  { gpio_set_level(LCD_PIN_DC, 0); }
static inline void lcd_dc_data(void) { gpio_set_level(LCD_PIN_DC, 1); }

/* 发送一个命令字节 */
static void lcd_write_cmd(uint8_t cmd)
{
    lcd_dc_cmd();
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
}

/* 发送数据字节序列 */
static void lcd_write_data(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    lcd_dc_data();
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
}

static void lcd_write_data8(uint8_t d)
{
    lcd_write_data(&d, 1);
}

/* ==================== 硬件初始化 ==================== */
static void lcd_gpio_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LCD_PIN_DC) | (1ULL << LCD_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    /* 硬件复位（加大延时，保证 ST7789 完全复位） */
    gpio_set_level(LCD_PIN_DC, 1);   // DC 默认拉高
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
}

static void lcd_backlight_init(void)
{
#if LCD_PIN_BL >= 0
    ledc_timer_config_t timer = {
        .speed_mode = BL_LEDC_MODE,
        .duty_resolution = BL_LEDC_DUTY_RES,
        .timer_num = BL_LEDC_TIMER,
        .freq_hz = BL_LEDC_FREQ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num = LCD_PIN_BL,
        .speed_mode = BL_LEDC_MODE,
        .channel = BL_LEDC_CHANNEL,
        .timer_sel = BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
#else
    ESP_LOGI(TAG, "背光引脚 = -1, 直接接3V3常亮, 跳过 LEDC 初始化");
#endif
}

void lcd_set_backlight(uint8_t percent)
{
#if LCD_PIN_BL >= 0
    if (percent > 100) percent = 100;
    uint32_t duty = (1023 * percent) / 100;
    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
#else
    (void)percent;  // 背光直接接3V3常亮, 不受控
#endif
}

static void lcd_spi_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2 + 16,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = LCD_SPI_CLOCK,
        .mode = 0,
        .spics_io_num = LCD_PIN_CS,
        .queue_size = 7,
        .flags = SPI_DEVICE_NO_DUMMY,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &dev, &s_spi));
}

/* ==================== ST7789 初始化序列 ==================== */
static void lcd_st7789_init_seq(void)
{
    lcd_write_cmd(0x11); // Sleep out
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_write_cmd(0x36); // MADCTL: 内存访问方向
    lcd_write_data8(0x00); // 正常方向, RGB

    lcd_write_cmd(0x3A); // 像素格式
    lcd_write_data8(0x05); // 16bit/pixel RGB565

    lcd_write_cmd(0xB2); // Porch 设置
    { uint8_t d[] = {0x0C, 0x0C, 0x00, 0x33, 0x33}; lcd_write_data(d, 5); }

    lcd_write_cmd(0xB7); // Gate 控制
    lcd_write_data8(0x35);

    lcd_write_cmd(0xBB); // VCOM
    lcd_write_data8(0x19);

    lcd_write_cmd(0xC0); // LCM 控制
    lcd_write_data8(0x2C);

    lcd_write_cmd(0xC2); // VDV VRH 使能
    lcd_write_data8(0x01);

    lcd_write_cmd(0xC3); // VRH
    lcd_write_data8(0x12);

    lcd_write_cmd(0xC4); // VDV
    lcd_write_data8(0x20);

    lcd_write_cmd(0xC6); // 帧率
    lcd_write_data8(0x0F);

    lcd_write_cmd(0xD0); // 电源控制
    { uint8_t d[] = {0xA4, 0xA1}; lcd_write_data(d, 2); }

    lcd_write_cmd(0xE0); // 正 gamma
    { uint8_t d[] = {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54,
                     0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23}; lcd_write_data(d, 14); }

    lcd_write_cmd(0xE1); // 负 gamma
    { uint8_t d[] = {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44,
                     0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23}; lcd_write_data(d, 14); }

    lcd_write_cmd(0x21); // 反显 (ST7789 常规操作)
    lcd_write_cmd(0x29); // 显示开启
    vTaskDelay(pdMS_TO_TICKS(20));
}

/* 设置绘图窗口 */
static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_write_cmd(0x2A); // 列地址
    uint8_t xd[] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    lcd_write_data(xd, 4);

    lcd_write_cmd(0x2B); // 行地址
    uint8_t yd[] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};
    lcd_write_data(yd, 4);

    lcd_write_cmd(0x2C); // 开始写 GRAM
}

/* ==================== 对外 API ==================== */
void lcd_init(void)
{
    ESP_LOGI(TAG, "LCD 初始化开始 (SPI=%d MHz)...", LCD_SPI_CLOCK / 1000000);
    lcd_gpio_init();
    lcd_backlight_init();
    lcd_spi_init();
    lcd_st7789_init_seq();

    /* 诊断用：先刷红色 -> 绿色 -> 蓝色，每色 500ms
     * 如果 SPI 通信正常，屏幕会显示三色循环，然后进入黑屏
     * 如果全程黑屏或花屏，说明 SPI 时序/接线有问题 */
    ESP_LOGI(TAG, "LCD 自检: RED");
    lcd_fill_screen(COLOR_RED);
    lcd_set_backlight(80);
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "LCD 自检: GREEN");
    lcd_fill_screen(COLOR_GREEN);
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "LCD 自检: BLUE");
    lcd_fill_screen(COLOR_BLUE);
    vTaskDelay(pdMS_TO_TICKS(500));

    lcd_fill_screen(COLOR_BLACK);
    ESP_LOGI(TAG, "LCD 初始化完成 (%dx%d)", LCD_WIDTH, LCD_HEIGHT);
}

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;

    lcd_set_window(x, y, x + w - 1, y + h - 1);

    /* 分块 DMA 传输，避免单次超大 buffer */
    #define CHUNK_PIXELS 512
    static uint16_t buf[CHUNK_PIXELS];
    uint16_t swapped = (color >> 8) | (color << 8); // SPI 大端
    for (int i = 0; i < CHUNK_PIXELS; i++) buf[i] = swapped;

    uint32_t total = (uint32_t)w * h;
    lcd_dc_data();
    while (total > 0) {
        uint32_t n = total > CHUNK_PIXELS ? CHUNK_PIXELS : total;
        spi_transaction_t t = {
            .length = n * 16,
            .tx_buffer = buf,
        };
        ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
        total -= n;
    }
}

void lcd_fill_screen(uint16_t color)
{
    lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

void lcd_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    lcd_set_window(x, y, x, y);
    uint8_t d[2] = { color >> 8, color & 0xFF };
    lcd_write_data(d, 2);
}

/* ==================== 字符显示 ==================== */
extern const uint8_t ascii_8x16[][16]; // 定义在 lcd_font.c

void lcd_draw_char(uint16_t x, uint16_t y, char ch, uint16_t fg, uint16_t bg)
{
    if (ch < 0x20 || ch > 0x7E) ch = '?';
    const uint8_t *font = ascii_8x16[ch - 0x20];

    static uint8_t buf[8 * 16 * 2];
    uint8_t fg_h = fg >> 8, fg_l = fg & 0xFF;
    uint8_t bg_h = bg >> 8, bg_l = bg & 0xFF;

    int idx = 0;
    for (int row = 0; row < 16; row++) {
        uint8_t line = font[row];
        for (int col = 0; col < 8; col++) {
            if (line & (0x80 >> col)) {
                buf[idx++] = fg_h;
                buf[idx++] = fg_l;
            } else {
                buf[idx++] = bg_h;
                buf[idx++] = bg_l;
            }
        }
    }

    lcd_set_window(x, y, x + 7, y + 15);
    lcd_write_data(buf, sizeof(buf));
}

void lcd_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg)
{
    while (*str) {
        if (x + 8 > LCD_WIDTH) {
            x = 0;
            y += 16;
            if (y + 16 > LCD_HEIGHT) break;
        }
        lcd_draw_char(x, y, *str, fg, bg);
        x += 8;
        str++;
    }
}

/* ==================== 2x 放大字体 ==================== */
/* 16x32 像素字符：每个 8x16 像素点扩为 2x2 像素块 */
void lcd_draw_char_2x(uint16_t x, uint16_t y, char ch, uint16_t fg, uint16_t bg)
{
    if (ch < 0x20 || ch > 0x7E) ch = '?';
    const uint8_t *font = ascii_8x16[ch - 0x20];

    /* 16*32 = 512 像素 * 2 字节 = 1024 字节 */
    static uint8_t buf[16 * 32 * 2];
    uint8_t fg_h = fg >> 8, fg_l = fg & 0xFF;
    uint8_t bg_h = bg >> 8, bg_l = bg & 0xFF;

    int idx = 0;
    for (int row = 0; row < 16; row++) {
        uint8_t line = font[row];
        /* 每行输出两次(高度2倍) */
        for (int dup = 0; dup < 2; dup++) {
            for (int col = 0; col < 8; col++) {
                uint8_t on = line & (0x80 >> col);
                /* 每列输出两次(宽度2倍) */
                for (int d2 = 0; d2 < 2; d2++) {
                    if (on) {
                        buf[idx++] = fg_h;
                        buf[idx++] = fg_l;
                    } else {
                        buf[idx++] = bg_h;
                        buf[idx++] = bg_l;
                    }
                }
            }
        }
    }

    lcd_set_window(x, y, x + 15, y + 31);
    lcd_write_data(buf, sizeof(buf));
}

void lcd_draw_string_2x(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg)
{
    while (*str) {
        if (x + 16 > LCD_WIDTH) {
            x = 0;
            y += 32;
            if (y + 32 > LCD_HEIGHT) break;
        }
        lcd_draw_char_2x(x, y, *str, fg, bg);
        x += 16;
        str++;
    }
}

uint16_t lcd_measure_string_2x(const char *str)
{
    uint16_t n = 0;
    while (*str++) n++;
    return n * 16;
}

/* ==================== 进度条 ==================== */
void lcd_draw_progress(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint8_t percent, uint16_t fg, uint16_t bg, uint16_t border)
{
    if (percent > 100) percent = 100;
    /* 外边框(2px) */
    lcd_fill_rect(x, y, w, 2, border);
    lcd_fill_rect(x, y + h - 2, w, 2, border);
    lcd_fill_rect(x, y, 2, h, border);
    lcd_fill_rect(x + w - 2, y, 2, h, border);

    /* 内部区域 */
    uint16_t ix = x + 3;
    uint16_t iy = y + 3;
    uint16_t iw = (w > 6) ? (w - 6) : 0;
    uint16_t ih = (h > 6) ? (h - 6) : 0;
    if (iw == 0 || ih == 0) return;

    uint16_t fill_w = (uint32_t)iw * percent / 100;
    if (fill_w > 0) lcd_fill_rect(ix, iy, fill_w, ih, fg);
    if (fill_w < iw) lcd_fill_rect(ix + fill_w, iy, iw - fill_w, ih, bg);
}
