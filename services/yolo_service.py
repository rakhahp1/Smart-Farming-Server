import os
from datetime import datetime
from PIL import Image
from ultralytics import YOLO

import state
from config import YOLO_MODEL_PATH, YOLO_DIR, YOLO_CONF

_yolo_detector = None

def init_yolo():
    global _yolo_detector
    if not os.path.exists(YOLO_MODEL_PATH):
        print(f"[YOLO] WARNING: model tidak ditemukan: {YOLO_MODEL_PATH}")
        _yolo_detector = None
        return
    _yolo_detector = YOLO(YOLO_MODEL_PATH)
    print("[YOLO] Loaded:", YOLO_MODEL_PATH)

def yolo_predict_and_save(pil_image: Image.Image):
    """
    Jalankan YOLO pada gambar PIL, simpan hasil plot ke /static/yolo/,
    update state.yolo_state untuk dashboard.
    """
    if _yolo_detector is None:
        raise RuntimeError("YOLO model belum tersedia. Pastikan file yolov8n_selada.pt ada dan init_yolo() terpanggil.")

    # ✅ Tambahan parameter iou untuk mengurangi double bounding box
    results = _yolo_detector.predict(
        pil_image,
        conf=YOLO_CONF,
        iou=0.3,              # 🔥 penting: kurangi overlap box
        agnostic_nms=True,  # bisa ubah ke True kalau beda kelas masih overlap
        verbose=False
    )

    r = results[0]

    summary = {}
    if r.boxes is not None and len(r.boxes) > 0:
        cls_ids = r.boxes.cls.cpu().numpy().astype(int)

        for cid in cls_ids:
            name = r.names.get(int(cid), str(int(cid)))
            summary[name] = summary.get(name, 0) + 1

    top = None
    if summary:
        top = max(summary.items(), key=lambda kv: kv[1])[0]

    plotted = r.plot()  # numpy BGR
    img_out = Image.fromarray(plotted[..., ::-1])  # BGR -> RGB

    ts_file = datetime.now().strftime("%Y%m%d_%H%M%S")
    fname = f"yolo_{ts_file}.jpg"
    fpath = os.path.join(YOLO_DIR, fname)
    img_out.save(fpath, quality=90)

    state.yolo_state["updated_at"] = datetime.now().strftime("%H:%M:%S")
    state.yolo_state["image_url"] = f"/static/yolo/{fname}"
    state.yolo_state["summary"] = summary
    state.yolo_state["top"] = top

    return state.yolo_state