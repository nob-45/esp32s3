@echo off
chcp 65001 >nul
cd /d d:\MyProjects\SmartElderCare

echo ============================================
echo   AUTO: 清理 -^> 编译 -^> 烧录 -^> 抓串口
echo ============================================

echo [1] 清理旧配置...
if exist sdkconfig del /q sdkconfig
if exist build rmdir /s /q build
mkdir build

echo [2] 加载 ESP-IDF...
call D:\Espressif\frameworks\esp-idf-v5.5.4\export.bat >nul 2>&1

echo [3] cmake 配置...
cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DPYTHON=D:\Espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe -DESP_PLATFORM=1 -DIDF_TARGET=esp32s3 -DCCACHE_ENABLE=1 -B build -S . > build\cmake_out.log 2>&1
if not exist build\build.ninja (
    echo [FAILED] cmake 失败
    powershell -Command "Get-Content build\cmake_out.log -Tail 40"
    exit /b 1
)

echo [4] ninja 编译...
ninja -C build > build\ninja_out.log 2>&1
if not exist build\SmartElderCare.bin (
    echo [FAILED] 编译失败
    findstr /R /C:"FAILED" /C:"error:" build\ninja_out.log
    powershell -Command "Get-Content build\ninja_out.log -Tail 20"
    exit /b 1
)
echo    编译 OK: 
dir build\SmartElderCare.bin | findstr SmartElderCare

echo [5] 烧录到 COM21...
idf.py -p COM21 flash > build\flash_out.log 2>&1
if errorlevel 1 (
    echo [FAILED] 烧录失败
    powershell -Command "Get-Content build\flash_out.log -Tail 30"
    exit /b 1
)
echo    烧录 OK

echo [6] 等待板子复位 (2秒)...
timeout /t 2 /nobreak >nul

echo [7] 抓取串口日志 15秒...
python read_serial.py

echo.
echo ============================================
echo   完成! 串口日志:
echo ============================================
powershell -Command "Get-Content serial_out.log -Tail 60"