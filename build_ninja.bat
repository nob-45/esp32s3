@echo off
chcp 65001 >nul
cd /d d:\MyProjects\SmartElderCare

echo [1/2] 加载 ESP-IDF 环境...
call D:\Espressif\frameworks\esp-idf-v5.5.4\export.bat >nul 2>&1

if not exist build\build.ninja (
    echo [ERROR] build\build.ninja 不存在，请先运行 build_p3.bat 做 cmake 配置
    exit /b 1
)

echo [2/2] 增量编译 (ninja)，日志: build\ninja_out.log
ninja -C build > build\ninja_out.log 2>&1
set NINJA_RC=%ERRORLEVEL%

echo.
echo ==== ninja 退出码: %NINJA_RC% ====
echo ---最后 30 行日志---
powershell -Command "Get-Content build\ninja_out.log -Tail 30"
echo.
echo ---错误行 (若有)---
findstr /R /C:"FAILED" /C:"fatal error" /C:"error:" build\ninja_out.log

if exist build\*.bin (
    echo.
    echo ==== 生成的 bin 文件 ====
    dir build\*.bin
)
exit /b %NINJA_RC%