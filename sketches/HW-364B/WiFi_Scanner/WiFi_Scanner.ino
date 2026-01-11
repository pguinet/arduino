/*
 * WiFi Scanner - HW-364B (ESP8266 + OLED integre)
 *
 * Scan des reseaux WiFi avec affichage sur l'ecran OLED.
 * Affiche le SSID, niveau de signal et indicateur de securite.
 *
 * Pins OLED sur HW-364B:
 *   SDA -> GPIO14 (D5)
 *   SCL -> GPIO12 (D6)
 *   Adresse I2C: 0x3C
 *
 * Board: NodeMCU 1.0 (ESP-12E Module)
 * FQBN: esp8266:esp8266:nodemcuv2
 */

#include <U8g2lib.h>
#include <Wire.h>
#include <ESP8266WiFi.h>

// Configuration de l'ecran OLED SSD1306 128x64
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(
  U8G2_R0,
  /* SCL */ 12,
  /* SDA */ 14,
  U8X8_PIN_NONE
);

// Ecran bicolore: 16 pixels jaunes en haut, reste en bleu
#define YELLOW_ZONE_HEIGHT 16

int networkCount = 0;
int displayOffset = 0;
unsigned long lastScan = 0;
#define SCAN_INTERVAL 10000  // Scan toutes les 10 secondes

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nHW-364B WiFi Scanner");

  u8g2.begin();

  // Mode station pour le scan
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  // Premier scan
  scanNetworks();
}

void scanNetworks() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  const char* title = "WiFi Scanner";
  int titleX = (128 - u8g2.getStrWidth(title)) / 2;
  u8g2.drawStr(titleX, 13, title);
  u8g2.drawHLine(0, YELLOW_ZONE_HEIGHT, 128);

  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(20, 40, "Scanning...");
  u8g2.sendBuffer();

  networkCount = WiFi.scanNetworks();
  lastScan = millis();
  displayOffset = 0;

  Serial.printf("Found %d networks\n", networkCount);
}

void drawSignalBars(int x, int y, int rssi) {
  // Convertir RSSI en niveau (0-4)
  int level;
  if (rssi >= -50) level = 4;
  else if (rssi >= -60) level = 3;
  else if (rssi >= -70) level = 2;
  else if (rssi >= -80) level = 1;
  else level = 0;

  // Dessiner les barres
  for (int i = 0; i < 4; i++) {
    int barHeight = 2 + i * 2;
    if (i < level) {
      u8g2.drawBox(x + i * 3, y + 8 - barHeight, 2, barHeight);
    } else {
      u8g2.drawFrame(x + i * 3, y + 8 - barHeight, 2, barHeight);
    }
  }
}

void loop() {
  // Re-scan periodique
  if (millis() - lastScan > SCAN_INTERVAL) {
    scanNetworks();
  }

  u8g2.clearBuffer();

  // === ZONE JAUNE ===
  u8g2.setFont(u8g2_font_ncenB10_tr);
  const char* title = "WiFi Scanner";
  int titleX = (128 - u8g2.getStrWidth(title)) / 2;
  u8g2.drawStr(titleX, 13, title);
  u8g2.drawHLine(0, YELLOW_ZONE_HEIGHT, 128);

  // === ZONE BLEUE ===
  u8g2.setFont(u8g2_font_6x10_tr);

  if (networkCount == 0) {
    u8g2.drawStr(20, 40, "No networks found");
  } else {
    // Afficher 4 reseaux max (espace disponible)
    int yPos = 28;
    for (int i = 0; i < 4 && (displayOffset + i) < networkCount; i++) {
      int idx = displayOffset + i;
      String ssid = WiFi.SSID(idx);
      int rssi = WiFi.RSSI(idx);

      // Tronquer le SSID si trop long
      if (ssid.length() > 16) {
        ssid = ssid.substring(0, 14) + "..";
      }

      // Indicateur de reseau securise
      const char* lock = (WiFi.encryptionType(idx) != ENC_TYPE_NONE) ? "*" : " ";

      char line[25];
      snprintf(line, sizeof(line), "%s%s", lock, ssid.c_str());
      u8g2.drawStr(0, yPos, line);

      // Barres de signal
      drawSignalBars(110, yPos - 8, rssi);

      yPos += 11;
    }

    // Indicateur de scroll
    char info[20];
    snprintf(info, sizeof(info), "%d/%d", displayOffset + 1, networkCount);
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 63, info);
  }

  u8g2.sendBuffer();

  // Faire defiler les reseaux
  delay(2000);
  if (networkCount > 4) {
    displayOffset++;
    if (displayOffset > networkCount - 4) {
      displayOffset = 0;
    }
  }
}
