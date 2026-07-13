@echo off
chcp 65001 >nul
cd /d d:\MyProjects\SmartElderCare

echo [1/4] 加载 ESP-IDF 环境...
call D:\Espressif\frameworks\esp-idf-v5.5.4\export.bat >nul 2>&1

echo [2/4] 清理并重建 build 目录...
if exist build rmdir /s /q build
mkdir build

echo [3/4] 运行 cmake 配置...
cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DPYTHON=D:\Espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe -DESP_PLATFORM=1 -DIDF_TARGET=esp32s3 -DCCACHE_ENABLE=1 -B build -S . > build\cmake_out.log 2>&1
set CMAKE_RC=%ERRORLEVEL%
echo cmake 退出码: %CMAKE_RC%

if not exist build\build.ninja (
    echo.
    echo ==== FAILED: build.ninja 未生成 ====
    echo ---cmake 日志最后 60 行---
    powershell -Command "Get-Content build\cmake_out.log -Tail 60"
    exit /b 1
)

echo [4/4] 运行 ninja 编译...
ninja -C build > build\ninja_out.log 2>&1
set NINJA_RC=%ERRORLEVEL%

echo.
echo ==== 编译结束，退出码: %NINJA_RC% ====
echo.
echo ---ninja 日志最后 40 行---
powershell -Command "Get-Content build\ninja_out.log -Tail 40"
echo.
echo ---错误行---
findstr /R /C:"FAILED" /C:"fatal error" /C:"error:" build\ninja_out.log
exit /b %NINJA_RC%