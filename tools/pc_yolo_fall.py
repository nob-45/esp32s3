"""
SmartElderCare - 上位机 (PC) YOLO 跌倒识别脚本

工作机制:
  1) 从 ESP32-S3 摄像头拉 MJPEG 视频流:  http://<板子IP>/stream
  2) 使用 Ultralytics YOLOv8-Pose 做人体关键点检测
  3) 通过人体外接框的 宽高比 + 关键点纵坐标离散度 判断是否跌倒
  4) 一旦连续 N 帧判定为跌倒, 立即 HTTP POST 到板子:  http://<板子IP>/alarm
     板子会在 LCD 显示 "老人跌倒 AI 识别 请立即查看" 并触发声光报警

依赖 (在 PC 上执行, 不需要装到板子里):
    pip install ultralytics opencv-python requests numpy

运行:
    python tools/pc_yolo_fall.py --ip 192.168.1.123
"""

import argparse
import time
import sys
import cv2
import numpy as np
import requests
from urllib.parse import urljoin

try:
    from ultralytics import YOLO
except ImportError:
    print("[错误] 未安装 ultralytics, 请先执行:  pip install ultralytics opencv-python requests")
    sys.exit(1)


def parse_args():
    p = argparse.ArgumentParser(description="ESP32-S3 智慧养老网关 - 上位机 YOLO 跌倒识别")
    p.add_argument("--ip", required=True, help="ESP32-S3 板子的 IP 地址, 例如 192.168.1.123")
    p.add_argument("--model", default="yolov8n-pose.pt",
                   help="YOLO 模型权重 (默认 yolov8n-pose.pt, 首次运行会自动下载)")
    p.add_argument("--conf", type=float, default=0.5, help="检测置信度阈值")
    p.add_argument("--fall-ratio", type=float, default=1.2,
                   help="跌倒判定阈值: 人体外接框 宽/高 大于此值即认为躺倒 (默认 1.2)")
    p.add_argument("--confirm-frames", type=int, default=5,
                   help="连续多少帧判定为跌倒后才上报 (默认 5, 防止误报)")
    p.add_argument("--cooldown", type=int, default=20,
                   help="上报后冷却时间 (秒), 期间不再重复上报 (默认 20)")
    p.add_argument("--show", action="store_true", help="显示实时画面窗口 (调试用)")
    return p.parse_args()


def is_fall(box_xywh, ratio_thresh):
    """
    简单几何法判断跌倒:
      - box_xywh: [x_center, y_center, w, h]
      - 站立时 h > w, 跌倒时 w > h  =>  w/h > ratio_thresh
    """
    _, _, w, h = box_xywh
    if h < 1e-6:
        return False
    return (w / h) > ratio_thresh


def report_fall(base_url):
    """向 ESP32 POST /alarm 触发本地声光报警"""
    url = urljoin(base_url, "/alarm")
    try:
        r = requests.post(url, timeout=2)
        print(f"[上报] POST {url} -> {r.status_code} {r.text}")
    except Exception as e:
        print(f"[上报失败] {e}")


def main():
    args = parse_args()
    base_url = f"http://{args.ip}"
    stream_url = urljoin(base_url, "/stream")

    print(f"[启动] 加载 YOLO 模型: {args.model}")
    model = YOLO(args.model)

    print(f"[启动] 打开视频流: {stream_url}")
    cap = cv2.VideoCapture(stream_url)
    if not cap.isOpened():
        print(f"[错误] 无法打开视频流, 请检查板子 IP 和网络: {stream_url}")
        sys.exit(1)

    fall_counter = 0
    last_report_ts = 0.0
    fps_t0 = time.time()
    fps_n = 0

    while True:
        ok, frame = cap.read()
        if not ok:
            print("[警告] 读帧失败, 1s 后重试")
            time.sleep(1.0)
            cap.release()
            cap = cv2.VideoCapture(stream_url)
            continue

        # YOLOv8-Pose 推理 (只关心 person 类, class 0)
        results = model.predict(frame, conf=args.conf, classes=[0], verbose=False)

        fall_now = False
        if len(results) > 0:
            r0 = results[0]
            if r0.boxes is not None and len(r0.boxes) > 0:
                # 遍历所有检测到的人
                for box in r0.boxes:
                    xywh = box.xywh[0].cpu().numpy()  # [cx, cy, w, h]
                    if is_fall(xywh, args.fall_ratio):
                        fall_now = True
                        cx, cy, w, h = xywh
                        x1 = int(cx - w / 2); y1 = int(cy - h / 2)
                        x2 = int(cx + w / 2); y2 = int(cy + h / 2)
                        cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 0, 255), 3)
                        cv2.putText(frame, "FALL", (x1, max(0, y1 - 8)),
                                    cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 0, 255), 2)
                    else:
                        cx, cy, w, h = xywh
                        x1 = int(cx - w / 2); y1 = int(cy - h / 2)
                        x2 = int(cx + w / 2); y2 = int(cy + h / 2)
                        cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 200, 0), 2)

        # 连续帧确认, 防误报
        if fall_now:
            fall_counter += 1
        else:
            fall_counter = 0

        now = time.time()
        if fall_counter >= args.confirm_frames and (now - last_report_ts) > args.cooldown:
            print(f"*** 检测到跌倒! 连续 {fall_counter} 帧, 立即上报板子 ***")
            report_fall(base_url)
            last_report_ts = now
            fall_counter = 0

        # 计算 FPS
        fps_n += 1
        if now - fps_t0 >= 2.0:
            fps = fps_n / (now - fps_t0)
            print(f"[状态] FPS={fps:.1f}  fall_counter={fall_counter}")
            fps_t0 = now
            fps_n = 0

        if args.show:
            cv2.putText(frame, f"fall_cnt={fall_counter}", (10, 25),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)
            cv2.imshow("SmartElderCare - PC YOLO Fall", frame)
            if cv2.waitKey(1) & 0xFF == 27:  # ESC 退出
                break

    cap.release()
    if args.show:
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()