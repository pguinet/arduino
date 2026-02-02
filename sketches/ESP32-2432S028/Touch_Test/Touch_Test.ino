/*
 * Touch_Test - ESP32-2432S028
 *
 * Test du tactile XPT2046. Affiche les coordonnées en temps réel.
 * Le tactile utilise un bus SPI séparé de l'écran.
 *
 * Board: ESP32-2432S028 (Cheap Yellow Display)
 * FQBN: esp32:esp32:esp32
 *
 * @dependencies TFT_eSPI, XPT2046_Touchscreen
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

// Backlight
#define TFT_BACKLIGHT 21

// Pins tactile XPT2046 (SPI séparé)
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK  25

// Bus SPI pour le tactile (VSPI avec pins personnalisés)
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

TFT_eSPI tft = TFT_eSPI();

// Calibration (à ajuster si nécessaire)
#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3800
#define TOUCH_MIN_Y 200
#define TOUCH_MAX_Y 3800

// Variables pour le dessin
uint16_t lastX = 0, lastY = 0;
bool drawing = false;

void setup() {
  Serial.begin(115200);
  Serial.println("Touch_Test - ESP32-2432S028");

  // Backlight
  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);

  // Initialiser l'écran
  tft.init();
  tft.setRotation(1);  // Paysage
  tft.fillScreen(TFT_BLACK);

  // Initialiser le SPI tactile avec les pins personnalisés
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  // Écran d'accueil
  drawUI();

  Serial.println("Pret - touche l'ecran!");
}

void drawUI() {
  tft.fillScreen(TFT_BLACK);

  // Titre
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 5);
  tft.println("Touch Test - CYD");

  // Zone de dessin
  tft.drawRect(0, 30, tft.width(), tft.height() - 60, TFT_DARKGREY);

  // Bouton Clear
  tft.fillRoundRect(10, tft.height() - 25, 80, 22, 5, TFT_RED);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(30, tft.height() - 19);
  tft.println("CLEAR");

  // Zone coordonnées
  tft.fillRoundRect(100, tft.height() - 25, 120, 22, 5, TFT_NAVY);

  // Palette de couleurs
  int paletteX = 230;
  int paletteY = tft.height() - 25;
  tft.fillRect(paletteX, paletteY, 20, 22, TFT_WHITE);
  tft.fillRect(paletteX + 22, paletteY, 20, 22, TFT_RED);
  tft.fillRect(paletteX + 44, paletteY, 20, 22, TFT_GREEN);
  tft.fillRect(paletteX + 66, paletteY, 20, 22, TFT_YELLOW);
}

uint16_t currentColor = TFT_WHITE;

void loop() {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();

    // Mapper les coordonnées brutes vers l'écran
    int x = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, tft.width());
    int y = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, tft.height());

    // Contraindre aux limites
    x = constrain(x, 0, tft.width() - 1);
    y = constrain(y, 0, tft.height() - 1);

    // Afficher les coordonnées
    tft.fillRoundRect(100, tft.height() - 25, 120, 22, 5, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextSize(1);
    tft.setCursor(105, tft.height() - 19);
    tft.printf("X:%3d Y:%3d Z:%d", x, y, p.z);

    Serial.printf("Touch: X=%d Y=%d (raw: %d,%d) Z=%d\n", x, y, p.x, p.y, p.z);

    // Vérifier si on touche le bouton Clear
    if (y > tft.height() - 30 && x < 100) {
      drawUI();
      delay(200);
      return;
    }

    // Vérifier si on touche la palette
    if (y > tft.height() - 30 && x >= 230) {
      int colorIdx = (x - 230) / 22;
      switch (colorIdx) {
        case 0: currentColor = TFT_WHITE; break;
        case 1: currentColor = TFT_RED; break;
        case 2: currentColor = TFT_GREEN; break;
        case 3: currentColor = TFT_YELLOW; break;
      }
      delay(100);
      return;
    }

    // Dessiner dans la zone de dessin
    if (y > 30 && y < tft.height() - 30) {
      if (drawing && abs(x - lastX) < 50 && abs(y - lastY) < 50) {
        tft.drawLine(lastX, lastY, x, y, currentColor);
      }
      tft.fillCircle(x, y, 2, currentColor);
      lastX = x;
      lastY = y;
      drawing = true;
    }
  } else {
    drawing = false;
  }

  delay(10);
}
