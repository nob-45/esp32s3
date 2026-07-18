# -*- coding: utf-8 -*-
"""
中文点阵字模生成器
用途: 从系统字体(微软雅黑/黑体) 栅格化生成 16x16 点阵中文字库
输出: main/lcd/lcd_font_cn.c  +  main/lcd/lcd_font_cn.h

依赖:
    pip install pillow

运行 (在项目根目录):
    python tools/gen_font_cn.py
"""

import os
import sys

try:
    from PIL import Image, ImageFont, ImageDraw
except ImportError:
    print("[错误] 缺少 pillow 库, 请先执行:  pip install pillow")
    sys.exit(1)

# ============ 配置区 ============
# 注: 只要 main.c 里出现的汉字都要收录, 否则会显示成方块
CHARS = (
    # 标题 / 品牌
    "智慧养老综合监护网关"
    # 系统 / 状态
    "系统状态运行启动完成开机时长秒分钟小时"
    "初始化中传感器校准请稍候刷新画面进度"
    # 水位 / 地面
    "水位地面积水干燥潮湿"
    # 空气 / 气味
    "空气质量洁净异味污染新鲜"
    # 雷达 / 人体
    "雷达人体存在有人无人空置活动静止"
    # 险情等级
    "正常安全注意警告警报危险预热异常"
    # 房间
    "浴室卧室厨房客厅"
    # 常用界面词
    "标题信息版本网络在线离线时间日期"
    "开关模式设置菜单返回确认取消保存"
    "温度湿度环境卫生间通风提示"
    "报警级别一二三四五六七八九十"
    "上下左右前后内外中"
    "文字数字符号名称类型编号"
    # 兜底常用字
    "的是不了在有和这个我们你他她它"
    "画面板卡显示屏刷新率颜色亮度背光"
    "综合骨架架构布局条目区域框边距"
    "计算记录历史缓存清除更新重启复位"
    # main.c 补充
    "主伴侧并序底循栏程部染标"
)

FONT_CANDIDATES = [
    "C:/Windows/Fonts/msyhbd.ttc",     # 微软雅黑 Bold (推荐, 现代圆润)
    "C:/Windows/Fonts/msyhbd.ttf",     # 备选路径
    "C:/Windows/Fonts/simhei.ttf",     # 黑体 备选
    "C:/Windows/Fonts/msyh.ttc",       # 微软雅黑 Regular
    "C:/Windows/Fonts/simsun.ttc",     # 宋体 兜底
]

FONT_SIZE  = 16    # 16px 与网格等高, 配合膨胀让笔画饱满
THRESHOLD  = 80    # 阈值下调, 边缘更饱满
DILATE     = True  # 1像素膨胀, 让细笔画在16x16下也清晰

OUT_C_PATH = "main/lcd/lcd_font_cn.c"
OUT_H_PATH = "main/lcd/lcd_font_cn.h"


def pick_font():
    for p in FONT_CANDIDATES:
        if os.path.exists(p):
            print(f"[字体] 使用: {p}")
            return ImageFont.truetype(p, FONT_SIZE)
    print("[警告] 未找到中文字体")
    return ImageFont.load_default()


def rasterize(ch, font):
    """
    在 32x32 大画布上高精度渲染 -> 用字形 bbox 精确居中 -> 缩放到 16x16
    这样比直接 16px 渲染更平滑、更清晰
    """
    BIG = 32
    big = Image.new("L", (BIG, BIG), 0)
    draw = ImageDraw.Draw(big)
    # 用 2 倍尺寸渲染, 再降采样 -> 抗锯齿效果
    big_font = ImageFont.truetype(font.path, FONT_SIZE * 2) if hasattr(font, "path") else font
    draw.text((0, 0), ch, fill=255, font=big_font, anchor="lt")

    bbox = big.getbbox()
    if bbox is None:
        return bytes(32)

    # 裁出字形墨迹, 缩放到 14x14 留 1px 边距
    glyph = big.crop(bbox)
    TARGET = 14
    # LANCZOS 高质量缩放, 保留笔画细节
    glyph = glyph.resize((TARGET, TARGET), Image.LANCZOS)

    img = Image.new("L", (16, 16), 0)
    # 居中贴到 16x16 网格
    img.paste(glyph, ((16 - TARGET) // 2, (16 - TARGET) // 2))

    # ---- 二值化 ----
    bits = [[0]*16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            if img.getpixel((x, y)) >= THRESHOLD:
                bits[y][x] = 1

    # ---- 轻度膨胀(可选): 若像素为 1 且相邻空缺, 把它右/下的浅灰(>50)也点亮
    if DILATE:
        for y in range(16):
            for x in range(16):
                if bits[y][x] == 0:
                    v = img.getpixel((x, y))
                    if v >= 50:  # 边缘浅灰
                        # 若上/左邻居已亮, 则补齐, 让笔画不断裂
                        if (x > 0 and bits[y][x-1]) or (y > 0 and bits[y-1][x]):
                            bits[y][x] = 1

    data = bytearray(32)
    for y in range(16):
        b_high = 0
        b_low  = 0
        for x in range(16):
            if bits[y][x]:
                if x < 8:
                    b_high |= 0x80 >> x
                else:
                    b_low  |= 0x80 >> (x - 8)
        data[y * 2]     = b_high
        data[y * 2 + 1] = b_low
    return bytes(data)


def main():
    # 去重保序
    seen = set()
    chars = []
    for c in CHARS:
        if c not in seen:
            seen.add(c)
            chars.append(c)
    print(f"[字符] 总数 = {len(chars)}, Flash 占用 = {len(chars)*32} 字节")

    font = pick_font()

    os.makedirs(os.path.dirname(OUT_H_PATH), exist_ok=True)

    # ---------- .h ----------
    h_lines = []
    h_lines.append("/* 自动生成: 中文 16x16 点阵字库 (勿手改, 由 tools/gen_font_cn.py 生成) */")
    h_lines.append(f"/* 字符数: {len(chars)}, Flash 占用: {len(chars)*32} 字节 */")
    h_lines.append("#ifndef LCD_FONT_CN_H")
    h_lines.append("#define LCD_FONT_CN_H")
    h_lines.append("")
    h_lines.append("#include <stdint.h>")
    h_lines.append("#include <stddef.h>")
    h_lines.append("")
    h_lines.append("#ifdef __cplusplus")
    h_lines.append('extern "C" {')
    h_lines.append("#endif")
    h_lines.append("")
    h_lines.append("typedef struct {")
    h_lines.append("    uint8_t utf8[3];      /* UTF-8 编码 (常用汉字均为3字节) */")
    h_lines.append("    uint8_t bitmap[32];   /* 16x16 点阵, 每行 2 字节, MSB-first */")
    h_lines.append("} font_cn_glyph_t;")
    h_lines.append("")
    h_lines.append("extern const font_cn_glyph_t g_font_cn[];")
    h_lines.append("extern const size_t          g_font_cn_count;")
    h_lines.append("")
    h_lines.append("/* 按 UTF-8 三字节查找字模, 找不到返回 NULL */")
    h_lines.append("const uint8_t *lcd_font_cn_lookup(uint8_t b0, uint8_t b1, uint8_t b2);")
    h_lines.append("")
    h_lines.append("#ifdef __cplusplus")
    h_lines.append("}")
    h_lines.append("#endif")
    h_lines.append("")
    h_lines.append("#endif /* LCD_FONT_CN_H */")

    with open(OUT_H_PATH, "w", encoding="utf-8") as f:
        f.write("\n".join(h_lines))
    print(f"[生成] {OUT_H_PATH}")

    # ---------- .c ----------
    c_lines = []
    c_lines.append("/* 自动生成, 勿手改 - 由 tools/gen_font_cn.py 生成 */")
    c_lines.append('#include "lcd_font_cn.h"')
    c_lines.append("")
    c_lines.append("const font_cn_glyph_t g_font_cn[] = {")
    for ch in chars:
        b = ch.encode("utf-8")
        if len(b) != 3:
            print(f"[跳过] '{ch}' UTF-8 长度 {len(b)} != 3")
            continue
        bmp = rasterize(ch, font)
        utf8_str = ", ".join(f"0x{x:02X}" for x in b)
        bmp_str  = ", ".join(f"0x{x:02X}" for x in bmp)
        c_lines.append(f"    /* '{ch}' */")
        c_lines.append(f"    {{ {{ {utf8_str} }}, {{ {bmp_str} }} }},")
    c_lines.append("};")
    c_lines.append("")
    c_lines.append("const size_t g_font_cn_count = sizeof(g_font_cn) / sizeof(g_font_cn[0]);")
    c_lines.append("")
    c_lines.append("const uint8_t *lcd_font_cn_lookup(uint8_t b0, uint8_t b1, uint8_t b2)")
    c_lines.append("{")
    c_lines.append("    for (size_t i = 0; i < g_font_cn_count; i++) {")
    c_lines.append("        if (g_font_cn[i].utf8[0] == b0 &&")
    c_lines.append("            g_font_cn[i].utf8[1] == b1 &&")
    c_lines.append("            g_font_cn[i].utf8[2] == b2) {")
    c_lines.append("            return g_font_cn[i].bitmap;")
    c_lines.append("        }")
    c_lines.append("    }")
    c_lines.append("    return (void *)0;")
    c_lines.append("}")

    with open(OUT_C_PATH, "w", encoding="utf-8") as f:
        f.write("\n".join(c_lines))
    print(f"[生成] {OUT_C_PATH}")
    print("[完成]")


if __name__ == "__main__":
    main()