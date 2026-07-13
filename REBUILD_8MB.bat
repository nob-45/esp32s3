@echo off
chcp 65001 >nul
cd /d d:\MyProjects\SmartElderCare

echo ============================================
echo   SmartElderCare 重新编译 (Flash 8MB 配置)
echo ============================================
echo.

echo [清理] 删除旧的 sdkconfig 缓存...
if exist sdkconfig del /q sdkconfig

echo [清理] 删除旧的 build 目录...
if exist build rmdir /s /q build

echo.
echo [1/3] 加载 ESP-IDF 环境...
call D:\Espressif\frameworks\esp-idf-v5.5.4\export.bat >nul 2>&1

echo [2/3] cmake 配置中 (约30秒)...
mkdir build
cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DPYTHON=D:\Espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe -DESP_PLATFORM=1 -DIDF_TARGET=esp32s3 -DCCACHE_ENABLE=1 -B build -S . > build\cmake_out.log 2>&1
if not exist build\build.ninja (
    echo [FAILED] cmake 配置失败，日志末尾:
    powershell -Command "Get-Content build\cmake_out.log -Tail 40"
    pause
    exit /b 1
)

echo [3/3] ninja 编译中 (约2-3分钟)...
ninja -C build > build\ninja_out.log 2>&1
set RC=%ERRORLEVEL%

echo.
if %RC%==0 (
    echo ============================================
    echo   编译成功！
    echo ============================================
    dir build\SmartElderCare.bin
    echo.
    echo 下一步: 运行 FLASH.bat 烧录
) else (
    echo ============================================
    echo   编译失败 退出码=%RC%
    echo ============================================
    findstr /R /C:"FAILED" /C:"error:" build\ninja_out.log
    echo.
    echo --- ninja 日志末尾 30 行 ---
    powershell -Command "Get-Content build\ninja_out.log -Tail 30"
)
echo.
pause