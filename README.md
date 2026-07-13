# SmartElderCare 智慧养老网关

基于 **ESP32-S3-DevKitC-1** 的居家监护网关开发中项目。当前阶段实现了 **LCD UI + 浴室积水检测** 两个基础模块。

---

## 一、简介

本项目定位为面向独居老人的家居安全监护设备，采用 ESP32-S3 作为主控，通过外挂多种传感器完成本地感知与显示。本仓库为项目的**基础开发阶段**代码，目标是先跑通 LCD 显示 + 单一传感器（水位）的完整链路，后续再逐步扩展其他模块。

---

## 二、当前已实现的功能

### 1. LCD 显示（ST7789 240×320）
- SPI 驱动，支持任意矩形填充、单色字符/字符串绘制
- 内置 8×16 ASCII 字库，以及 2 倍放大字体
- 提供进度条绘制 API
- 背光通过 LEDC PWM 控制，可调亮度

### 2. 浴室积水检测（水位探针 A0/D0）
- A0 通过 ADC1_CH5 采样，D0 作为数字阈值输入
- 启动时自动采集"干燥基线"，消除个体差异
- **中位数滤波**：一次采 32 个样本，排序后去掉首尾各 8 个，中间求均，抗尖峰
- **EMA 指数平滑**：`smoothed = 0.3*raw + 0.7*smoothed`，抑制水面波动
- **迟滞双阈值**：DAMP / FLOOD 两级阈值 + 30 counts 迟滞，防止临界值反复跳变
- **三态输出**：DRY（干燥）/ DAMP（潮湿）/ FLOOD（积水报警）

### 3. UI 界面
- 深色科技感主题
- 顶部标题栏（项目名 + 版本号）
- 状态卡：显示当前三态（背景色随状态变化：绿/橙/红）
- 进度条：显示相对水量百分比
- 数据面板：ADC(EMA)、Baseline、Delta、D0 状态、阈值
- 底部状态栏：运行时长 + 报警指示灯

### 4. 工程配套
- `build_flash.bat` 一键脚本：自动加载 ESP-IDF 环境 → 编译 → 烧录 → 监视
- 分区表 `partitions.csv` 适配 16MB Flash

---

## 三、硬件清单

| 器件 | 型号 | 说明 |
|------|------|------|
| 主控 | ESP32-S3-DevKitC-1 v1.1 | N16R8（16MB Flash / 8MB PSRAM） |
| LCD | 2.4" ST7789 SPI | 240×320 分辨率 |
| 水位传感器 | 通用水位探针模块 | 带 A0 模拟输出 + D0 数字阈值 |

---

## 四、硬件连接说明

### LCD (ST7789) 接线

| LCD 引脚 | ESP32-S3 GPIO | 说明 |
|---------|--------------|------|
| VCC     | 3.3V         | 电源 |
| GND     | GND          | 地   |
| SCLK    | GPIO12       | SPI 时钟 |
| MOSI    | GPIO11       | SPI 数据 |
| RES     | GPIO10       | 复位 |
| DC      | GPIO9        | 数据/命令 |
| CS      | GPIO14       | 片选 |
| BLK     | GPIO13       | 背光（LEDC PWM 调光） |

### 水位传感器接线

| 传感器引脚 | ESP32-S3 GPIO | 说明 |
|-----------|--------------|------|
| VCC | 3.3V   | 电源 |
| GND | GND    | 地 |
| A0  | GPIO6  | 模拟输出（ADC1_CH5） |
| D0  | GPIO5  | 数字阈值输出（有水=LOW） |

---

## 五、目录结构

```
project_esp32/
├── main/
│   ├── main.c                # 主程序 + UI 渲染
│   ├── lcd/
│   │   ├── lcd_st7789.h/c    # LCD 驱动
│   │   └── lcd_font.c        # 8x16 ASCII 字库
│   ├── sensor/
│   │   ├── water_sensor.h/c  # 积水检测（滤波+迟滞+三态）
│   └── CMakeLists.txt
├── CMakeLists.txt
├── partitions-16MiB.csv      # 16MB 分区表
├── sdkconfig
├── build_flash.bat           # 一键编译烧录脚本
└── README.md
```

---

## 六、快速上手

### 环境要求
- ESP-IDF v5.x
- Windows 系统

### 一键运行
双击 `build_flash.bat`，脚本会自动完成 ESP-IDF 环境加载 → 编译 → 烧录 → 打开监视器。

### 手动命令
```bat
idf.py set-target esp32s3
idf.py build
idf.py -p COM4 flash monitor
```

---

## 七、已知问题

- 水位传感器在**长时间通电**下电极会电解产生水垢/氧化，导致基线漂移，需要定期重新校准（调用 `water_sensor_recalibrate()`）
- 传感器灵敏度对水的电导率有依赖，纯净水响应较弱，自来水/浴室积水响应正常
- 当前阈值 `WATER_DELTA_DAMP=80` / `WATER_DELTA_FLOOD=250` 是实验室调出来的经验值，实际部署时需要根据现场传感器批次微调