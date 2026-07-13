
@echo off
chcp 65001 >nul
cd /d d:\MyProjects\SmartElderCare

echo ============================================
echo  ESP32-S3 智慧养老网关 - 编译脚本
echo ============================================
echo  首次编译约 5-15 分钟，请耐心等待
echo  编译过程中不要关闭这个窗口！
echo  不要按 Ctrl+C ！
echo ============================================
echo.

echo [步骤 1/2] 加载 ESP-IDF 环境...
call D:\Espressif\frameworks\esp-idf-v5.5.4\export.bat
if errorlevel 1 (
    echo.
    echo [错误] ESP-IDF 环境加载失败！
    pause
    exit /b 1
)

echo.
echo [步骤 2/2] 开始编译 (ninja)...
echo ------------------------------
ninja -C d:\MyProjects\SmartElderCare\build
set NINJA_RET=%errorlevel%
echo ------------------------------

echo.
if %NINJA_RET% EQU 0 (
    echo ============================================
    echo  [成功] 编译完成！
    echo ============================================
    dir d:\MyProjects\SmartElderCare\build\*.bin
    echo.
    echo 下一步：把开发板 USB 接电脑，运行烧录命令
    echo   idf.py -p COMx flash monitor
    echo   ^(COMx 替换成设备管理器里看到的实际串口号^)
) else (
    echo ============================================
    echo  [失败] 编译错误，退出码: %NINJA_RET%
    echo ============================================
    echo 请把上面 error: 相关的行截图给我
)

echo.
echo 按任意键关闭窗口...
pause >nul