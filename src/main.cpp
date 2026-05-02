
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
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "wifi_portal.h"

// ── Konfigurasi Dinamis (diambil dari Preferences) ─────────────────────────
String wifi_ssid = "";
String wifi_pass = "";
String api_url   = "https://ml-api-supabase.vercel.app";

#define API_KEY      "yuli1"
#define FW_VERSION   "6.3.1-fix"

// ── Pin ───────────────────────────────────────────────────────────────────
#define RELAY_PIN  25
#define DHT_PIN     4
#define SOIL_PIN   35

// Logika relay: aktif HIGH
#define RELAY_ON   HIGH
#define RELAY_OFF  LOW

// ── OLED ──────────────────────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── DHT22 ─────────────────────────────────────────────────────────────────
DHT dht(DHT_PIN, DHT22);

// ── Soil Moisture kalibrasi ───────────────────────────────────────────────
#define SOIL_DRY_ADC 2800
#define SOIL_WET_ADC 1200
#define SOIL_SAMPLES   10

// ── NTP (WIT = UTC+9) ─────────────────────────────────────────────────────
const char* ntpServer          = "pool.ntp.org";
const long  gmtOffset_sec      = 9 * 3600;
const int   daylightOffset_sec = 0;

// ── Timing ────────────────────────────────────────────────────────────────
const unsigned long SENSOR_READ_INTERVAL = 2000UL;   // baca sensor lokal (2 detik)
const unsigned long SENSOR_SEND_INTERVAL = 30000UL;  // kirim data ke /sensor (30 detik)
const unsigned long STATUS_INTERVAL      = 5000UL;   // poll /pump-status (5 detik)
const unsigned long DISPLAY_INTERVAL     = 1000UL;   // refresh OLED (1 detik)
const unsigned long WIFI_CHECK_MS        = 10000UL;  // cek koneksi WiFi
const unsigned long NTP_RESYNC_MS        = 3600000UL; // resync NTP tiap 1 jam


unsigned long lastSensorRead    = 0;
unsigned long lastSensorSend    = 0;  // [FIX] variabel baru khusus timer kirim
unsigned long lastStatusCheck   = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastWiFiCheck     = 0;
unsigned long lastNtpSync       = 0;

// ── Mode operasi ──────────────────────────────────────────────────────────
enum Mode { AUTO, MANUAL };
Mode mode = AUTO;

// ── State ─────────────────────────────────────────────────────────────────
bool   pumpOn         = false;
bool   wifiConnected  = false;
bool   ntpSynced      = false;
bool   manualOverride = false;

enum ApiStatus { API_UNKNOWN, API_OK, API_AUTH_ERR, API_ERR };
ApiStatus    apiStatus     = API_UNKNOWN;
unsigned long lastApiSuccess = 0;

String lastLabel      = "---";
float  lastConfidence = 0.0;
String lastAutoInfo   = "";

const char* namaHari[] = {"Mgg","Sen","Sel","Rab","Kam","Jum","Sab"};

// ── Data sensor ───────────────────────────────────────────────────────────
float temperature = NAN;
float airHumidity = NAN;
int   soilRaw     = 0;
float soilPct     = 0.0;

// ── Portal State ──────────────────────────────────────────────────────────
WebServer  server(80);
DNSServer  dnsServer;
Preferences prefs;
bool isPortalMode = false;


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

void loadSettings() {
  prefs.begin("siram-pintar", false);
  wifi_ssid = prefs.getString("ssid", "");
  wifi_pass = prefs.getString("pass", "");
  // Hapus kredensial dari NVS setelah dibaca (volatile — tidak permanen)
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
  Serial.println("[PREFS] WiFi settings loaded into RAM and cleared from NVS (Volatile).");
}

void saveSettings(String s, String p) {
  prefs.begin("siram-pintar", false);
  prefs.putString("ssid", s);
  prefs.putString("pass", p);
  prefs.end();
  Serial.println("[PREFS] Temporary WiFi settings saved for next boot.");
}

bool connectWiFi() {
  if (wifi_ssid == "") {
    Serial.println("[WiFi] SSID kosong, masuk mode portal.");
    return false;
  }

  Serial.printf("[WiFi] Connecting: %s\n", wifi_ssid.c_str());
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(2, 8);  display.println("Koneksi WiFi...");
  display.setCursor(2, 22); display.println(wifi_ssid);
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
    if (++attempt > 30) {
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

void startPortal() {
  isPortalMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("SiramPintar_Config", "");
  dnsServer.start(53, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", WIFI_PORTAL_HTML);
  });

  server.on("/scan", HTTP_GET, []() {
    int n = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < n; ++i) {
      JsonObject obj = arr.add<JsonObject>();
      obj["ssid"] = WiFi.SSID(i);
      obj["rssi"] = WiFi.RSSI(i);
    }
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/connect", HTTP_POST, []() {
    String s = server.arg("ssid");
    String p = server.arg("password");
    if (s != "") {
      saveSettings(s, p);
      server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"WiFi Disimpan! Restarting...\"}");
      delay(2000);
      ESP.restart();
    } else {
      server.send(400, "application/json", "{\"status\":\"err\",\"message\":\"SSID wajib diisi!\"}");
    }
  });

  server.onNotFound([]() {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", "");
  });

  server.begin();
  Serial.println("[PORTAL] Started at 192.168.4.1");

  display.clearDisplay();
  display.setCursor(0, 10); display.println("MODE KONFIGURASI");
  display.setCursor(0, 25); display.println("Hubungkan ke WiFi:");
  display.setCursor(0, 35); display.println("SiramPintar_Config");
  display.setCursor(0, 50); display.println("Buka: 192.168.4.1");
  display.display();
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
// API — GET /pump-status (polling ringan setiap 5 detik)
// ═══════════════════════════════════════════════════════════════════════════
void checkPumpStatus() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (ESP.getFreeHeap() < 25000) {
    Serial.println("[STATUS] Heap rendah, skip poll.");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(8);

  HTTPClient http;
  http.setTimeout(6000);
  http.begin(client, api_url + "/pump-status");
  http.addHeader("X-API-Key", API_KEY);

  int code = http.GET();

  if (code == 200) {
    String response = http.getString();
    JsonDocument res;

    if (!deserializeJson(res, response)) {
      bool   serverPump = res["pump_status"] | pumpOn;
      String sMode      = res["mode"] | (mode == AUTO ? "auto" : "manual");
      bool   overrideOn = res["manual_override"] | false;

      Mode newMode = (sMode == "auto") ? AUTO : MANUAL;
      if (newMode != mode) {
        mode = newMode;
        Serial.printf("[STATUS] Mode berubah → %s\n", mode == AUTO ? "AUTO" : "MANUAL");
      }

      manualOverride = overrideOn;

      if (serverPump != pumpOn) {
        setPump(serverPump);
        Serial.printf("[STATUS] pump_status sinkron dari server → %s\n",
                      serverPump ? "ON" : "OFF");
      }

      apiStatus      = API_OK;
      lastApiSuccess = millis();
    }

  } else if (code == 401) {
    apiStatus = API_AUTH_ERR;
    Serial.printf("[STATUS] 401 UNAUTHORIZED — periksa API_KEY '%s'\n", API_KEY);
  } else if (code > 0) {
    Serial.printf("[STATUS] Gagal, code: %d\n", code);
  }

  http.end();
}


// ═══════════════════════════════════════════════════════════════════════════
// API — POST /sensor (dikirim setiap 30 detik)
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
    http.begin(client, api_url + "/sensor");
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
          manualOverride = res["auto_info"]["manual_override"] | false;

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

  if (manualOverride && mode == AUTO) {
    display.print("|OVR");
  } else {
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
// SETUP
// ═══════════════════════════════════════════════════════════════════════════
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  Serial.println("\n=== SIRAM PINTAR ESP32 v" FW_VERSION " BOOTING ===");
  Serial.printf("[AUTH] API Key: %s\n", API_KEY);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);

  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);

  dht.begin();
  Wire.begin();
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[ERROR] OLED gagal!");
    while (true) delay(1000);
  }

  // Splash screen
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(14, 4);  display.println("SIRAM PINTAR");
  display.setCursor(14, 16); display.println("IoT + KNN");
  display.setCursor(14, 28); display.print("Firmware v"); display.println(FW_VERSION);
  display.setCursor(14, 42); display.println("Memuat...");
  display.display();
  delay(1500);

  loadSettings();

  wifiConnected = connectWiFi();
  if (wifiConnected) {
    syncNTP();
  } else {
    startPortal();
    return;
  }

  // Baca sensor awal
  delay(2000);
  for (int i = 0; i < 5; i++) {
    temperature = dht.readTemperature();
    airHumidity = dht.readHumidity();
    if (!isnan(temperature) && !isnan(airHumidity)) break;
    delay(500);
  }
  soilRaw = readSoilADC();
  soilPct = constrain(map(soilRaw, SOIL_DRY_ADC, SOIL_WET_ADC, 0, 100), 0, 100);

  // Kirim data awal & poll status
  if (wifiConnected) {
    sendToAPI();
    checkPumpStatus();
  }

  unsigned long now = millis();
  lastSensorRead = now;
  lastSensorSend = now;
}

void loop() {
  if (isPortalMode) {
    dnsServer.processNextRequest();
    server.handleClient();
    return;
  }

  unsigned long now = millis();

  handleWiFiReconnect();

  if (now - lastStatusCheck >= STATUS_INTERVAL) {
    lastStatusCheck = now;
    wifiConnected   = (WiFi.status() == WL_CONNECTED);
    if (wifiConnected) checkPumpStatus();
  }

  if (now - lastSensorRead >= SENSOR_READ_INTERVAL) {
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

  if (now - lastSensorSend >= SENSOR_SEND_INTERVAL) {
    lastSensorSend = now;
    wifiConnected  = (WiFi.status() == WL_CONNECTED);
    if (wifiConnected) sendToAPI();
  }

  if (now - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = now;
    wifiConnected     = (WiFi.status() == WL_CONNECTED);
    updateDisplay();
  }

  yield();
}