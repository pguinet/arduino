/*
 * NTP Clock - HW-364B (ESP8266 + OLED integre)
 *
 * Horloge synchronisee via NTP avec interface web de configuration.
 * Affiche l'heure sur l'ecran OLED bicolore.
 *
 * Pins OLED sur HW-364B:
 *   SDA -> GPIO14 (D5)
 *   SCL -> GPIO12 (D6)
 *
 * Board: NodeMCU 1.0 (ESP-12E Module)
 * FQBN: esp8266:esp8266:nodemcuv2
 */

#include <U8g2lib.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <time.h>
#include <EEPROM.h>
#include "credentials.h"

// Configuration OLED
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(
  U8G2_R0,
  /* SCL */ 12,
  /* SDA */ 14,
  U8X8_PIN_NONE
);

#define YELLOW_ZONE_HEIGHT 16

// Serveur web
ESP8266WebServer server(80);

// Configuration (sauvegardee en EEPROM)
struct Config {
  int8_t timezone;      // Offset UTC en heures (-12 a +14)
  bool format24h;       // true = 24h, false = 12h
  char ntpServer[40];   // Serveur NTP
  uint8_t checksum;     // Validation
} config;

// Serveurs NTP
const char* defaultNtpServer = "pool.ntp.org";

// Variables
bool timeValid = false;
unsigned long lastDisplayUpdate = 0;

void loadConfig() {
  EEPROM.begin(sizeof(Config));
  EEPROM.get(0, config);

  // Verification checksum simple
  uint8_t check = config.timezone ^ config.format24h ^ strlen(config.ntpServer);
  if (check != config.checksum || config.timezone < -12 || config.timezone > 14) {
    // Config invalide, valeurs par defaut
    config.timezone = 1;  // Europe/Paris (hiver)
    config.format24h = true;
    strncpy(config.ntpServer, defaultNtpServer, sizeof(config.ntpServer));
    saveConfig();
  }
}

void saveConfig() {
  config.checksum = config.timezone ^ config.format24h ^ strlen(config.ntpServer);
  EEPROM.put(0, config);
  EEPROM.commit();
}

void setupTime() {
  configTime(config.timezone * 3600, 0, config.ntpServer);
}

void setupWiFi() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(15, 13, "NTP Clock");
  u8g2.drawHLine(0, YELLOW_ZONE_HEIGHT, 128);
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(10, 35, "Connexion WiFi...");
  u8g2.drawStr(10, 50, WIFI_SSID);
  u8g2.sendBuffer();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(15, 13, "NTP Clock");
    u8g2.drawHLine(0, YELLOW_ZONE_HEIGHT, 128);
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(10, 35, "Connecte!");
    u8g2.drawStr(10, 50, WiFi.localIP().toString().c_str());
    u8g2.sendBuffer();
    delay(2000);
  }
}

// === Pages Web ===

void handleRoot() {
  time_t now = time(nullptr);
  struct tm* ti = localtime(&now);

  char timeStr[10];
  char dateStr[20];

  if (config.format24h) {
    sprintf(timeStr, "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
  } else {
    int h = ti->tm_hour % 12;
    if (h == 0) h = 12;
    sprintf(timeStr, "%d:%02d:%02d %s", h, ti->tm_min, ti->tm_sec, ti->tm_hour >= 12 ? "PM" : "AM");
  }
  sprintf(dateStr, "%02d/%02d/%04d", ti->tm_mday, ti->tm_mon + 1, ti->tm_year + 1900);

  String html = R"(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta http-equiv="refresh" content="1">
  <title>NTP Clock - HW-364B</title>
  <style>
    body {
      font-family: -apple-system, sans-serif;
      display: flex;
      flex-direction: column;
      align-items: center;
      min-height: 100vh;
      margin: 0;
      background: linear-gradient(135deg, #0f0c29, #302b63, #24243e);
      color: white;
    }
    .clock {
      margin-top: 3rem;
      text-align: center;
    }
    .time {
      font-size: 4rem;
      font-weight: bold;
      font-family: 'Courier New', monospace;
      text-shadow: 0 0 20px rgba(255,255,255,0.3);
    }
    .date {
      font-size: 1.5rem;
      opacity: 0.8;
      margin-top: 0.5rem;
    }
    .config {
      margin-top: 2rem;
      background: rgba(255,255,255,0.1);
      padding: 2rem;
      border-radius: 1rem;
      width: 90%;
      max-width: 400px;
    }
    h2 { margin-top: 0; }
    label { display: block; margin: 1rem 0 0.3rem; }
    select, input {
      width: 100%;
      padding: 0.5rem;
      border-radius: 0.3rem;
      border: none;
      font-size: 1rem;
    }
    button {
      margin-top: 1.5rem;
      padding: 0.8rem 2rem;
      font-size: 1rem;
      border: none;
      border-radius: 0.5rem;
      background: #4CAF50;
      color: white;
      cursor: pointer;
      width: 100%;
    }
    button:hover { background: #45a049; }
    .info {
      margin-top: 2rem;
      font-size: 0.8rem;
      opacity: 0.5;
    }
  </style>
</head>
<body>
  <div class="clock">
    <div class="time">)" + String(timeStr) + R"(</div>
    <div class="date">)" + String(dateStr) + R"(</div>
  </div>

  <div class="config">
    <h2>Configuration</h2>
    <form action="/config" method="POST">
      <label>Fuseau horaire (UTC)</label>
      <select name="tz">
)";

  // Options timezone
  for (int tz = -12; tz <= 14; tz++) {
    html += "<option value=\"" + String(tz) + "\"";
    if (tz == config.timezone) html += " selected";
    html += ">UTC" + String(tz >= 0 ? "+" : "") + String(tz) + "</option>\n";
  }

  html += R"(
      </select>

      <label>Format</label>
      <select name="format">
        <option value="24")" + String(config.format24h ? " selected" : "") + R"(>24 heures</option>
        <option value="12")" + String(!config.format24h ? " selected" : "") + R"(>12 heures (AM/PM)</option>
      </select>

      <label>Serveur NTP</label>
      <input type="text" name="ntp" value=")" + String(config.ntpServer) + R"(">

      <button type="submit">Enregistrer</button>
    </form>
  </div>

  <div class="info">
    IP: )" + WiFi.localIP().toString() + R"( | RSSI: )" + String(WiFi.RSSI()) + R"( dBm
  </div>
</body>
</html>
)";

  server.send(200, "text/html", html);
}

void handleConfig() {
  if (server.hasArg("tz")) {
    config.timezone = server.arg("tz").toInt();
  }
  if (server.hasArg("format")) {
    config.format24h = (server.arg("format") == "24");
  }
  if (server.hasArg("ntp")) {
    server.arg("ntp").toCharArray(config.ntpServer, sizeof(config.ntpServer));
  }

  saveConfig();
  setupTime();

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleApi() {
  time_t now = time(nullptr);
  struct tm* ti = localtime(&now);

  String json = "{";
  json += "\"hour\":" + String(ti->tm_hour) + ",";
  json += "\"minute\":" + String(ti->tm_min) + ",";
  json += "\"second\":" + String(ti->tm_sec) + ",";
  json += "\"day\":" + String(ti->tm_mday) + ",";
  json += "\"month\":" + String(ti->tm_mon + 1) + ",";
  json += "\"year\":" + String(ti->tm_year + 1900) + ",";
  json += "\"timezone\":" + String(config.timezone) + ",";
  json += "\"format24h\":" + String(config.format24h ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}

void setupServer() {
  server.on("/", handleRoot);
  server.on("/config", HTTP_POST, handleConfig);
  server.on("/api/time", handleApi);
  server.begin();
}

void updateDisplay() {
  time_t now = time(nullptr);
  struct tm* ti = localtime(&now);

  // Verifier si l'heure est valide (annee > 2020)
  timeValid = (ti->tm_year > 120);

  u8g2.clearBuffer();

  // === ZONE JAUNE ===
  u8g2.setFont(u8g2_font_6x10_tr);

  // Date a gauche
  char dateStr[12];
  sprintf(dateStr, "%02d/%02d/%04d", ti->tm_mday, ti->tm_mon + 1, ti->tm_year + 1900);
  u8g2.drawStr(0, 12, dateStr);

  // Jour de la semaine a droite
  const char* jours[] = {"Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam"};
  u8g2.drawStr(105, 12, jours[ti->tm_wday]);

  u8g2.drawHLine(0, YELLOW_ZONE_HEIGHT, 128);

  // === ZONE BLEUE ===
  if (!timeValid) {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(10, 40, "Synchronisation...");
  } else {
    // Heure en grand
    u8g2.setFont(u8g2_font_logisoso28_tn);
    char timeStr[9];
    sprintf(timeStr, "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
    int x = (128 - u8g2.getStrWidth(timeStr)) / 2;
    u8g2.drawStr(x, 50, timeStr);

    // Format 12h : afficher AM/PM
    if (!config.format24h) {
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.drawStr(110, 55, ti->tm_hour >= 12 ? "PM" : "AM");
    }
  }

  // IP en bas
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 63, WiFi.localIP().toString().c_str());

  // Indicateur WiFi
  int rssi = WiFi.RSSI();
  int bars = (rssi > -50) ? 4 : (rssi > -60) ? 3 : (rssi > -70) ? 2 : (rssi > -80) ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    int h = 2 + i * 2;
    int x = 115 + i * 3;
    if (i < bars) u8g2.drawBox(x, 63 - h, 2, h);
    else u8g2.drawFrame(x, 63 - h, 2, h);
  }

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nNTP Clock - HW-364B");

  u8g2.begin();
  loadConfig();
  setupWiFi();
  setupTime();
  setupServer();

  Serial.print("Serveur web: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  server.handleClient();

  // Mise a jour affichage chaque seconde
  if (millis() - lastDisplayUpdate >= 1000) {
    lastDisplayUpdate = millis();
    updateDisplay();
  }
}
