# -*- coding: utf-8 -*-
"""检查 main.c 里被截断的中文字符串到底是什么字节"""
import re

path = "main/main.c"
with open(path, "rb") as f:
    d = f.read()

# 找所有 lcd_measure_utf8_2x("...") 和 lcd_measure_utf8("...")
for m in re.finditer(rb'lcd_(?:measure|draw)_utf8(?:_2x)?\(([^)]+)\)', d):
    s = m.group(1)
    # 提取引号内内容
    q = re.search(rb'"([^"]*)"', s)
    if q:
        raw = q.group(1)
        try:
            txt = raw.decode("utf-8")
        except Exception:
            txt = "<decode error>"
        print(f"HEX: {raw.hex()}  |  {txt!r}")