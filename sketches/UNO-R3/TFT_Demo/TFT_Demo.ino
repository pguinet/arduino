/*
 * TFT_Demo - Arduino UNO R3 + Shield TFT 3.5"
 *
 * Demo de l'ecran TFT 3.5" en parallele 8-bit (shield UNO).
 * Auto-detection du controleur, bandes de couleurs, formes,
 * texte de bienvenue pour le club Domontois.
 *
 * Board: Arduino UNO R3
 * FQBN: arduino:avr:uno
 *
 * @dependencies MCUFRIEND_kbv, Adafruit GFX Library
 */

#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>

MCUFRIEND_kbv tft;

#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define YELLOW  0xFFE0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define ORANGE  0xFD20

void splash(uint16_t id) {
  tft.fillScreen(BLACK);

  tft.setTextColor(CYAN);
  tft.setTextSize(3);
  tft.setCursor(40, 30);
  tft.print("Club Info");
  tft.setCursor(50, 60);
  tft.print("Domontois");

  tft.drawFastHLine(20, 100, tft.width() - 40, WHITE);

  tft.setTextColor(YELLOW);
  tft.setTextSize(2);
  tft.setCursor(20, 120);
  tft.print("Arduino UNO R3");
  tft.setCursor(20, 145);
  tft.print("+ TFT 3.5\"");

  tft.setTextColor(GREEN);
  tft.setTextSize(1);
  tft.setCursor(20, 180);
  tft.print("Driver detecte : 0x");
  tft.println(id, HEX);
  tft.setCursor(20, 195);
  tft.print("Resolution     : ");
  tft.print(tft.width());
  tft.print(" x ");
  tft.println(tft.height());
}

void colorBands() {
  uint16_t colors[] = {RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, MAGENTA};
  int n = 7;
  int h = tft.height() / n;
  for (int i = 0; i < n; i++) {
    tft.fillRect(0, i * h, tft.width(), h, colors[i]);
  }
}

void shapes() {
  tft.fillScreen(BLACK);
  for (int i = 0; i < 25; i++) {
    int x = random(tft.width());
    int y = random(tft.height());
    int r = random(10, 40);
    uint16_t c = tft.color565(random(256), random(256), random(256));
    tft.fillCircle(x, y, r, c);
  }
  for (int i = 0; i < 15; i++) {
    int x = random(tft.width() - 60);
    int y = random(tft.height() - 60);
    int w = random(20, 60);
    int h = random(20, 60);
    uint16_t c = tft.color565(random(256), random(256), random(256));
    tft.drawRect(x, y, w, h, c);
  }
}

void setup() {
  Serial.begin(9600);
  uint16_t id = tft.readID();
  Serial.print("TFT controller ID: 0x");
  Serial.println(id, HEX);

  if (id == 0x0 || id == 0xFFFF || id == 0xD3D3) {
    id = 0x9486;
  }
  tft.begin(id);
  tft.setRotation(1);
  randomSeed(analogRead(A5));

  splash(id);
}

void loop() {
  delay(4000);
  colorBands();
  delay(2500);
  shapes();
  delay(3000);

  uint16_t id = tft.readID();
  if (id == 0x0 || id == 0xFFFF || id == 0xD3D3) id = 0x9486;
  splash(id);
}
