import os
import time
import csv
from datetime import datetime

import numpy as np
import pandas as pd

import matplotlib
matplotlib.use("Agg")  # HARUS sebelum pyplot
import matplotlib.pyplot as plt

from sklearn.tree import DecisionTreeClassifier, plot_tree, export_text
from sklearn.model_selection import train_test_split
from sklearn.metrics import accuracy_score

import state
from config import (
    BASE_DIR,
    DATASET_PATH,
    FEATURES,
    TARGET,
    LABEL_MAP,
    STATIC_DIR,
    THRESH_SOIL_NEED_WATER,
)
from utils import now_hms


# =======================
# LOG FILE PENGUJIAN
# =======================
# Folder khusus untuk log pengujian lapangan
LOG_DIR = os.path.join(BASE_DIR, "logs")
os.makedirs(LOG_DIR, exist_ok=True)

# File CSV utama yang dipakai di Bab 4
LOG_FILE = os.path.join(LOG_DIR, "log_pengujian.csv")

# Buat header log kalau file belum ada
if not os.path.exists(LOG_FILE):
    with open(LOG_FILE, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow([
            "timestamp",
            "zona",
            "suhu",
            "kelembaban_udara",
            "soilA",
            "soilB",
            "soil_avg",
            "intensitas_cahaya",
            "prediksi",
            "actual",
            "relay_cmd"
        ])

# =======================
# BATAS FREKUENSI LOGGER
# =======================
# Maksimal 2x per menit -> minimal interval 30 detik
LOG_INTERVAL_SEC = 30.0
LAST_LOG_TIME = 0.0  # diupdate setiap kali logger menulis ke CSV


def heuristic_actual_label(soil_percent: float):
    """
    Heuristik label aktual berdasarkan kelembaban tanah (%).
    1 = SIRAM jika kelembaban <= THRESH_SOIL_NEED_WATER
    0 = TIDAK SIRAM jika di atas threshold
    """
    if soil_percent is None:
        return None
    return 1 if soil_percent <= THRESH_SOIL_NEED_WATER else 0


def train_model():
    if not os.path.exists(DATASET_PATH):
        raise FileNotFoundError(f"Dataset tidak ditemukan: {DATASET_PATH}")

    df = pd.read_csv(DATASET_PATH)

    for col in FEATURES + [TARGET]:
        if col not in df.columns:
            raise ValueError(
                f"Kolom '{col}' tidak ada di dataset. Kolom tersedia: {list(df.columns)}"
            )

    X = df[FEATURES]
    y = df[TARGET]

    X_train, X_test, y_train, y_test = train_test_split(
        X, y,
        test_size=0.30,
        random_state=42,
        stratify=y if len(set(y)) > 1 else None
    )

    model = DecisionTreeClassifier(random_state=42)
    model.fit(X_train, y_train)

    acc = accuracy_score(y_test, model.predict(X_test))
    print("[ML] Model Accuracy (test):", round(acc, 4))

    # save tree image to static/tree.png
    os.makedirs(STATIC_DIR, exist_ok=True)
    plt.figure(figsize=(14, 7))
    plot_tree(
        model,
        feature_names=FEATURES,
        filled=True,
        rounded=True,
        class_names=["Tidak Siram", "Siram"]
    )
    plt.savefig(os.path.join(STATIC_DIR, "tree.png"), dpi=150)
    plt.close()

    rules_text = export_text(model, feature_names=FEATURES)
    return model, rules_text, float(acc)


def init_ml():
    model, rules, acc = train_model()
    state.model = model
    state.tree_rules_text = rules
    state.model_acc = acc


def collect_features_per_pot():
    """
    Menggabungkan sensor global (suhu, kelembaban, ldr) dan
    kelembaban tanah A/B per pot menjadi fitur per pot.
    """
    suhu = state.sensor_data.get("suhu")
    hum = state.sensor_data.get("kelembaban")
    ldr = state.sensor_data.get("ldr")

    # pastikan global sensor valid
    if None in (suhu, hum, ldr):
        return None

    pots = {}
    for i in range(1, 4):
        s1 = state.sensor_data.get(f"kelembaban_tanah_{i}A")
        s2 = state.sensor_data.get(f"kelembaban_tanah_{i}B")
        if None in (s1, s2):
            return None

        soil_avg = (float(s1) + float(s2)) / 2.0
        pots[f"pot_{i}"] = {
            "suhu": float(suhu),
            "kelembaban": float(hum),
            "kelembaban_tanah": float(soil_avg),
            "intensitas_cahaya": float(ldr),
        }
    return pots


def _should_send_relay(rid: int, desired: str, min_interval_sec: float = 1.5) -> bool:
    """
    Anti-spam relay: kirim hanya jika berubah, atau minimal interval tercapai.
    """
    desired = (desired or "OFF").upper().strip()
    now = time.time()

    # state.last_relay_sent harus ada di state.py
    last = float(state.last_relay_sent.get(rid, 0.0))
    current = (state.relay_state.get(rid, "OFF") or "OFF").upper().strip()

    if current != desired:
        return True
    if (now - last) >= float(min_interval_sec):
        return True
    return False


def maybe_infer_and_update(publish_relay_fn=None):
    """
    Dipanggil setiap sensor masuk. Jika data lengkap -> infer + update state + history.
    Jika mode AUTO -> kontrol relay (butuh publish_relay_fn).
    Sekarang juga LOGGING ke log_pengujian.csv untuk kebutuhan Bab 4
    dengan frekuensi maksimal 2x per menit.
    """
    global LAST_LOG_TIME

    if state.model is None:
        return

    pots = collect_features_per_pot()
    if not pots:
        return

    t = now_hms()

    preds = {}
    actuals = {}

    # ========================
    # 1. PREDIKSI PER POT
    # ========================
    for i in range(1, 4):
        pkey = f"pot_{i}"
        feats = pots[pkey]

        arr = np.array([feats[k] for k in FEATURES], dtype=float).reshape(1, -1)
        pred = int(state.model.predict(arr)[0])

        # predict_proba bisa error jika model hanya 1 kelas, jadi guard
        try:
            proba = state.model.predict_proba(arr)[0].tolist()
        except Exception:
            proba = [None, None]

        actual = heuristic_actual_label(feats["kelembaban_tanah"])
        if actual is None:
            actual = pred

        preds[i] = pred
        actuals[i] = int(actual)

        state.decision_info["pots"][pkey] = {
            "features": feats,
            "prediction": pred,
            "status": LABEL_MAP[pred],
            "proba": proba,
            "actual": int(actual),
        }

    state.decision_info["ready"] = True
    state.decision_info["updated_at"] = t

    # ========================
    # 2. REALTIME ACCURACY
    # ========================
    for i in (1, 2, 3):
        state.total_count += 1
        if preds[i] == actuals[i]:
            state.correct_count += 1

    acc_rt = round((state.correct_count / state.total_count) * 100, 2) if state.total_count else 0.0

    # ========================
    # 3. HISTORY UNTUK DASHBOARD
    # ========================
    state.history["time"].append(t)
    state.history["soil_1"].append(float(pots["pot_1"]["kelembaban_tanah"]))
    state.history["soil_2"].append(float(pots["pot_2"]["kelembaban_tanah"]))
    state.history["soil_3"].append(float(pots["pot_3"]["kelembaban_tanah"]))

    state.history["actual_1"].append(int(actuals[1]))
    state.history["actual_2"].append(int(actuals[2]))
    state.history["actual_3"].append(int(actuals[3]))

    state.history["pred_1"].append(int(preds[1]))
    state.history["pred_2"].append(int(preds[2]))
    state.history["pred_3"].append(int(preds[3]))

    state.history["acc"].append(float(acc_rt))

    # ========================
    # 4. LOGGER DATA PENGUJIAN
    # ========================
    # Batasi: hanya tulis log maksimal 2x per menit (interval ≥ 30 detik)
    now_ts = time.time()
    if (now_ts - LAST_LOG_TIME) >= LOG_INTERVAL_SEC:
        for rid in (1, 2, 3):
            pkey = f"pot_{rid}"
            feats = state.decision_info["pots"][pkey]["features"]
            pred = preds[rid]
            actual = actuals[rid]

            # command relay yang diinginkan model
            relay_cmd = "ON" if pred == 1 else "OFF"

            soilA = state.sensor_data.get(f"kelembaban_tanah_{rid}A")
            soilB = state.sensor_data.get(f"kelembaban_tanah_{rid}B")

            try:
                with open(LOG_FILE, "a", newline="", encoding="utf-8") as f:
                    writer = csv.writer(f)
                    writer.writerow([
                        datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                        rid,
                        feats["suhu"],
                        feats["kelembaban"],
                        soilA,
                        soilB,
                        feats["kelembaban_tanah"],
                        feats["intensitas_cahaya"],
                        pred,
                        actual,
                        relay_cmd
                    ])
            except Exception as e:
                # Jangan sampai logger bikin sistem mati, cukup print error
                print("[LOG] Gagal menulis log_pengujian:", e)

        LAST_LOG_TIME = now_ts  # update waktu terakhir logging

    # ========================
    # 5. AUTO RELAY CONTROL
    # ========================
    if state.control_mode.get("mode") == "auto" and publish_relay_fn is not None:
        for i in (1, 2, 3):
            desired = "ON" if preds[i] == 1 else "OFF"
            if _should_send_relay(i, desired, min_interval_sec=1.5):
                publish_relay_fn(i, desired)
                state.last_relay_sent[i] = time.time()
