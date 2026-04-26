#include <Arduino.h>
#include "DHT.h"

// =========================
// PIN CONFIG
// =========================
#define DHTPIN 4
#define DHTTYPE DHT11

const int PIN_SOIL  = 34;  // YL-69 AO -> GPIO34 (ADC1)
const int PIN_LDR   = 35;  // LDR analog -> GPIO35 (ADC1)
const int PIN_RELAY = 26;  // pin relay (ubah sesuai wiring)

// Relay aktif LOW umumnya true. Jika relay kamu aktif HIGH, ubah jadi false.
const bool RELAY_ACTIVE_LOW = true;

// =========================
// RULE (SOIL UTAMA + BORDERLINE)
// =========================
const float SOIL_ON_BELOW      = 41.0;  // soil < 41  => ON
const float SOIL_BORDER_LOW    = 41.0;  // awal borderline
const float SOIL_BORDER_HIGH   = 45.0;  // akhir borderline
const float SOIL_FORCE_OFF_ABOVE = 45.0; // soil > 45 => OFF (biar stabil)

// Kondisi tambahan saat borderline
const float TEMP_ON_ABOVE      = 30.0;  // suhu > 30
const int   LIGHT_ON_ABOVE     = 60;    // cahaya > 60 (0-100)

// =========================
// SAMPLING CONFIG
// =========================
const unsigned long INTERVAL_MS = 2000;
const int SOIL_SAMPLES = 10;
const int LDR_SAMPLES  = 10;

// =========================
// KALIBRASI ADC -> PERSEN
// =========================
int SOIL_DRY_ADC = 3300;   // ADC tanah kering
int SOIL_WET_ADC = 1500;   // ADC tanah basah

int LDR_DARK_ADC   = 3500; // gelap
int LDR_BRIGHT_ADC = 500;  // terang

// =========================
// GLOBAL
// =========================
DHT dht(DHTPIN, DHTTYPE);
unsigned long lastPrint = 0;

// simpan kondisi relay terakhir agar tidak kedip
bool relayState = false;

// simpan nilai DHT terakhir valid
float lastTemp = NAN;
float lastHum  = NAN;

// ---------- utility ----------
static float clampf(float x, float a, float b) {
  if (x < a) return a;
  if (x > b) return b;
  return x;
}

static float adcToPercent(int adc, int adc_low, int adc_high) {
  if (adc_low == adc_high) return 0;

  float pct;
  if (adc_low < adc_high) {
    pct = (float)(adc - adc_low) * 100.0f / (float)(adc_high - adc_low);
  } else {
    pct = (float)(adc_low - adc) * 100.0f / (float)(adc_low - adc_high);
  }
  return clampf(pct, 0.0f, 100.0f);
}

static int readAdcAvg(int pin, int samples) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(5);
  }
  return (int)(sum / samples);
}

static void setRelay(bool on) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(PIN_RELAY, on ? LOW : HIGH);
  } else {
    digitalWrite(PIN_RELAY, on ? HIGH : LOW);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(PIN_SOIL, INPUT);
  pinMode(PIN_LDR, INPUT);

  pinMode(PIN_RELAY, OUTPUT);
  relayState = false;
  setRelay(relayState);

  dht.begin();

  // Header CSV
  Serial.println("suhu,kelembaban,soil_avg,intensitas_cahaya,label");

  // (hapus kalau mau output csv murni)
  Serial.println("# Rule:");
  Serial.println("# 1) soil < 41 => ON");
  Serial.println("# 2) soil 41-45 AND suhu>30 AND cahaya>60 => ON");
  Serial.println("# else OFF");
}

void loop() {
  unsigned long now = millis();
  if (now - lastPrint < INTERVAL_MS) return;
  lastPrint = now;

  // ====== BACA DHT (pendukung) ======
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (!isnan(t) && !isnan(h)) {
    lastTemp = t;
    lastHum  = h;
  }
  if (isnan(lastTemp)) lastTemp = -1;
  if (isnan(lastHum))  lastHum  = -1;

  // ====== BACA LDR ======
  int ldr_adc = readAdcAvg(PIN_LDR, LDR_SAMPLES);
  float light_pct = adcToPercent(ldr_adc, LDR_DARK_ADC, LDR_BRIGHT_ADC);
  int light_int = (int)round(light_pct);

  // ====== BACA SOIL (utama) ======
  int soil_adc = readAdcAvg(PIN_SOIL, SOIL_SAMPLES);
  float soil_pct = adcToPercent(soil_adc, SOIL_DRY_ADC, SOIL_WET_ADC);

  // ====== KEPUTUSAN (soil utama + borderline) ======
  // Soil sangat kering => ON
  if (soil_pct < SOIL_ON_BELOW) {
    relayState = true;
  }
  // Borderline: 41-45 => ON hanya jika suhu & cahaya tinggi
  else if (soil_pct >= SOIL_BORDER_LOW && soil_pct <= SOIL_BORDER_HIGH) {
    bool kondisiLapanganMendukung = (lastTemp > TEMP_ON_ABOVE) && (light_int > LIGHT_ON_ABOVE);
    relayState = kondisiLapanganMendukung ? true : false;
  }
  // Soil cukup basah => OFF
  else if (soil_pct > SOIL_FORCE_OFF_ABOVE) {
    relayState = false;
  }

  setRelay(relayState);
  int label = relayState ? 1 : 0;

  // ====== OUTPUT CSV ======
  int hum_int = (lastHum < 0) ? -1 : (int)round(lastHum);
  Serial.print(lastTemp, 1);  Serial.print(",");
  Serial.print(hum_int);      Serial.print(",");
  Serial.print(soil_pct, 1);  Serial.print(",");
  Serial.print(light_int);    Serial.print(",");
  Serial.println(label);

  // Debug raw ADC (kalibrasi)
  // Serial.print("# raw soil_adc="); Serial.print(soil_adc);
  // Serial.print(" raw ldr_adc=");  Serial.println(ldr_adc);
}
