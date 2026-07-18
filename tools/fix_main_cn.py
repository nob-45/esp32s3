# -*- coding: utf-8 -*-
"""字节级修复 main.c 中的 Latin-1 -> UTF-8 双重编码乱码"""

path = "main/main.c"
with open(path, "rb") as f:
    data = f.read()

# 需要修复的字符串: 直接用字节替换
# (乱码字节, 正确 UTF-8 字节)
pairs = [
    # "系统启动"
    (bytes.fromhex("c3a7c2b3c2bbc3a7c2bbc29fc3a5c290c2afc3a5c28ac2a8"),
     "系统启动".encode("utf-8")),
    # "正在初始化传感器"
    (bytes.fromhex("c3a6c2adc2a3c3a5c29cc2a8c3a5c288c29dc3a5c2a7c28bc3a5c28cc296c3a4c2bcc2a0c3a6c284c29fc3a5c299c2a8"),
     "正在初始化传感器".encode("utf-8")),
]

for bad, good in pairs:
    if bad in data:
        data = data.replace(bad, good)
        print(f"replaced -> {good.decode('utf-8')!r}")
    else:
        print(f"NOT FOUND: {bad.hex()}")

with open(path, "wb") as f:
    f.write(data)

# 验证
with open(path, "rb") as f:
    v = f.read().decode("utf-8")
print("has 运行:", "运行" in v)
print("has 系统启动:", "系统启动" in v)
print("has 正在初始化传感器:", "正在初始化传感器" in v)