# 🌱 Smart Farming Server (YOLOv8 + IoT)

Sistem Smart Farming berbasis Computer Vision dan IoT untuk mendeteksi kondisi tanaman selada secara real-time menggunakan model YOLOv8 serta monitoring sensor melalui dashboard web.

---

## 🚀 Fitur Utama

### 1. Monitoring Sensor (IoT)
- Suhu
- Kelembaban
- Intensitas cahaya (LDR)
- Data ditampilkan secara real-time pada dashboard

### 2. Deteksi Kondisi Tanaman
- Growing (Sedang tumbuh)
- Ready to Harvest (Siap panen)
- Damaged (Rusak)

Menggunakan model YOLOv8 berbasis deep learning.

### 3. Dashboard Web
- Visualisasi data sensor
- Monitoring kondisi tanaman
- Integrasi dengan backend Flask

---

## 🏗️ Arsitektur Sistem

Kamera / Sensor → ESP32 → Backend (Flask) → YOLOv8 → Dashboard Web

---

## ⚙️ Teknologi yang Digunakan

- Python (Flask)
- YOLOv8 (Ultralytics)
- ESP32 (IoT)
- HTML, CSS, JavaScript
- MQTT / HTTP Communication

---

## 📂 Struktur Folder
