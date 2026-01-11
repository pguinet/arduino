/*
 * Bitcoin Ticker - HW-364B (ESP8266 + OLED integre)
 *
 * Affiche le cours du Bitcoin en USD via l'API CoinGecko.
 * Mise a jour toutes les 60 secondes.
 *
 * Pins OLED sur HW-364B:
 *   SDA -> GPIO14 (D5)
 *   SCL -> GPIO12 (D6)
 *
 * Board: NodeMCU 1.0 (ESP-12E Module)
 * FQBN: esp8266:esp8266:nodemcuv2
 *
 * @dependencies U8g2, ArduinoJson
 */

#include <U8g2lib.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include "credentials.h"

// Configuration OLED
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(
  U8G2_R0,
  /* SCL */ 12,
  /* SDA */ 14,
  U8X8_PIN_NONE
);

#define YELLOW_ZONE_HEIGHT 16
#define UPDATE_INTERVAL 60000  // 60 secondes

// API CoinGecko (gratuite, sans cle)
const char* API_HOST = "api.coingecko.com";
const char* API_PATH = "/api/v3/simple/price?ids=bitcoin&vs_currencies=usd&include_24hr_change=true";

// Donnees Bitcoin
float btcPrice = 0;
float btcChange24h = 0;
bool dataValid = false;
unsigned long lastUpdate = 0;
char lastUpdateTime[10] = "--:--";

// WiFi client
WiFiClientSecure client;

void setupWiFi() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  const char* title = "BTC Ticker";
  int x = (128 - u8g2.getStrWidth(title)) / 2;
  u8g2.drawStr(x, 13, title);
  u8g2.drawHLine(0, YELLOW_ZONE_HEIGHT, 128);
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(5, 35, "Connexion WiFi...");
  u8g2.sendBuffer();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    u8g2.drawStr(5, 50, WiFi.localIP().toString().c_str());
    u8g2.sendBuffer();
    delay(1000);
  }

  // Configuration NTP pour l'heure
  configTime(1 * 3600, 0, "pool.ntp.org");
}

void fetchBitcoinPrice() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  // Afficher "Updating..." pendant la requete
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(100, 63, "...");
  u8g2.sendBuffer();

  client.setInsecure();  // Skip certificate verification

  HTTPClient https;

  String url = "https://" + String(API_HOST) + String(API_PATH);

  if (https.begin(client, url)) {
    int httpCode = https.GET();

    if (httpCode == HTTP_CODE_OK) {
      String payload = https.getString();

      // Parser JSON
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (!error) {
        btcPrice = doc["bitcoin"]["usd"].as<float>();
        btcChange24h = doc["bitcoin"]["usd_24h_change"].as<float>();
        dataValid = true;

        // Mettre a jour l'heure
        time_t now = time(nullptr);
        struct tm* ti = localtime(&now);
        sprintf(lastUpdateTime, "%02d:%02d", ti->tm_hour, ti->tm_min);

        Serial.printf("BTC: $%.2f (%.2f%%)\n", btcPrice, btcChange24h);
      } else {
        Serial.println("JSON parse error");
      }
    } else {
      Serial.printf("HTTP error: %d\n", httpCode);
    }

    https.end();
  }

  lastUpdate = millis();
}

void drawBitcoinIcon(int x, int y) {
  // Symbole Bitcoin simplifie
  u8g2.drawCircle(x + 8, y + 8, 8);
  u8g2.drawStr(x + 5, y + 12, "B");
  u8g2.drawVLine(x + 7, y + 1, 3);
  u8g2.drawVLine(x + 9, y + 1, 3);
  u8g2.drawVLine(x + 7, y + 13, 3);
  u8g2.drawVLine(x + 9, y + 13, 3);
}

void updateDisplay() {
  u8g2.clearBuffer();

  // === ZONE JAUNE ===
  u8g2.setFont(u8g2_font_ncenB10_tr);
  const char* title = "Bitcoin USD";
  int titleX = (128 - u8g2.getStrWidth(title)) / 2;
  u8g2.drawStr(titleX, 13, title);

  u8g2.drawHLine(0, YELLOW_ZONE_HEIGHT, 128);

  // === ZONE BLEUE ===
  if (!dataValid) {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(15, 40, "Chargement...");
  } else {
    // Prix en grand
    u8g2.setFont(u8g2_font_logisoso18_tn);
    char priceStr[15];

    if (btcPrice >= 100000) {
      sprintf(priceStr, "%.0f", btcPrice);
    } else if (btcPrice >= 10000) {
      sprintf(priceStr, "%.1f", btcPrice);
    } else {
      sprintf(priceStr, "%.2f", btcPrice);
    }

    // Ajouter le symbole $
    char fullPrice[20];
    sprintf(fullPrice, "$%s", priceStr);

    int priceX = (128 - u8g2.getStrWidth(fullPrice)) / 2;
    u8g2.drawStr(priceX, 42, fullPrice);

    // Variation 24h
    u8g2.setFont(u8g2_font_ncenB10_tr);
    char changeStr[15];
    if (btcChange24h >= 0) {
      sprintf(changeStr, "+%.2f%%", btcChange24h);
    } else {
      sprintf(changeStr, "%.2f%%", btcChange24h);
    }

    int changeX = (128 - u8g2.getStrWidth(changeStr)) / 2;
    u8g2.drawStr(changeX, 56, changeStr);

    // Fleche haut/bas
    int arrowX = changeX - 12;
    if (btcChange24h >= 0) {
      // Fleche vers le haut
      u8g2.drawTriangle(arrowX, 52, arrowX + 8, 52, arrowX + 4, 46);
    } else {
      // Fleche vers le bas
      u8g2.drawTriangle(arrowX, 46, arrowX + 8, 46, arrowX + 4, 52);
    }
  }

  // Derniere mise a jour
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 63, lastUpdateTime);

  // Indicateur WiFi
  int rssi = WiFi.RSSI();
  int bars = (rssi > -50) ? 4 : (rssi > -60) ? 3 : (rssi > -70) ? 2 : (rssi > -80) ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    int h = 2 + i * 2;
    int bx = 115 + i * 3;
    if (i < bars) u8g2.drawBox(bx, 63 - h, 2, h);
    else u8g2.drawFrame(bx, 63 - h, 2, h);
  }

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nBitcoin Ticker - HW-364B");

  u8g2.begin();
  setupWiFi();

  // Premier fetch
  fetchBitcoinPrice();
  updateDisplay();
}

void loop() {
  // Mise a jour periodique
  if (millis() - lastUpdate >= UPDATE_INTERVAL) {
    fetchBitcoinPrice();
  }

  updateDisplay();
  delay(1000);
}
