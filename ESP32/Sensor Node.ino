#include <WiFi.h>
#include <PubSubClient.h>

// ===== WIFI =====
const char* ssid = "wifi-iot";
const char* password = "password-iot";

// ===== MQTT =====
const char* mqttServer = "test.mosquitto.org";
const int mqttPort = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// ===== TOPIK MQTT =====
String topicBase = "SatriaSensors773546";

String humtPath1B = topicBase + "/kelembaban_tanah_1B";
String humtPath2B = topicBase + "/kelembaban_tanah_2B";
String humtPath3B = topicBase + "/kelembaban_tanah_3B";

// status topic (opsional)
String statusTopic = topicBase + "/sensornodeB_status";

// ===== PIN SENSOR (ADC1) =====
int pin_kt1B = 35;
int pin_kt2B = 33;
int pin_kt3B = 32;

// ===== NILAI SENSOR =====
float kelembaban_tanah1B = 0;
float kelembaban_tanah2B = 0;
float kelembaban_tanah3B = 0;

// ===== MQTT PUBLISH =====
void send_mosquito(String path, float pesan) {
  String payload = String(pesan, 2);
  client.publish(path.c_str(), payload.c_str(), true); // retain ON
}

// ===== WIFI CONNECT =====
void setupWiFi() {
  Serial.print("Connecting to WiFi ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.println(WiFi.localIP());
}

// ===== MQTT CONNECT =====
void reconnect() {
  while (!client.connected()) {
    Serial.println("Connecting MQTT...");

    String clientId = "ESP32_SensorNode_B_" + String((uint32_t)ESP.getEfuseMac(), HEX);

    // Last Will retained
    if (client.connect(clientId.c_str(), statusTopic.c_str(), 1, true, "OFFLINE")) {
      Serial.println("MQTT Connected!");
      client.publish(statusTopic.c_str(), "ONLINE", true);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retry 3s");
      delay(3000);
    }
  }
}

// ===== BACA SENSOR =====
void baca_sensor() {
  kelembaban_tanah1B = constrain(100 - map(analogRead(pin_kt1B), 2400, 4095, 0, 100), 0, 100);
  kelembaban_tanah2B = constrain(100 - map(analogRead(pin_kt2B), 2400, 4095, 0, 100), 0, 100);
  kelembaban_tanah3B = constrain(100 - map(analogRead(pin_kt3B), 2000, 4095, 0, 100), 0, 100);

  Serial.print("KT1-B: " + String(kelembaban_tanah1B));
  Serial.print(" | KT2-B: " + String(kelembaban_tanah2B));
  Serial.println(" | KT3-B: " + String(kelembaban_tanah3B));
}

unsigned long lastSend = 0;
const unsigned long SEND_MS = 2000;

void setup() {
  Serial.begin(115200);

  setupWiFi();
  client.setServer(mqttServer, mqttPort);
  client.setKeepAlive(30);
  client.setSocketTimeout(10);

  pinMode(pin_kt1B, INPUT);
  pinMode(pin_kt2B, INPUT);
  pinMode(pin_kt3B, INPUT);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  baca_sensor();

  if (millis() - lastSend >= SEND_MS) {
    send_mosquito(humtPath1B, kelembaban_tanah1B);
    send_mosquito(humtPath2B, kelembaban_tanah2B);
    send_mosquito(humtPath3B, kelembaban_tanah3B);
    lastSend = millis();
    Serial.println("Soil B published");
  }

  delay(5);
}
