from apscheduler.schedulers.background import BackgroundScheduler
from datetime import datetime
import cv2
from PIL import Image

from services.yolo_service import yolo_predict_and_save

scheduler = BackgroundScheduler()

# 🔴 buka kamera SEKALI saja (webcam eksternal)
cap = cv2.VideoCapture(1)

# optional: set resolusi
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)


def auto_capture():
    print("[AUTO] Capture jalan:", datetime.now())

    if not cap.isOpened():
        print("[AUTO] Kamera tidak terbuka")
        return

    ret, frame = cap.read()

    if ret:
        # OpenCV (BGR) → PIL (RGB)
        frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        pil_img = Image.fromarray(frame_rgb)

        # pipeline YOLO
        result = yolo_predict_and_save(pil_img)

        kondisi = result.get("top")
        print("[AUTO] Deteksi:", kondisi)

        # 🎯 optional: hanya simpan kondisi penting
        if kondisi in ["Ready", "Damaged"]:
            print("[AUTO] Kondisi penting terdeteksi")

    else:
        print("[AUTO] Gagal capture frame")


def init_scheduler():
    # interval (testing)
    #scheduler.add_job(auto_capture, 'interval', minutes=1)

    # contoh real use:
    scheduler.add_job(auto_capture, 'cron', hour='8,12,16', minute=54)

    scheduler.start()
    print("[SYSTEM] Scheduler aktif")