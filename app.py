import os
from flask import Flask

from services.decisiontree_service import init_ml
from services.yolo_service import init_yolo
from services.scheduler_service import init_scheduler
from services.mqtt_service import init_mqtt
from web.routes import register_routes


def create_app():
    app = Flask(
        __name__,
        template_folder="web/templates",
        static_folder="static"
    )

    # init subsystems (jalan sekali saat start)
    init_ml()     # train/load Decision Tree + save tree.png
    init_yolo()   # load YOLO model (yolov8n_selada.pt)
    init_scheduler()
    init_mqtt()   # connect mqtt & loop_start

    register_routes(app)
    return app


if __name__ == "__main__":
    app = create_app()
    print("Open dashboard: http://localhost:5000")

    port = int(os.environ.get("PORT", 5000))

    # PENTING: use_reloader=False supaya init_mqtt tidak kepanggil 2x
    app.run(
        host="0.0.0.0",
        port=port,
        debug=False,
        use_reloader=False,
        threaded=True
    )
