@echo off
REM ============================================================
REM  SmartElderCare - Build + Flash + Monitor (One-click)
REM  ESP-IDF: D:\Espressif\frameworks\esp-idf-v5.5.4
REM  Serial : COM21   (change here if needed)
REM ============================================================
setlocal
set IDF_EXPORT=D:\Espressif\frameworks\esp-idf-v5.5.4\export.bat
set PORT=COM21

echo.
echo === Activating ESP-IDF ===
call "%IDF_EXPORT%" >nul
if errorlevel 1 (
    echo [ERROR] Failed to load ESP-IDF environment.
    pause
    exit /b 1
)

echo.
echo === Building project ===
idf.py build
if errorlevel 1 (
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

echo.
echo === Flashing to %PORT% ===
idf.py -p %PORT% flash
if errorlevel 1 (
    echo [ERROR] Flash failed.
    pause
    exit /b 1
)

echo.
echo === Monitor (press Ctrl+] to exit) ===
idf.py -p %PORT% monitor

endlocal