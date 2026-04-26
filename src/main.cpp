#include <Arduino.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ── Konfigurasi WiFi ───────────────────────────────────────────────────────
#define WIFI_SSID "TP"
#define WIFI_PASS "984038759"

// ── Konfigurasi API ────────────────────────────────────────────────────────
#define API_BASE_URL "https://ml-api-flax.vercel.app"

// !! GANTI DENGAN API KEY ANDA (5 karakter, huruf+angka)
// Harus sama persis dengan nilai API_KEY di environment Vercel
#define API_KEY "yuli1"

// ── Pin ────────────────────────────────────────────────────────────────────
// DIUBAH: RELAY_PIN dari 23 → 25 (sesuai hardware baru)
#define RELAY_PIN    25
#define DHT_PIN       4
#define SOIL_PIN     35
#define BTN_PIN      13
#define DEBOUNCE_MS  50

// DIUBAH: Definisi logika relay — HIGH = ON, LOW = OFF
// Kode lama pakai logika terbalik (LOW=ON, HIGH=OFF)
// Kode baru sesuai relay pin 25 yang aktif HIGH
#define RELAY_ON  HIGH
#define RELAY_OFF LOW

// ── OLED ───────────────────────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── DHT22 ──────────────────────────────────────────────────────────────────
DHT dht(DHT_PIN, DHT22);

// ── Soil Moisture kalibrasi ────────────────────────────────────────────────
#define SOIL_DRY_ADC 2800
#define SOIL_WET_ADC 1300
#define SOIL_SAMPLES   10

// ── NTP (WIT = UTC+9) ──────────────────────────────────────────────────────
const char* ntpServer        = "pool.ntp.org";
const long  gmtOffset_sec    = 9 * 3600;
const int   daylightOffset_sec = 0;

// ── Timing ─────────────────────────────────────────────────────────────────
const unsigned long SENSOR_INTERVAL  =  2000UL;
const unsigned long API_INTERVAL     = 30000UL;
const unsigned long DISPLAY_INTERVAL =  1000UL;
const unsigned long WIFI_CHECK_MS    = 10000UL;
const unsigned long NTP_RESYNC_MS    = 3600000UL;

unsigned long lastSensorRead   = 0;
unsigned long lastApiSend      = 0;
unsigned long lastDisplayUpdate= 0;
unsigned long lastBtnTime      = 0;
unsigned long lastWiFiCheck    = 0;
unsigned long lastNtpSync      = 0;
bool          lastBtnState     = HIGH;

// ── Mode operasi ───────────────────────────────────────────────────────────
enum Mode { AUTO, MANUAL };
Mode mode = AUTO;

// ── State ──────────────────────────────────────────────────────────────────
bool   pumpOn        = false;
bool   wifiConnected = false;
bool   ntpSynced     = false;

// Status koneksi API (termasuk auth)
enum ApiStatus { API_UNKNOWN, API_OK, API_AUTH_ERR, API_ERR };
ApiStatus apiStatus   = API_UNKNOWN;
unsigned long lastApiSuccess = 0;

String lastLabel     = "---";
float  lastConfidence= 0.0;
String lastAutoInfo  = "";

const char* namaHari[] = {"Mgg","Sen","Sel","Rab","Kam","Jum","Sab"};

// ── Data sensor ────────────────────────────────────────────────────────────
float temperature = NAN;
float airHumidity = NAN;
int   soilRaw     = 0;
float soilPct     = 0.0;

// ═══════════════════════════════════════════════════════════════════════════
// UTILITAS
// ═══════════════════════════════════════════════════════════════════════════
int readSoilADC() {
  long sum = 0;
  for (int i = 0; i < SOIL_SAMPLES; i++) {
    sum += analogRead(SOIL_PIN);
    delay(5);
  }
  return (int)(sum / SOIL_SAMPLES);
}

// DIUBAH: setPump() sekarang pakai RELAY_ON / RELAY_OFF
// Kode lama: digitalWrite(RELAY_PIN, on ? LOW : HIGH)  — logika terbalik
// Kode baru: digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF) — logika benar
void setPump(bool on) {
  if (on == pumpOn) return;
  pumpOn = on;
  digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF);
  Serial.printf("[POMPA] %s | Mode: %s | Tanah: %.1f%%\n",
                on ? "ON" : "OFF",
                mode == AUTO ? "AUTO" : "MANUAL",
                soilPct);
}

// ═══════════════════════════════════════════════════════════════════════════
// WIFI & NTP
// ═══════════════════════════════════════════════════════════════════════════
void syncNTP() {
  Serial.println("[NTP] Sinkronisasi waktu WIT...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 10000)) {
    ntpSynced   = true;
    lastNtpSync = millis();
    Serial.printf("[NTP] OK: %s %02d:%02d:%02d\n",
                  namaHari[timeinfo.tm_wday],
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else {
    ntpSynced = false;
    Serial.println("[NTP] Gagal — server akan fallback waktu sendiri.");
  }
}

bool connectWiFi() {
  Serial.printf("[WiFi] Connecting: %s\n", WIFI_SSID);
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(2, 8);  display.println("Koneksi WiFi...");
  display.setCursor(2, 22); display.println(WIFI_SSID);
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
    if (++attempt > 40) {
      Serial.println("\n[WiFi] Gagal!");
      display.clearDisplay();
      display.setCursor(10, 24); display.println("WiFi Gagal!");
      display.display();
      return false;
    }
  }
  Serial.printf("\n[WiFi] OK! IP: %s\n", WiFi.localIP().toString().c_str());
  display.clearDisplay();
  display.setCursor(10, 24); display.println("WiFi Terhubung!");
  display.display();
  delay(800);
  return true;
}

void handleWiFiReconnect() {
  unsigned long now = millis();
  if (WiFi.status() != WL_CONNECTED && (now - lastWiFiCheck >= WIFI_CHECK_MS)) {
    lastWiFiCheck = now;
    wifiConnected = false;
    ntpSynced     = false;
    Serial.println("[WiFi] Reconnecting...");
    WiFi.disconnect();
    WiFi.reconnect();
  }

  if (WiFi.status() == WL_CONNECTED && !wifiConnected) {
    wifiConnected = true;
    Serial.println("[WiFi] Reconnected! Resync NTP...");
    syncNTP();
  }

  if (wifiConnected && ntpSynced && (now - lastNtpSync >= NTP_RESYNC_MS)) {
    syncNTP();
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// API — /sensor  (dengan X-API-Key header)
// ═══════════════════════════════════════════════════════════════════════════
bool sendToAPI() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (ESP.getFreeHeap() < 30000) {
    Serial.println("[API] Heap rendah, skip kirim.");
    return false;
  }

  bool success = false;

  for (int attempt = 1; attempt <= 2; attempt++) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15);

    HTTPClient http;
    http.setTimeout(12000);
    http.begin(client, String(API_BASE_URL) + "/sensor");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-API-Key", API_KEY);

    JsonDocument doc;
    doc["soil_moisture"] = isnan(soilPct)     ? 0.0f : (float)(round(soilPct     * 10) / 10.0);
    doc["temperature"]   = isnan(temperature) ? 0.0f : (float)(round(temperature * 10) / 10.0);
    doc["air_humidity"]  = isnan(airHumidity) ? 0.0f : (float)(round(airHumidity * 10) / 10.0);

    struct tm timeinfo;
    if (ntpSynced && getLocalTime(&timeinfo)) {
      doc["hour"]   = timeinfo.tm_hour;
      doc["minute"] = timeinfo.tm_min;
      doc["day"]    = timeinfo.tm_wday;
    }

    String body;
    serializeJson(doc, body);
    Serial.printf("[API] POST /sensor: %s\n", body.c_str());

    int code = http.POST(body);

    if (code == 200) {
      String response = http.getString();
      JsonDocument res;

      if (!deserializeJson(res, response)) {
        if (!res["classification"]["label"].isNull()) {
          lastLabel      = res["classification"]["label"].as<String>();
          lastConfidence = res["classification"]["confidence"].as<float>();
        }
        if (!res["pump_status"].isNull())
          setPump(res["pump_status"].as<bool>());

        if (!res["mode"].isNull()) {
          String sMode = res["mode"].as<String>();
          mode = (sMode == "auto") ? AUTO : MANUAL;
        }

        if (!res["auto_info"].isNull()) {
          String reason  = res["auto_info"]["reason"].as<String>();
          String blocked = res["auto_info"]["blocked_reason"].as<String>();
          bool   raining = res["auto_info"]["is_raining"].as<bool>();
          int    rscore  = res["auto_info"]["rain_score"].as<int>();

          if (reason.length()  > 0) Serial.printf("[AUTO] %s\n", reason.c_str());
          if (blocked.length() > 0) Serial.printf("[BLOKIR] %s\n", blocked.c_str());
          if (raining)              Serial.printf("[HUJAN] skor=%d\n", rscore);

          lastAutoInfo = blocked.length() > 0 ? blocked : reason;
          if (lastAutoInfo.length() > 40) lastAutoInfo = lastAutoInfo.substring(0, 40);
        }

        if (!res["device_time"].isNull()) {
          Serial.printf("[WAKTU] Server: %s (src: %s)\n",
                        res["device_time"].as<String>().c_str(),
                        res["time_source"].as<String>().c_str());
        }
      }

      apiStatus      = API_OK;
      lastApiSuccess = millis();
      success        = true;
      Serial.printf("[API] OK | Pompa: %s | Label: %s (%.0f%%)\n",
                    pumpOn ? "ON" : "OFF", lastLabel.c_str(), lastConfidence);

    } else if (code == 401) {
      apiStatus = API_AUTH_ERR;
      Serial.printf("[API] 401 UNAUTHORIZED — periksa API_KEY '%s'\n", API_KEY);
      http.end();
      break;

    } else {
      apiStatus = API_ERR;
      Serial.printf("[API] Gagal attempt %d, code: %d\n", attempt, code);
    }

    http.end();
    if (success) break;
    if (attempt < 2) delay(3000);
  }
  return success;
}

// ═══════════════════════════════════════════════════════════════════════════
// API — /control  (dengan X-API-Key header)
// ═══════════════════════════════════════════════════════════════════════════
void sendControlToAPI(bool on, Mode requestedMode) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (ESP.getFreeHeap() < 30000) return;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15);

  HTTPClient http;
  http.setTimeout(12000);
  http.begin(client, String(API_BASE_URL) + "/control");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", API_KEY);

  JsonDocument doc;
  doc["action"] = on ? "on" : "off";
  doc["mode"]   = (requestedMode == MANUAL) ? "manual" : "auto";

  String body;
  serializeJson(doc, body);
  Serial.printf("[CONTROL] POST: %s\n", body.c_str());

  int code = http.POST(body);

  if (code == 200) {
    String response = http.getString();
    JsonDocument res;
    if (!deserializeJson(res, response)) {
      if (!res["pump_status"].isNull()) {
        bool serverPump = res["pump_status"].as<bool>();
        if (serverPump != pumpOn) {
          setPump(serverPump);
          Serial.printf("[CONTROL] Server koreksi pompa → %s\n", serverPump ? "ON" : "OFF");
        }
      }
      if (!res["mode"].isNull()) {
        String sMode = res["mode"].as<String>();
        mode = (sMode == "auto") ? AUTO : MANUAL;
      }
      bool debounced = res["debounced"] | false;
      if (debounced) Serial.println("[CONTROL] Perintah duplikat diabaikan server.");
      apiStatus = API_OK;
    }
  } else if (code == 401) {
    apiStatus = API_AUTH_ERR;
    Serial.println("[CONTROL] 401 UNAUTHORIZED — periksa API_KEY!");
  } else {
    Serial.printf("[CONTROL] Gagal, code: %d\n", code);
  }
  http.end();
}

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY
// ═══════════════════════════════════════════════════════════════════════════
void updateDisplay() {
  display.clearDisplay();

  struct tm timeinfo;
  bool hasTime = ntpSynced && getLocalTime(&timeinfo);
  char timeStr[16];
  if (hasTime)
    sprintf(timeStr, "%s %02d:%02d", namaHari[timeinfo.tm_wday],
            timeinfo.tm_hour, timeinfo.tm_min);
  else
    strcpy(timeStr, "No NTP");

  // ── 1. Header bar ──────────────────────────────────────────────────────
  display.fillRect(0, 0, 128, 12, WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print(timeStr);

  display.setCursor(100, 2);
  switch (apiStatus) {
    case API_OK:       display.print("OK");  break;
    case API_AUTH_ERR: display.print("401"); break;
    case API_ERR:      display.print("ERR"); break;
    default:           display.print(wifiConnected ? "W" : "--"); break;
  }

  // ── 2. Suhu & RH ──────────────────────────────────────────────────────
  display.setTextColor(WHITE);
  display.setCursor(2, 15);
  display.print("T:");
  if (isnan(temperature)) display.print("ERR");
  else { display.print(temperature, 1); display.print("C"); }

  display.setCursor(68, 15);
  display.print("RH:");
  if (isnan(airHumidity)) display.print("ERR");
  else { display.print(airHumidity, 0); display.print("%"); }

  // ── 3. Tanah ──────────────────────────────────────────────────────────
  display.setCursor(2, 25);
  display.print("Tanah:");
  display.print(soilPct, 1);
  display.print("%");

  // ── 4. Progress bar tanah ─────────────────────────────────────────────
  display.drawRect(0, 34, 128, 6, WHITE);
  int fillW = (int)map(constrain((long)soilPct, 0, 100), 0, 100, 0, 124);
  if (fillW > 0) display.fillRect(2, 36, fillW, 2, WHITE);

  // ── 5. Mode & Status Pompa ────────────────────────────────────────────
  display.setCursor(2, 43);
  display.print(mode == AUTO ? "AUTO" : "MAN");
  display.print("|Pmp:");
  if (pumpOn) {
    display.fillRect(72, 42, 54, 9, WHITE);
    display.setTextColor(BLACK);
    display.setCursor(76, 43);
    display.print("ON  ");
    display.setTextColor(WHITE);
  } else {
    display.print("OFF");
  }

  // ── 6. Label KNN ──────────────────────────────────────────────────────
  display.setCursor(2, 54);
  display.print(lastLabel.substring(0, 7));
  display.print(" ");
  display.print((int)lastConfidence);
  display.print("%");

  // ── 7. Indikator koneksi API (pojok kanan bawah) ──────────────────────
  if (apiStatus == API_OK) {
    display.fillCircle(123, 58, 3, WHITE);
  } else if (apiStatus == API_AUTH_ERR) {
    display.drawLine(119, 54, 127, 62, WHITE);
    display.drawLine(127, 54, 119, 62, WHITE);
  } else {
    display.drawCircle(123, 58, 3, WHITE);
  }

  display.display();
}

// ═══════════════════════════════════════════════════════════════════════════
// BUTTON
// ═══════════════════════════════════════════════════════════════════════════
void checkButton() {
  bool      state = digitalRead(BTN_PIN);
  unsigned long now = millis();

  if (state == LOW && lastBtnState == HIGH && (now - lastBtnTime) > DEBOUNCE_MS) {
    lastBtnTime = now;
    mode = MANUAL;
    setPump(!pumpOn);
    sendControlToAPI(pumpOn, MANUAL);
    Serial.printf("[BTN] Toggle pompa → %s (MANUAL)\n", pumpOn ? "ON" : "OFF");
  }

  if (state == LOW && lastBtnState == LOW &&
      (now - lastBtnTime) > 2000 && mode == MANUAL) {
    lastBtnTime = now;
    mode = AUTO;
    Serial.println("[BTN] Tahan → kembali AUTO");
    sendControlToAPI(pumpOn, AUTO);
  }

  lastBtnState = state;
}

// ═══════════════════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════════════════
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  Serial.println("\n=== SIRAM PINTAR v6 BOOTING ===");
  Serial.printf("[AUTH] API Key: %s\n", API_KEY);

  pinMode(RELAY_PIN, OUTPUT);
  // DIUBAH: kondisi awal relay menggunakan RELAY_OFF (LOW)
  // Kode lama: digitalWrite(RELAY_PIN, HIGH) — sama artinya tapi tidak konsisten
  // Kode baru: pakai konstanta RELAY_OFF agar jelas dan konsisten
  digitalWrite(RELAY_PIN, RELAY_OFF);

  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);
  pinMode(BTN_PIN, INPUT_PULLUP);

  dht.begin();
  Wire.begin();
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[ERROR] OLED gagal!");
    while (true) delay(1000);
  }

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(18, 8);  display.println("SIRAM PINTAR");
  display.setCursor(12, 22); display.println("IoT + KNN v6");
  display.setCursor(20, 38); display.println("Memuat...");
  display.display();
  delay(1500);

  wifiConnected = connectWiFi();
  if (wifiConnected) syncNTP();

  delay(2000);
  for (int i = 0; i < 5; i++) {
    temperature = dht.readTemperature();
    airHumidity = dht.readHumidity();
    if (!isnan(temperature) && !isnan(airHumidity)) break;
    delay(500);
  }

  soilRaw = readSoilADC();
  soilPct = constrain(map(soilRaw, SOIL_DRY_ADC, SOIL_WET_ADC, 0, 100), 0, 100);
  delay(500);

  if (wifiConnected) sendToAPI();
}

// ═══════════════════════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  checkButton();
  handleWiFiReconnect();

  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) temperature = t;
    if (!isnan(h)) airHumidity = h;
    soilRaw = readSoilADC();
    soilPct = constrain(map(soilRaw, SOIL_DRY_ADC, SOIL_WET_ADC, 0, 100), 0, 100);

    if (mode == AUTO && WiFi.status() != WL_CONNECTED) {
      if (!pumpOn && soilPct <= 40.0f) setPump(true);
      else if (pumpOn && soilPct >= 70.0f) setPump(false);
    }
  }

  if (now - lastApiSend >= API_INTERVAL) {
    lastApiSend   = now;
    wifiConnected = (WiFi.status() == WL_CONNECTED);
    if (wifiConnected) sendToAPI();
  }

  if (now - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = now;
    wifiConnected = (WiFi.status() == WL_CONNECTED);
    updateDisplay();
  }

  yield();
}