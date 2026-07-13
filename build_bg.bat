@echo off
chcp 65001 >nul
cd /d d:\MyProjects\SmartElderCare

echo [1/2] 后台启动 ninja 编译 (独立窗口)...
echo 日志输出: d:\MyProjects\SmartElderCare\build\ninja_out.log
echo 完成标记: d:\MyProjects\SmartElderCare\build\_build_done.txt

if exist build\_build_done.txt del build\_build_done.txt

start "ninja-build" /MIN cmd /c "call D:\Espressif\frameworks\esp-idf-v5.5.4\export.bat >nul 2>&1 && ninja -C d:\MyProjects\SmartElderCare\build > d:\MyProjects\SmartElderCare\build\ninja_out.log 2>&1 && echo OK > d:\MyProjects\SmartElderCare\build\_build_done.txt || echo FAIL > d:\MyProjects\SmartElderCare\build\_build_done.txt"

echo [2/2] 已在后台启动，本窗口立即退出。
echo 稍后请查询: type build\_build_done.txt   看到 OK 表示成功
exit /b 0