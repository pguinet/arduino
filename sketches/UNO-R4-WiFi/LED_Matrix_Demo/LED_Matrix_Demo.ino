/*
 * LED_Matrix_Demo - Arduino UNO R4 WiFi
 *
 * Demonstration de la matrice LED 12x8 integree :
 * texte defilant, coeur qui bat, animation de chargement.
 *
 * Board: Arduino UNO R4 WiFi
 * FQBN: arduino:renesas_uno:unor4wifi
 *
 * @dependencies (aucune)
 */

#include "ArduinoGraphics.h"
#include "Arduino_LED_Matrix.h"

ArduinoLEDMatrix matrix;

const uint32_t heart_big[3] = {
  0x3184a448,
  0x42081100,
  0xa0040000
};

const uint32_t heart_small[3] = {
  0x01044a4,
  0x40811000,
  0x40040000
};

const uint32_t frames_load[][4] = {
  {0xe0000000, 0x00000000, 0x00000000, 66},
  {0x70000000, 0x00000000, 0x00000000, 66},
  {0x38000000, 0x00000000, 0x00000000, 66},
  {0x1c000000, 0x00000000, 0x00000000, 66},
  {0x0e000000, 0x00000000, 0x00000000, 66},
  {0x07000000, 0x00000000, 0x00000000, 66},
  {0x03800000, 0x00000000, 0x00000000, 66},
  {0x01c00000, 0x00000000, 0x00000000, 66},
  {0x00e00000, 0x00000000, 0x00000000, 66},
  {0x00700000, 0x00000000, 0x00000000, 66},
  {0x00380000, 0x00000000, 0x00000000, 66},
  {0x001c0000, 0x00000000, 0x00000000, 66}
};

void setup() {
  matrix.begin();
}

void showHeartbeat() {
  for (int i = 0; i < 3; i++) {
    matrix.loadFrame(heart_big);
    delay(180);
    matrix.loadFrame(heart_small);
    delay(120);
    matrix.loadFrame(heart_big);
    delay(180);
    delay(500);
  }
}

void showLoading() {
  for (int i = 0; i < 5; i++) {
    for (int f = 0; f < 12; f++) {
      matrix.loadFrame(frames_load[f]);
      delay(60);
    }
  }
}

void showText() {
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  matrix.textScrollSpeed(75);
  matrix.textFont(Font_5x7);
  matrix.beginText(0, 1, 0xFFFFFF);
  matrix.println("  Domontois  Arduino UNO R4");
  matrix.endText(SCROLL_LEFT);
  matrix.endDraw();
}

void loop() {
  showText();
  delay(500);
  showHeartbeat();
  delay(300);
  showLoading();
  delay(300);
}
