/*
 * OLED Demo - HW-364B (ESP8266 + OLED integre)
 *
 * Demonstration de l'ecran OLED bicolore (jaune/bleu)
 * avec animation et uptime.
 *
 * Pins OLED sur HW-364B:
 *   SDA -> GPIO14 (D5)
 *   SCL -> GPIO12 (D6)
 *   Adresse I2C: 0x3C
 *
 * Board: NodeMCU 1.0 (ESP-12E Module)
 * FQBN: esp8266:esp8266:nodemcuv2
 *
 * @dependencies U8g2
 */

#include <U8g2lib.h>
#include <Wire.h>

// Configuration de l'ecran OLED SSD1306 128x64
// Software I2C avec les pins specifiques de la HW-364B
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(
  U8G2_R0,
  /* SCL */ 12,   // GPIO12 = D6
  /* SDA */ 14,   // GPIO14 = D5
  U8X8_PIN_NONE
);

int frame = 0;

// Ecran bicolore: 16 pixels jaunes en haut, reste en bleu
#define YELLOW_ZONE_HEIGHT 16

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nHW-364B OLED Demo");

  u8g2.begin();
}

void loop() {
  u8g2.clearBuffer();

  // === ZONE JAUNE (0-16 pixels) ===
  u8g2.setFont(u8g2_font_ncenB10_tr);
  const char* title = "HW-364B Demo";
  int titleX = (128 - u8g2.getStrWidth(title)) / 2;
  u8g2.drawStr(titleX, 13, title);

  // Ligne de separation
  u8g2.drawHLine(0, YELLOW_ZONE_HEIGHT, 128);

  // === ZONE BLEUE (16-64 pixels) ===
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(15, 30, "ESP8266 + OLED");

  // Animation: barre de progression
  int barWidth = (frame % 100) + 1;
  u8g2.drawFrame(14, 38, 100, 10);
  u8g2.drawBox(15, 39, barWidth, 8);

  // Compteur et uptime
  char buf[30];
  unsigned long sec = millis() / 1000;
  sprintf(buf, "Uptime: %02lu:%02lu", sec / 60, sec % 60);
  u8g2.drawStr(25, 58, buf);

  u8g2.sendBuffer();

  frame++;
  delay(50);
}
