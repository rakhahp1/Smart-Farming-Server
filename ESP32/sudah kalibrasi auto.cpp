#include <WiFi.h>
#include <PubSubClient.h>
#include "DHT.h"
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>

Preferences prefs;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ====== GPIO Relay ======
#define RELAY1 13
#define RELAY2 14
#define RELAY3 27

// pin sensor
int pin_dht11 = 5;
int pin_ldr = 33;
int pin_kelembaban_tanah1A = 34;
int pin_kelembaban_tanah2A = 35;
int pin_kelembaban_tanah3A = 32;

#define DHTTYPE DHT11
DHT dht(pin_dht11, DHTTYPE);

// nilai sensor
float suhu = 0;
float kelembaban = 0;
float ldr = 0;
float kelembaban_tanah1A = 0;
float kelembaban_tanah2A = 0;
float kelembaban_tanah3A = 0;

// ====== KALIBRASI DHT11 (OFFSET + FILTER) ======
float dht_temp_offset = 0.0;  // + berarti menaikkan suhu
float dht_hum_offset  = 0.0;  // + berarti menaikkan kelembaban

float suhu_f = 0.0;
float kelembaban_f = 0.0;
const float ALPHA = 0.2; // smoothing 0.1-0.3

// ====== AUTO KALIBRASI ======
bool autoCalActive = false;
unsigned long autoCalStartMs = 0;
unsigned long autoCalDurationMs = 60000; // default 60 detik
unsigned long autoCalLastSampleMs = 0;
const unsigned long AUTO_SAMPLE_MS = 2000; // DHT11 aman tiap 1-2 detik

float autoRefTemp = 0.0;
float autoRefHum  = 0.0;

double sumT = 0.0;
double sumH = 0.0;
uint32_t nSample = 0;

// status relay (0=OFF,1=ON)
int kondisi_p1 = 0;
int kondisi_p2 = 0;
int kondisi_p3 = 0;

// ====== Konfigurasi WiFi ======
const char* ssid = "wifi-iot";
const char* password = "password-iot";

// ====== Konfigurasi MQTT ======
const char* mqttServer = "test.mosquitto.org";
const int mqttPort = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// ====== Topik MQTT ======
String topicBase = "SatriaSensors773546";

String ldrPath   = topicBase + "/ldr";
String tmpPath   = topicBase + "/suhu";
String humPath   = topicBase + "/kelembaban";
String humtPath1 = topicBase + "/kelembaban_tanah_1A";
String humtPath2 = topicBase + "/kelembaban_tanah_2A";
String humtPath3 = topicBase + "/kelembaban_tanah_3A";

String relay1Path = topicBase + "/relay1";
String relay2Path = topicBase + "/relay2";
String relay3Path = topicBase + "/relay3";

// ====== TOPIC KALIBRASI VIA MQTT ======
String calCmdPath    = topicBase + "/calibrate";        // subscribe command
String calStatusPath = topicBase + "/calibrate_status"; // publish status

// ====== TIMING (ANTI FLOOD) ======
const unsigned long SENSOR_PUB_MS = 1000;
unsigned long lastSensorPub = 0;

// ====== FAILSAFE RELAY (ANTI STUCK ON) ======
const unsigned long MAX_RELAY_ON_MS = 30000;
unsigned long r1_on_since = 0;
unsigned long r2_on_since = 0;
unsigned long r3_on_since = 0;

// ====== LCD ======
void lcd_i2c(String text = "", int kolom = 0, int baris = 0) {
  byte bar[8] = { B11111, B11111, B11111, B11111, B11111, B11111, B11111 };
  if (text == "") {
    lcd.init();
    lcd.backlight();
    lcd.createChar(0, bar);
    lcd.setCursor(0, 0);
    lcd.print("Loading..");
    for (int i = 0; i < 16; i++) {
      lcd.setCursor(i, 1);
      lcd.write(byte(0));
      delay(60);
    }
    lcd.clear();
  } else {
    lcd.setCursor(kolom, baris);
    lcd.print(text + "                ");
  }
}

void debug(String message, int row = 0, int clear = 1) {
  Serial.println(message);
  if (clear == 1) lcd.clear();
  lcd.setCursor(0, row);
  lcd.print(message);
}

// ====== MQTT helper publish status ======
void publishCalStatus(const String &msg, bool retainFlag = false) {
  client.publish(calStatusPath.c_str(), msg.c_str(), retainFlag);
  Serial.println("[CAL_STATUS] " + msg);
}

// ====== LOAD/SAVE KALIBRASI ======
void loadCalibration() {
  prefs.begin("calib", true);
  dht_temp_offset = prefs.getFloat("t_off", 0.0);
  dht_hum_offset  = prefs.getFloat("h_off", 0.0);
  prefs.end();

  Serial.println("=== CALIB LOADED ===");
  Serial.print("Temp offset: "); Serial.println(dht_temp_offset, 2);
  Serial.print("Hum  offset: "); Serial.println(dht_hum_offset, 2);
}

void saveCalibration() {
  prefs.begin("calib", false);
  prefs.putFloat("t_off", dht_temp_offset);
  prefs.putFloat("h_off", dht_hum_offset);
  prefs.end();

  Serial.println("=== CALIB SAVED ===");
  Serial.print("Temp offset: "); Serial.println(dht_temp_offset, 2);
  Serial.print("Hum  offset: "); Serial.println(dht_hum_offset, 2);
}

// ====== AUTO KALIBRASI CONTROL ======
void startAutoCal(float tRef, float hRef, uint32_t seconds) {
  autoRefTemp = tRef;
  autoRefHum  = hRef;

  autoCalDurationMs = (unsigned long)seconds * 1000UL;
  if (autoCalDurationMs < 10000UL) autoCalDurationMs = 10000UL; // min 10s

  sumT = 0.0;
  sumH = 0.0;
  nSample = 0;

  autoCalActive = true;
  autoCalStartMs = millis();
  autoCalLastSampleMs = 0;

  Serial.println("=== AUTO CAL START ===");
  Serial.print("Ref Temp: "); Serial.println(autoRefTemp, 2);
  Serial.print("Ref Hum : "); Serial.println(autoRefHum, 2);
  Serial.print("Duration: "); Serial.print(autoCalDurationMs / 1000); Serial.println(" s");
  Serial.println("Dekatkan DHT11 dg alat referensi, tunggu selesai...");

  publishCalStatus("AUTO_START refT=" + String(autoRefTemp, 2) +
                   " refH=" + String(autoRefHum, 2) +
                   " dur=" + String(autoCalDurationMs / 1000));

  // LCD info singkat
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("AUTO CAL...");
  lcd.setCursor(0, 1); lcd.print(String(autoCalDurationMs / 1000) + "s");
}

void cancelAutoCal(const String &reason = "") {
  autoCalActive = false;
  sumT = 0; sumH = 0; nSample = 0;
  autoCalLastSampleMs = 0;

  String msg = "AUTO_CANCELLED";
  if (reason.length()) msg += " reason=" + reason;
  publishCalStatus(msg);

  Serial.println("=== AUTO CAL CANCELLED ===");
}

void processAutoCal() {
  if (!autoCalActive) return;

  unsigned long now = millis();

  // sampling tiap 2 detik
  if (autoCalLastSampleMs == 0 || (now - autoCalLastSampleMs >= AUTO_SAMPLE_MS)) {
    autoCalLastSampleMs = now;

    float h_raw = dht.readHumidity();
    float t_raw = dht.readTemperature();

    if (!isnan(h_raw) && !isnan(t_raw)) {
      sumT += t_raw;
      sumH += h_raw;
      nSample++;

      Serial.print("[AUTO] sample#"); Serial.print(nSample);
      Serial.print(" t="); Serial.print(t_raw, 2);
      Serial.print(" h="); Serial.println(h_raw, 2);

      publishCalStatus("AUTO_PROGRESS n=" + String(nSample) +
                       " t=" + String(t_raw, 2) +
                       " h=" + String(h_raw, 2));
    } else {
      Serial.println("[AUTO] DHT read NaN, sample di-skip");
      publishCalStatus("AUTO_PROGRESS skip=NaN");
    }
  }

  // selesai?
  if (now - autoCalStartMs >= autoCalDurationMs) {
    autoCalActive = false;

    if (nSample < 3) {
      Serial.println("=== AUTO CAL FAILED ===");
      Serial.println("Sampel terlalu sedikit. Coba lagi (durasi lebih lama / cek wiring DHT).");
      publishCalStatus("AUTO_FAILED reason=too_few_samples n=" + String(nSample));
      return;
    }

    float meanT = (float)(sumT / (double)nSample);
    float meanH = (float)(sumH / (double)nSample);

    dht_temp_offset = autoRefTemp - meanT;
    dht_hum_offset  = autoRefHum  - meanH;

    saveCalibration();

    Serial.println("=== AUTO CAL DONE ===");
    Serial.print("Mean DHT Temp: "); Serial.println(meanT, 2);
    Serial.print("Mean DHT Hum : "); Serial.println(meanH, 2);
    Serial.print("New Temp off : "); Serial.println(dht_temp_offset, 2);
    Serial.print("New Hum  off : "); Serial.println(dht_hum_offset, 2);

    publishCalStatus("AUTO_DONE meanT=" + String(meanT, 2) +
                     " meanH=" + String(meanH, 2) +
                     " offT=" + String(dht_temp_offset, 2) +
                     " offH=" + String(dht_hum_offset, 2));

    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("AUTO DONE");
    lcd.setCursor(0, 1); lcd.print("offT:" + String(dht_temp_offset, 1));
    delay(1500);
  }
}

// ====== PARSE AUTO CMD ======
bool parseAutoCmd(const String &cmdIn, float &tRef, float &hRef, uint32_t &sec) {
  String cmd = cmdIn;
  cmd.trim();

  if (!(cmd.startsWith("AUTO") || cmd.startsWith("auto"))) return false;

  int p1 = cmd.indexOf(' ');
  if (p1 < 0) return false;

  String rest = cmd.substring(p1 + 1);
  rest.trim();

  int p2 = rest.indexOf(' ');
  if (p2 < 0) return false;

  String tStr = rest.substring(0, p2);
  String rest2 = rest.substring(p2 + 1);
  rest2.trim();

  int p3 = rest2.indexOf(' ');
  if (p3 < 0) {
    // tanpa detik -> default 60
    tRef = tStr.toFloat();
    hRef = rest2.toFloat();
    sec = 60;
    return true;
  } else {
    String hStr = rest2.substring(0, p3);
    String sStr = rest2.substring(p3 + 1);
    sStr.trim();

    tRef = tStr.toFloat();
    hRef = hStr.toFloat();
    int s = sStr.toInt();
    if (s <= 0) s = 60;
    sec = (uint32_t)s;
    return true;
  }
}

// ====== COMMAND KALIBRASI VIA SERIAL ======
void handleCalibSerial() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  // === DEBUG: bukti command masuk ===
  Serial.println(">>> CMD MASUK: [" + cmd + "]");

  if (cmd.length() == 0) return;

  if (cmd.equalsIgnoreCase("SHOW")) {
    Serial.print("Temp offset: "); Serial.println(dht_temp_offset, 2);
    Serial.print("Hum  offset: "); Serial.println(dht_hum_offset, 2);
    Serial.print("AutoCalActive: "); Serial.println(autoCalActive ? "YES" : "NO");
    return;
  }

  if (cmd.equalsIgnoreCase("SAVE")) {
    saveCalibration();
    return;
  }

  if (cmd.equalsIgnoreCase("RESET")) {
    dht_temp_offset = 0.0;
    dht_hum_offset  = 0.0;
    saveCalibration();
    Serial.println("Offset direset ke 0 dan disimpan.");
    publishCalStatus("MANUAL_RESET");
    return;
  }

  if (cmd.equalsIgnoreCase("CANCEL")) {
    if (autoCalActive) cancelAutoCal("serial_cancel");
    else Serial.println("AutoCal tidak aktif.");
    return;
  }

  // AUTO dari Serial
  float tRef = 0, hRef = 0;
  uint32_t sec = 60;
  if (parseAutoCmd(cmd, tRef, hRef, sec)) {
    if (autoCalActive) {
      Serial.println("AutoCal sedang berjalan. Tunggu selesai atau kirim CANCEL.");
      return;
    }
    startAutoCal(tRef, hRef, sec);
    return;
  }

  // manual offset T/H
  if (cmd.length() >= 2) {
    char type = toupper(cmd.charAt(0));
    float val = cmd.substring(1).toFloat();

    if (type == 'T') {
      dht_temp_offset = val;
      Serial.print("Set Temp offset = "); Serial.println(dht_temp_offset, 2);
      Serial.println("Ketik SAVE untuk simpan permanen.");
      publishCalStatus("MANUAL_SET offT=" + String(dht_temp_offset, 2));
      return;
    }
    if (type == 'H') {
      dht_hum_offset = val;
      Serial.print("Set Hum offset = "); Serial.println(dht_hum_offset, 2);
      Serial.println("Ketik SAVE untuk simpan permanen.");
      publishCalStatus("MANUAL_SET offH=" + String(dht_hum_offset, 2));
      return;
    }
  }

  Serial.println("Perintah tidak dikenal.");
  Serial.println("Manual: T+1.5, H-3, SHOW, SAVE, RESET, CANCEL");
  Serial.println("Auto  : AUTO 30.2 70 60");
}

// ====== WiFi ======
void setupWiFi() {
  debug("Connecting to ");
  debug(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  debug("\nWiFi connected");
  delay(1200);
  debug(WiFi.localIP().toString());
  delay(1200);
}

// ====== Callback MQTT ======
void MQTTcallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  message.trim();

  Serial.print("Message ["); Serial.print(topic); Serial.print("] = ");
  Serial.println(message);

  // ====== KALIBRASI VIA MQTT ======
  if (String(topic) == calCmdPath) {
    if (message.equalsIgnoreCase("CANCEL")) {
      if (autoCalActive) cancelAutoCal("mqtt_cancel");
      else publishCalStatus("AUTO_CANCELLED reason=not_active");
      return;
    }

    float tRef = 0, hRef = 0;
    uint32_t sec = 60;
    if (parseAutoCmd(message, tRef, hRef, sec)) {
      if (autoCalActive) {
        publishCalStatus("AUTO_REJECT reason=busy");
        return;
      }
      startAutoCal(tRef, hRef, sec);
      return;
    }

    publishCalStatus("AUTO_REJECT reason=bad_format payload=" + message);
    return;
  }

  // ====== Relay1 ======
  if (String(topic) == relay1Path) {
    if (message == "ON") {
      digitalWrite(RELAY1, LOW);
      kondisi_p1 = 1;
      r1_on_since = millis();
      Serial.println("Relay 1 ON");
    } else if (message == "OFF") {
      digitalWrite(RELAY1, HIGH);
      kondisi_p1 = 0;
      r1_on_since = 0;
      Serial.println("Relay 1 OFF");
    }
  }

  // ====== Relay2 ======
  if (String(topic) == relay2Path) {
    if (message == "ON") {
      digitalWrite(RELAY2, LOW);
      kondisi_p2 = 1;
      r2_on_since = millis();
      Serial.println("Relay 2 ON");
    } else if (message == "OFF") {
      digitalWrite(RELAY2, HIGH);
      kondisi_p2 = 0;
      r2_on_since = 0;
      Serial.println("Relay 2 OFF");
    }
  }

  // ====== Relay3 ======
  if (String(topic) == relay3Path) {
    if (message == "ON") {
      digitalWrite(RELAY3, LOW);
      kondisi_p3 = 1;
      r3_on_since = millis();
      Serial.println("Relay 3 ON");
    } else if (message == "OFF") {
      digitalWrite(RELAY3, HIGH);
      kondisi_p3 = 0;
      r3_on_since = 0;
      Serial.println("Relay 3 OFF");
    }
  }
}

// ====== MQTT Connect ======
void reconnect() {
  while (!client.connected()) {
    debug("Attempting MQTT...");
    String clientId = "SatriaSensors_ESP32_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    String willTopic = topicBase + "/mainnode_status";

    if (client.connect(clientId.c_str(), willTopic.c_str(), 1, true, "OFFLINE")) {
      debug("MQTT connected");

      client.publish(willTopic.c_str(), "ONLINE", true);

      client.subscribe(relay1Path.c_str(), 1);
      client.subscribe(relay2Path.c_str(), 1);
      client.subscribe(relay3Path.c_str(), 1);

      // subscribe kalibrasi
      client.subscribe(calCmdPath.c_str(), 1);

      client.publish("esp/test", "Hello from MainNode", false);
      publishCalStatus("READY topic_cmd=" + calCmdPath);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retry 3s");
      delay(3000);
    }
  }
}

// ====== Publish sensor ======
void send_mosquito(String path, float pesan, bool retainFlag = true) {
  String payload = String(pesan, 2);
  client.publish(path.c_str(), payload.c_str(), retainFlag);
}

void baca_sensor() {
  float h_raw = dht.readHumidity();
  float t_raw = dht.readTemperature();

  if (isnan(h_raw) || isnan(t_raw)) {
    if (!autoCalActive) Serial.println("DHT11 read failed (NaN). Keeping last values.");
  } else {
    float t_cal = t_raw + dht_temp_offset;
    float h_cal = h_raw + dht_hum_offset;
    h_cal = constrain(h_cal, 0.0, 100.0);

    if (suhu_f == 0.0 && kelembaban_f == 0.0) {
      suhu_f = t_cal;
      kelembaban_f = h_cal;
    } else {
      suhu_f = (ALPHA * t_cal) + ((1.0 - ALPHA) * suhu_f);
      kelembaban_f = (ALPHA * h_cal) + ((1.0 - ALPHA) * kelembaban_f);
    }

    suhu = suhu_f;
    kelembaban = kelembaban_f;
  }

  // sensor lain
  ldr = constrain(100 - map(analogRead(pin_ldr), 0, 4095, 0, 100), 0, 100);
  kelembaban_tanah1A = constrain(100 - map(analogRead(pin_kelembaban_tanah1A), 2400, 4095, 0, 100), 0, 100);
  kelembaban_tanah2A = constrain(100 - map(analogRead(pin_kelembaban_tanah2A), 2400, 4095, 0, 100), 0, 100);
  kelembaban_tanah3A = constrain(100 - map(analogRead(pin_kelembaban_tanah3A), 2000, 4095, 0, 100), 0, 100);

  // === penting: saat AUTO aktif, jangan spam Serial ===
  if (!autoCalActive) {
    Serial.print("S:" + String(suhu) + " H:" + String(kelembaban) + " LDR:" + String(ldr));
    Serial.println(" KT1A:" + String(kelembaban_tanah1A) + " KT2A:" + String(kelembaban_tanah2A) + " KT3A:" + String(kelembaban_tanah3A));
  }
}

// ====== LCD UI timing ======
int timer_tampilan = 3000;
unsigned long timer1 = 0;
int mode = 0;

// ====== FAILSAFE CHECK ======
void failsafe_check() {
  unsigned long now = millis();

  if (kondisi_p1 == 1 && r1_on_since > 0 && (now - r1_on_since > MAX_RELAY_ON_MS)) {
    digitalWrite(RELAY1, HIGH);
    kondisi_p1 = 0;
    r1_on_since = 0;
    Serial.println("FAILSAFE: Relay1 OFF (timeout)");
  }
  if (kondisi_p2 == 1 && r2_on_since > 0 && (now - r2_on_since > MAX_RELAY_ON_MS)) {
    digitalWrite(RELAY2, HIGH);
    kondisi_p2 = 0;
    r2_on_since = 0;
    Serial.println("FAILSAFE: Relay2 OFF (timeout)");
  }
  if (kondisi_p3 == 1 && r3_on_since > 0 && (now - r3_on_since > MAX_RELAY_ON_MS)) {
    digitalWrite(RELAY3, HIGH);
    kondisi_p3 = 0;
    r3_on_since = 0;
    Serial.println("FAILSAFE: Relay3 OFF (timeout)");
  }
}

void setup() {
  Serial.begin(115200);

  lcd_i2c();
  loadCalibration();

  Serial.println("=== DHT11 CALIBRATION MENU ===");
  Serial.println("Manual: T+1.5, H-3.0, SHOW, SAVE, RESET, CANCEL");
  Serial.println("Auto  : AUTO <t_ref> <h_ref> <seconds>");
  Serial.println("Example: AUTO 30.2 70 60");
  Serial.println("MQTT CMD topic: " + calCmdPath);
  Serial.println("MQTT STATUS topic: " + calStatusPath);
  Serial.println("CATATAN: Serial Monitor harus Newline/Both NL&CR");

  setupWiFi();

  client.setServer(mqttServer, mqttPort);
  client.setCallback(MQTTcallback);
  client.setKeepAlive(30);
  client.setSocketTimeout(10);

  dht.begin();

  pinMode(pin_ldr, INPUT);
  pinMode(pin_kelembaban_tanah1A, INPUT);
  pinMode(pin_kelembaban_tanah2A, INPUT);
  pinMode(pin_kelembaban_tanah3A, INPUT); // ✅ fix (sebelumnya ada yang dobel)

  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);

  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);
  digitalWrite(RELAY3, HIGH);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  // command dari Serial
  handleCalibSerial();

  // auto kalibrasi (kalau aktif)
  processAutoCal();

  // baca sensor
  baca_sensor();

  // publish tiap 1 detik
  if (millis() - lastSensorPub >= SENSOR_PUB_MS) {
    send_mosquito(ldrPath, ldr, true);
    send_mosquito(tmpPath, suhu, true);
    send_mosquito(humPath, kelembaban, true);
    send_mosquito(humtPath1, kelembaban_tanah1A, true);
    send_mosquito(humtPath2, kelembaban_tanah2A, true);
    send_mosquito(humtPath3, kelembaban_tanah3A, true);
    lastSensorPub = millis();

    if (!autoCalActive) Serial.println("Sensor data published");
  }

  failsafe_check();

  // tampilan LCD (jangan ganggu saat auto)
  if (!autoCalActive) {
    if (millis() - timer1 >= (unsigned long)timer_tampilan) {
      mode += 1;
      timer1 = millis();
    }
    if (mode >= 3) mode = 0;

    if (mode == 0) {
      debug("S:" + String(suhu) + " K:" + String(kelembaban));
      debug("ldr: " + String(ldr), 1, 0);
    } else if (mode == 1) {
      debug("KT1A:" + String(kelembaban_tanah1A));
      debug("KT2A:" + String(kelembaban_tanah2A), 1, 0);
    } else if (mode == 2) {
      debug("KT3A:" + String(kelembaban_tanah3A));
      debug("P1:" + String(kondisi_p1) + " P2:" + String(kondisi_p2) + " P3:" + String(kondisi_p3), 1, 0);
    }
  }

  delay(5);
}