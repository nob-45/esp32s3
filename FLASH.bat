@echo off
chcp 65001 >nul
cd /d d:\MyProjects\SmartElderCare

echo ============================================
echo  ESP32-S3 智慧养老网关 - 烧录 + 串口监视
echo  串口: COM21
echo ============================================
echo.
echo  烧录前请确认：
echo   1) 开发板 USB 已插入电脑
echo   2) 设备管理器里能看到 COM21
echo   3) 屏幕已按下面顺序接好线：
echo        屏3V3 -^> S3 3.3V
echo        屏GND -^> S3 GND
echo        屏SCK -^> S3 GPIO15
echo        屏SDA -^> S3 GPIO16
echo        屏CS  -^> S3 GPIO17
echo        屏WR  -^> S3 GPIO18   ^(注意：WR就是DC^)
echo        屏RST -^> S3 GPIO8
echo        屏PWR -^> S3 3.3V     ^(直接接3V3常亮, 不占用GPIO^)
echo   4) 液位传感器：
echo        VCC -^> 3.3V,  GND -^> GND
echo        AO  -^> GPIO6  ^(ADC1_CH5^)
echo        DO  -^> GPIO5
echo ============================================
echo.
pause

echo.
echo [步骤 1/2] 加载 ESP-IDF 环境...
call D:\Espressif\frameworks\esp-idf-v5.5.4\export.bat >nul
if errorlevel 1 (
    echo [错误] ESP-IDF 环境加载失败！
    pause
    exit /b 1
)

echo.
echo [步骤 2/2] 开始烧录到 COM21...
echo ------------------------------
idf.py -p COM21 flash monitor
echo ------------------------------

echo.
echo 提示：如需退出串口监视，按 Ctrl + ]
pause