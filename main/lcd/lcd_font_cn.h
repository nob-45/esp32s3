/* 自动生成: 中文 16x16 点阵字库 (勿手改, 由 tools/gen_font_cn.py 生成) */
/* 字符数: 217, Flash 占用: 6944 字节 */
#ifndef LCD_FONT_CN_H
#define LCD_FONT_CN_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t utf8[3];      /* UTF-8 编码 (常用汉字均为3字节) */
    uint8_t bitmap[32];   /* 16x16 点阵, 每行 2 字节, MSB-first */
} font_cn_glyph_t;

extern const font_cn_glyph_t g_font_cn[];
extern const size_t          g_font_cn_count;

/* 按 UTF-8 三字节查找字模, 找不到返回 NULL */
const uint8_t *lcd_font_cn_lookup(uint8_t b0, uint8_t b1, uint8_t b2);

#ifdef __cplusplus
}
#endif

#endif /* LCD_FONT_CN_H */