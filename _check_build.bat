@echo off
call D:\Espressif\frameworks\esp-idf-v5.5.4\export.bat >nul 2>&1
idf.py build > build.log 2>&1
if errorlevel 1 (
    echo BUILD_FAIL
    findstr /I "error warning failed" build.log
) else (
    echo BUILD_OK
)