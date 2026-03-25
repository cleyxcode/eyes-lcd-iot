#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>      // simpan WiFi ke flash (NVS)
#include <WebServer.h>        // web portal provisioning
#include <DNSServer.h>        // captive portal redirect
#include "wifi_portal.h"      // HTML halaman setup WiFi

// ── Konfigurasi API ────────────────────────────────────────────────────────
#define API_BASE_URL  "https://siram-pintar-api.onrender.com"

// ── Pin ────────────────────────────────────────────────────────────────────
#define RELAY_PIN     23
#define DHT_PIN       4
#define SOIL_PIN      35
#define BTN_PIN       13
#define DEBOUNCE_MS   50

// ── OLED ───────────────────────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── DHT22 ──────────────────────────────────────────────────────────────────
DHT dht(DHT_PIN, DHT22);

// ── Soil Moisture kalibrasi ────────────────────────────────────────────────
#define SOIL_DRY_ADC  3200
#define SOIL_WET_ADC  1200

// ── WiFi Provisioning ──────────────────────────────────────────────────────
#define AP_SSID       "Siram-Pintar-Setup"   // nama AP saat provisioning
#define AP_PASS       ""                     // kosong = terbuka (mudah diakses)
#define PREF_NS       "wifi"                 // namespace Preferences NVS
#define WIFI_TIMEOUT  20                     // detik timeout koneksi WiFi

Preferences preferences;
WebServer   webServer(80);
DNSServer   dnsServer;
bool        isProvisioningMode = false;

// ── Mode operasi ───────────────────────────────────────────────────────────
enum Mode { AUTO, MANUAL };
Mode mode = AUTO;

// ── State ──────────────────────────────────────────────────────────────────
bool    pumpOn         = false;
bool    wifiConnected  = false;
String  lastLabel      = "---";
float   lastConfidence = 0.0;

// ── Timing ─────────────────────────────────────────────────────────────────
unsigned long lastSensorRead    = 0;
unsigned long lastApiSend       = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastBtnTime       = 0;
bool          lastBtnState      = HIGH;

const unsigned long SENSOR_INTERVAL  = 2000;
const unsigned long API_INTERVAL     = 5UL * 60000UL;
const unsigned long DISPLAY_INTERVAL = 1000;

// ── Data sensor ────────────────────────────────────────────────────────────
float temperature  = NAN;
float airHumidity  = NAN;
int   soilRaw      = 0;
float soilPct      = 0.0;

// ═══════════════════════════════════════════════════════════════════════════
// WIFI PROVISIONING — Simpan / Muat Credentials
// ═══════════════════════════════════════════════════════════════════════════

void saveWifiCredentials(const String& ssid, const String& pass) {
  preferences.begin(PREF_NS, false);   // false = read-write
  preferences.putString("ssid", ssid);
  preferences.putString("pass", pass);
  preferences.end();
  Serial.printf("[NVS] Saved SSID: %s\n", ssid.c_str());
}

bool loadWifiCredentials(String& ssid, String& pass) {
  preferences.begin(PREF_NS, true);    // true = read-only
  ssid = preferences.getString("ssid", "");
  pass = preferences.getString("pass", "");
  preferences.end();
  return ssid.length() > 0;
}

void clearWifiCredentials() {
  preferences.begin(PREF_NS, false);
  preferences.clear();
  preferences.end();
  Serial.println("[NVS] Credentials cleared");
}

// ── Coba konek WiFi dengan credentials ───────────────────────────────────
bool tryConnect(const String& ssid, const String& pass) {
  Serial.printf("[WiFi] Connecting to: %s\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  for (int i = 0; i < WIFI_TIMEOUT * 2; i++) {
    delay(500);
    Serial.print(".");
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
      return true;
    }
  }
  Serial.println("\n[WiFi] Failed to connect.");
  WiFi.disconnect(true);
  return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// WEB SERVER HANDLERS — Captive Portal
// ═══════════════════════════════════════════════════════════════════════════

// Kirim halaman HTML utama
void handleRoot() {
  webServer.send_P(200, "text/html", WIFI_PORTAL_HTML);
}

// Redirect semua path tidak dikenal → portal (captive portal behavior)
void handleNotFound() {
  webServer.sendHeader("Location", "http://192.168.4.1", true);
  webServer.send(302, "text/plain", "");
}

// GET /scan — scan jaringan WiFi dan kembalikan JSON
void handleScan() {
  Serial.println("[Scan] Scanning WiFi networks...");
  int n = WiFi.scanNetworks(false, true);  // false=sync, true=show hidden

  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    // Escape karakter spesial di SSID untuk JSON
    String ssid = WiFi.SSID(i);
    ssid.replace("\\", "\\\\");
    ssid.replace("\"", "\\\"");
    json += "{\"ssid\":\"" + ssid + "\","
          + "\"rssi\":"    + WiFi.RSSI(i) + ","
          + "\"secure\":"  + (WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  json += "]";

  Serial.printf("[Scan] Found %d networks\n", n);
  webServer.send(200, "application/json", json);
}

// POST /connect — terima ssid + password, coba konek, simpan jika berhasil
void handleConnect() {
  if (!webServer.hasArg("ssid")) {
    webServer.send(400, "application/json", "{\"success\":false,\"message\":\"Parameter ssid diperlukan\"}");
    return;
  }

  String ssid = webServer.arg("ssid");
  String pass = webServer.arg("password");   // bisa kosong untuk jaringan terbuka

  if (ssid.isEmpty()) {
    webServer.send(400, "application/json", "{\"success\":false,\"message\":\"SSID tidak boleh kosong\"}");
    return;
  }

  Serial.printf("[Connect] Attempting SSID: %s\n", ssid.c_str());

  // Tampilkan di OLED selama proses koneksi
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(10, 8);  display.println("Menghubungkan ke:");
  display.setCursor(10, 22); display.println(ssid.substring(0, 18));
  display.setCursor(10, 40); display.println("Harap tunggu...");
  display.display();

  bool ok = tryConnect(ssid, pass);

  if (ok) {
    // Simpan credentials ke flash
    saveWifiCredentials(ssid, pass);

    String ip  = WiFi.localIP().toString();
    String res = "{\"success\":true,\"message\":\"Berhasil terhubung!\",\"ip\":\"" + ip + "\"}";
    webServer.send(200, "application/json", res);

    // Tampilkan sukses di OLED
    display.clearDisplay();
    display.setCursor(20, 8);  display.println("WiFi Terhubung!");
    display.setCursor(10, 22); display.println("IP: " + ip);
    display.setCursor(10, 38); display.println("Restart dalam 3s...");
    display.display();

    delay(3000);
    ESP.restart();  // restart → load credentials dari NVS → mode normal
  } else {
    String res = "{\"success\":false,\"message\":\"Gagal terhubung. Periksa password dan coba lagi.\"}";
    webServer.send(200, "application/json", res);

    // Kembali ke mode AP setelah gagal
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);

    display.clearDisplay();
    display.setCursor(10, 8);  display.println("Koneksi Gagal!");
    display.setCursor(10, 22); display.println("Periksa password");
    display.setCursor(10, 36); display.println("WiFi Anda.");
    display.setCursor(10, 50); display.println("AP: " + String(AP_SSID));
    display.display();
  }
}

// ── Tampilkan info provisioning di OLED ──────────────────────────────────
void showProvisioningOLED() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.fillRect(0, 0, 128, 12, WHITE);
  display.setTextColor(BLACK);
  display.setCursor(20, 2); display.println("SETUP MODE");
  display.setTextColor(WHITE);
  display.setCursor(2, 16); display.println("Hubungkan HP ke WiFi:");
  display.setTextSize(1);
  display.setCursor(2, 28); display.println(AP_SSID);
  display.drawLine(0, 38, 128, 38, WHITE);
  display.setCursor(2, 42); display.println("Buka browser:");
  display.setCursor(2, 52); display.println("192.168.4.1");
  display.display();
}

// ── Mulai mode provisioning ───────────────────────────────────────────────
void startProvisioningMode() {
  isProvisioningMode = true;
  Serial.printf("[Provision] Starting AP: %s\n", AP_SSID);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(500);

  IPAddress apIP(192, 168, 4, 1);
  Serial.printf("[Provision] AP IP: %s\n", apIP.toString().c_str());

  // DNS Captive Portal — semua domain → 192.168.4.1
  dnsServer.start(53, "*", apIP);

  // Daftarkan route web server
  webServer.on("/",        HTTP_GET,  handleRoot);
  webServer.on("/scan",    HTTP_GET,  handleScan);
  webServer.on("/connect", HTTP_POST, handleConnect);
  webServer.onNotFound(handleNotFound);
  webServer.begin();

  showProvisioningOLED();
  Serial.println("[Provision] Portal ready at 192.168.4.1");
}

// ═══════════════════════════════════════════════════════════════════════════
// OPERASI NORMAL — Sensor, API, OLED, Button
// ═══════════════════════════════════════════════════════════════════════════

void setPump(bool on) {
  if (on == pumpOn) return;
  pumpOn = on;
  digitalWrite(RELAY_PIN, on ? LOW : HIGH);
  Serial.printf("POMPA %s | Mode: %s | Tanah: %.1f%%\n",
                on ? "ON" : "OFF",
                mode == AUTO ? "AUTO" : "MANUAL",
                soilPct);
}

bool sendToAPI() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String(API_BASE_URL) + "/sensor";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["soil_moisture"] = soilPct;
  doc["temperature"]   = isnan(temperature) ? 0 : temperature;
  doc["air_humidity"]  = isnan(airHumidity)  ? 0 : airHumidity;

  String body;
  serializeJson(doc, body);
  int code = http.POST(body);

  if (code == 200) {
    String response = http.getString();
    JsonDocument res;
    if (!deserializeJson(res, response)) {
      lastLabel      = res["classification"]["label"].as<String>();
      lastConfidence = res["classification"]["confidence"].as<float>();
      if (mode == AUTO) {
        String action = res["pump_action"].as<String>();
        if (action == "on")  setPump(true);
        if (action == "off") setPump(false);
      }
      Serial.printf("API OK | Label: %s (%.1f%%)\n", lastLabel.c_str(), lastConfidence);
    }
    http.end();
    return true;
  }
  Serial.printf("API gagal: HTTP %d\n", code);
  http.end();
  return false;
}

void sendControlToAPI(bool on) {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String(API_BASE_URL) + "/control";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  JsonDocument doc;
  doc["action"] = on ? "on" : "off";
  doc["mode"]   = mode == MANUAL ? "manual" : "auto";
  String body;
  serializeJson(doc, body);
  http.POST(body);
  http.end();
}

void updateDisplay() {
  display.clearDisplay();
  display.fillRect(0, 0, 128, 12, WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print("POMPA:");
  display.print(pumpOn ? "ON " : "OFF");
  display.setCursor(58, 2);
  display.print(mode == AUTO ? "[AUTO]" : "[MAN] ");
  if (wifiConnected) { display.setCursor(112, 2); display.print("W"); }

  display.setTextColor(WHITE);
  display.setCursor(2, 14);
  if (isnan(temperature)) display.print("--.-C");
  else { display.print(temperature, 1); display.print("C"); }
  display.setCursor(68, 14);
  display.print("RH:");
  if (isnan(airHumidity)) display.print("--");
  else display.print(airHumidity, 0);
  display.print("%");

  display.drawLine(0, 24, 128, 24, WHITE);
  display.setCursor(2, 27);
  display.print("TANAH: ");
  display.print(soilPct, 1);
  display.print("%");

  display.drawRect(0, 37, 128, 10, WHITE);
  int fillW = constrain((int)map((long)soilPct, 0, 100, 0, 126), 0, 126);
  if (fillW > 0) display.fillRect(1, 38, fillW, 8, WHITE);

  display.setCursor(2, 50);
  display.print("KNN: ");
  display.print(lastLabel);
  if (lastConfidence > 0) {
    display.print(" ");
    display.print((int)lastConfidence);
    display.print("%");
  }
  display.display();
}

void checkButton() {
  bool state = digitalRead(BTN_PIN);
  unsigned long now = millis();

  if (state == LOW && lastBtnState == HIGH && (now - lastBtnTime) > DEBOUNCE_MS) {
    lastBtnTime = now;
    if (mode == AUTO) {
      mode = MANUAL;
      setPump(!pumpOn);
    } else {
      setPump(!pumpOn);
    }
    sendControlToAPI(pumpOn);
  }
  if (state == LOW && (now - lastBtnTime) > 2000 && mode == MANUAL) {
    mode = AUTO;
    Serial.println("Kembali ke mode AUTO");
  }
  lastBtnState = state;
}

// ═══════════════════════════════════════════════════════════════════════════
// SETUP & LOOP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== SIRAM PINTAR BOOTING ===");

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);  // relay OFF default (active-low)
  pinMode(SOIL_PIN, INPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);

  dht.begin();
  Wire.begin();
  Wire.setClock(400000);

  // OLED init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED gagal!");
    while (true);
  }

  // Splash screen
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(18, 8);  display.println("SIRAM PINTAR");
  display.setCursor(12, 22); display.println("IoT + KNN Model");
  display.setCursor(20, 38); display.println("Memuat...");
  display.display();
  delay(1000);

  // ── Cek credentials tersimpan ─────────────────────────────────────────
  String savedSSID, savedPass;
  bool hasCredentials = loadWifiCredentials(savedSSID, savedPass);

  if (hasCredentials) {
    Serial.printf("[Boot] Found saved SSID: %s — trying to connect\n", savedSSID.c_str());

    display.clearDisplay();
    display.setCursor(2, 8);  display.println("Menghubungkan WiFi:");
    display.setCursor(2, 22); display.println(savedSSID.substring(0, 20));
    display.setCursor(2, 38); display.println("Harap tunggu...");
    display.display();

    wifiConnected = tryConnect(savedSSID, savedPass);
  }

  if (!wifiConnected) {
    // ── Tidak ada credentials atau koneksi gagal → Provisioning Mode ──
    Serial.println("[Boot] No WiFi → starting provisioning portal");
    startProvisioningMode();
    return;   // loop() akan handle portal
  }

  // ── Koneksi berhasil → Mode Normal ────────────────────────────────────
  Serial.println("[Boot] WiFi OK → normal mode");

  // Warmup DHT22
  delay(2000);
  for (int i = 0; i < 5; i++) {
    temperature = dht.readTemperature();
    airHumidity = dht.readHumidity();
    if (!isnan(temperature) && !isnan(airHumidity)) break;
    delay(500);
  }

  soilRaw = analogRead(SOIL_PIN);
  soilPct = constrain(map(soilRaw, SOIL_DRY_ADC, SOIL_WET_ADC, 0, 100), 0, 100);

  Serial.printf("[Init] Suhu:%.1fC RH:%.0f%% Tanah:%.1f%%\n", temperature, airHumidity, soilPct);
  sendToAPI();
}

void loop() {
  // ── Mode Provisioning ─────────────────────────────────────────────────
  if (isProvisioningMode) {
    dnsServer.processNextRequest();   // captive portal DNS
    webServer.handleClient();         // web server HTTP
    return;
  }

  // ── Mode Normal ───────────────────────────────────────────────────────
  unsigned long now = millis();

  checkButton();

  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) temperature = t;
    if (!isnan(h)) airHumidity = h;
    soilRaw = analogRead(SOIL_PIN);
    soilPct = constrain(map(soilRaw, SOIL_DRY_ADC, SOIL_WET_ADC, 0, 100), 0, 100);

    // Fallback lokal jika WiFi putus
    if (mode == AUTO && WiFi.status() != WL_CONNECTED) {
      if (!pumpOn && soilPct < 40.0) setPump(true);
      else if (pumpOn && soilPct > 70.0) setPump(false);
    }
  }

  if (now - lastApiSend >= API_INTERVAL) {
    lastApiSend = now;
    wifiConnected = (WiFi.status() == WL_CONNECTED);
    sendToAPI();
  }

  if (now - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = now;
    wifiConnected = (WiFi.status() == WL_CONNECTED);
    updateDisplay();
  }

  yield();
}
