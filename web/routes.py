import time
import base64
from io import BytesIO
from flask import jsonify, render_template, request

import state
from config import THRESH_SOIL_NEED_WATER
from services.mqtt_service import publish_relay
from services.yolo_service import yolo_predict_and_save
from PIL import Image


def register_routes(app):

    @app.route("/")
    def index():
        return render_template("index.html")

    @app.route("/mode/<mode>", methods=["POST"])
    def set_mode(mode):
        state.control_mode["mode"] = "auto" if mode.strip().lower() == "auto" else "manual"
        return jsonify(ok=True, mode=state.control_mode["mode"])

    @app.route("/relay/<int:rid>/<relay_state_value>", methods=["POST"])
    def set_relay(rid, relay_state_value):
        publish_relay(rid, relay_state_value)
        return jsonify(ok=True, relay=rid, state=state.relay_state[rid])

    @app.route("/sensors")
    def sensors_api():
        relay_json = {str(k): v for k, v in state.relay_state.items()}
        return jsonify(
            values=state.sensor_data,
            timestamps=state.sensor_timestamp,
            relay=relay_json,
            mode=state.control_mode["mode"]
        )

    @app.route("/decision")
    def decision_api():
        return jsonify(state.decision_info)

    @app.route("/history")
    def history_api():
        return jsonify({k: list(v) for k, v in state.history.items()})

    @app.route("/accuracy")
    def accuracy_api():
        if state.total_count == 0:
            return jsonify(accuracy=0)
        return jsonify(round((state.correct_count / state.total_count) * 100, 2))

    @app.route("/meta")
    def meta_api():
        up = int(time.time() - state.START_TS)
        hh = up // 3600
        mm = (up % 3600) // 60
        ss = up % 60
        return jsonify(
            model_acc=round(state.model_acc, 4),
            thresh_soil=THRESH_SOIL_NEED_WATER,
            uptime=f"{hh:02d}:{mm:02d}:{ss:02d}"
        )

    @app.route("/rules")
    def rules_api():
        return state.tree_rules_text

    # ===================== YOLO API =====================

    @app.route("/yolo_latest")
    def yolo_latest():
        return jsonify(state.yolo_state)

    # ===== Webcam (base64) =====
    @app.route("/yolo_submit", methods=["POST"])
    def yolo_submit():
        data = request.get_json(silent=True) or {}
        image_data = data.get("image")

        if not image_data:
            return jsonify(status="error", message="Tidak ada gambar"), 400

        try:
            image_str = image_data.split(",")[1]
            image_bytes = base64.b64decode(image_str)
            pil_img = Image.open(BytesIO(image_bytes)).convert("RGB")

            yolo_predict_and_save(pil_img)

            return jsonify(
                status="success",
                message="YOLO webcam berhasil",
                **state.yolo_state
            )

        except Exception as e:
            return jsonify(status="error", message=str(e)), 500

    # ===== Upload File =====
    @app.route("/yolo_upload", methods=["POST"])
    def yolo_upload():
        file = request.files.get("image")

        if not file:
            return jsonify(status="error", message="Tidak ada file"), 400

        try:
            # convert file → PIL
            pil_img = Image.open(file.stream).convert("RGB")

            # proses YOLO (pakai service yang sama)
            yolo_predict_and_save(pil_img)

            return jsonify(
                status="success",
                message="YOLO upload berhasil",
                **state.yolo_state
            )

        except Exception as e:
            return jsonify(status="error", message=str(e)), 500