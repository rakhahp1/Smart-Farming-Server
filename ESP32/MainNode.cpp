#include <WiFi.h>
#include <PubSubClient.h>
#include "DHT.h"
#include <LiquidCrystal_I2C.h>

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

// ====== TIMING (ANTI FLOOD) ======
const unsigned long SENSOR_PUB_MS = 1000;   // publish sensor tiap 1 detik
unsigned long lastSensorPub = 0;

// ====== FAILSAFE RELAY (ANTI STUCK ON) ======
// Jika relay ON lebih lama dari ini, otomatis dimatikan
const unsigned long MAX_RELAY_ON_MS = 30000; // 30 detik (sesuaikan kebutuhan)
unsigned long r1_on_since = 0;
unsigned long r2_on_since = 0;
unsigned long r3_on_since = 0;

// ====== LCD ======
void lcd_i2c(String text = "", int kolom = 0, int baris = 0) {
  byte bar[8] = {
    B11111, B11111, B11111, B11111, B11111, B11111, B11111,
  };
  if (text == "") {
    lcd.init();
    lcd.backlight();
    lcd.createChar(0, bar);
    lcd.setCursor(0, 0);
    lcd.print("Loading..");
    for (int i = 0; i < 16; i++) {
      lcd.setCursor(i, 1);
      lcd.write(byte(0));
      delay(100);
    }
    delay(50);
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

  Serial.print("Message [");
  Serial.print(topic);
  Serial.print("] = ");
  Serial.println(message);

  // Relay1
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

  // Relay2
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

  // Relay3
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

    // Last Will: jika ESP32 mati/putus, publish status offline (opsional)
    // (Topic bisa Anda pakai untuk monitoring)
    String willTopic = topicBase + "/mainnode_status";

    if (client.connect(clientId.c_str(), willTopic.c_str(), 1, true, "OFFLINE")) {
      debug("MQTT connected");

      // publish online retained
      client.publish(willTopic.c_str(), "ONLINE", true);

      // subscribe relay
      client.subscribe(relay1Path.c_str(), 1);
      client.subscribe(relay2Path.c_str(), 1);
      client.subscribe(relay3Path.c_str(), 1);

      // kecil saja test
      client.publish("esp/test", "Hello from MainNode", false);
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
  // pakai retain agar dashboard dapat nilai terakhir ketika refresh
  String payload = String(pesan, 2);
  client.publish(path.c_str(), payload.c_str(), retainFlag);
}

void baca_sensor() {
  kelembaban = dht.readHumidity();
  suhu = dht.readTemperature();
  ldr = constrain(100 - map(analogRead(pin_ldr), 0, 4095, 0, 100), 0, 100);
  kelembaban_tanah1A = constrain(100 - map(analogRead(pin_kelembaban_tanah1A), 2400, 4095, 0, 100), 0, 100);
  kelembaban_tanah2A = constrain(100 - map(analogRead(pin_kelembaban_tanah2A), 2400, 4095, 0, 100), 0, 100);
  kelembaban_tanah3A = constrain(100 - map(analogRead(pin_kelembaban_tanah3A), 2000, 4095, 0, 100), 0, 100);

  Serial.print("S:" + String(suhu) + " H:" + String(kelembaban) + " LDR:" + String(ldr));
  Serial.println(" KT1A:" + String(kelembaban_tanah1A) + " KT2A:" + String(kelembaban_tanah2A) + " KT3A:" + String(kelembaban_tanah3A));
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
  setupWiFi();

  client.setServer(mqttServer, mqttPort);
  client.setCallback(MQTTcallback);
  client.setKeepAlive(30);
  client.setSocketTimeout(10);

  dht.begin();

  // pin sensor
  pinMode(pin_ldr, INPUT);
  pinMode(pin_kelembaban_tanah1A, INPUT);
  pinMode(pin_kelembaban_tanah2A, INPUT);
  pinMode(pin_kelembaban_tanah3A, INPUT);

  // Relay output
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);

  // default relay mati (aktif LOW)
  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);
  digitalWrite(RELAY3, HIGH);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  // baca sensor selalu boleh
  baca_sensor();

  // publish sensor tiap 1 detik (anti flood)
  if (millis() - lastSensorPub >= SENSOR_PUB_MS) {
    send_mosquito(ldrPath, ldr, true);
    send_mosquito(tmpPath, suhu, true);
    send_mosquito(humPath, kelembaban, true);
    send_mosquito(humtPath1, kelembaban_tanah1A, true);
    send_mosquito(humtPath2, kelembaban_tanah2A, true);
    send_mosquito(humtPath3, kelembaban_tanah3A, true);
    lastSensorPub = millis();
    Serial.println("Sensor data published");
  }

  // failsafe anti stuck
  failsafe_check();

  // tampilan LCD (tetap)
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

  delay(5);
}
