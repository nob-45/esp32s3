# 读取 COM21 串口输出（10秒），触发一次复位，日志写入 serial_out.log
import serial, time, sys, os

port = 'COM21'
logfile = 'serial_out.log'

try:
    s = serial.Serial(port, 115200, timeout=0.5)
except Exception as e:
    with open(logfile, 'w', encoding='utf-8') as f:
        f.write(f"[ERR] 打开 {port} 失败: {e}\n")
    print(f"[ERR] 打开 {port} 失败: {e}")
    sys.exit(1)

# 复位 ESP32 (DTR/RTS)
s.setDTR(False)
s.setRTS(True)
time.sleep(0.1)
s.setRTS(False)
time.sleep(0.2)
s.reset_input_buffer()

print(f"[OK] {port} 已连接，开始抓取 10 秒串口数据...")

# 以二进制追加方式写入，避免 Windows GBK 编码问题
with open(logfile, 'wb') as f:
    f.write(f"[OK] {port} 已连接，开始抓取 10 秒串口数据...\n".encode('utf-8'))
    f.write(b"-" * 60 + b"\n")
    end = time.time() + 10
    while time.time() < end:
        data = s.read(4096)
        if data:
            f.write(data)
            f.flush()
    f.write(b"\n" + b"-" * 60 + b"\n")
    f.write(b"[OK] end\n")

s.close()
print("[OK] 抓取结束，日志已写入", logfile)