from serial.tools import list_ports
ports = list(list_ports.comports())
if not ports:
    print("[WARN] 未检测到任何串口")
else:
    for p in ports:
        print(p.device, "-", p.description)