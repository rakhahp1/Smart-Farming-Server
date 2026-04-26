import os

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

# ================= MQTT CONFIG =================
BROKER = "test.mosquitto.org"
PORT = 1883
TOPIC_BASE = "SatriaSensors773546"

RELAY_TOPICS = {
    1: f"{TOPIC_BASE}/relay1",
    2: f"{TOPIC_BASE}/relay2",
    3: f"{TOPIC_BASE}/relay3",
}

sensor_topics = [
    f"{TOPIC_BASE}/ldr",
    f"{TOPIC_BASE}/suhu",
    f"{TOPIC_BASE}/kelembaban",
    f"{TOPIC_BASE}/kelembaban_tanah_1A",
    f"{TOPIC_BASE}/kelembaban_tanah_1B",
    f"{TOPIC_BASE}/kelembaban_tanah_2A",
    f"{TOPIC_BASE}/kelembaban_tanah_2B",
    f"{TOPIC_BASE}/kelembaban_tanah_3A",
    f"{TOPIC_BASE}/kelembaban_tanah_3B",
]
SENSOR_KEYS = [s.split("/")[-1] for s in sensor_topics]

# ================= PATHS =================
STATIC_DIR = os.path.join(BASE_DIR, "static")
YOLO_DIR = os.path.join(STATIC_DIR, "yolo")
os.makedirs(STATIC_DIR, exist_ok=True)
os.makedirs(YOLO_DIR, exist_ok=True)

# !!! GANTI PATH DATASET KAMU DI SINI !!!
DATASET_PATH = r"C:\Users\rakha\Downloads\pyhtonserver_uji\POT3-Copy\smart farming server\dataset.csv"

# ================= MODEL CONFIG =================
FEATURES = ["suhu", "kelembaban", "kelembaban_tanah", "intensitas_cahaya"]
TARGET = "label"  # 0/1
LABEL_MAP = {0: "TIDAK SIRAM", 1: "SIRAM"}

# ================= HEURISTIC "AKTUAL" =================
THRESH_SOIL_NEED_WATER = 45.0

# ================= YOLO CONFIG =================
YOLO_MODEL_PATH = os.path.join(BASE_DIR, "selada_mix.pt")
YOLO_CONF = 0.6

# ================= DASHBOARD =================
MAX_HISTORY = 60
