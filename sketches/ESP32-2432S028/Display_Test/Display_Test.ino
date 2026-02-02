/*
 * Display_Test - ESP32-2432S028
 *
 * Test de l'écran TFT ILI9341 2.8" 320x240.
 * Affiche des formes géométriques, du texte et un test de couleurs.
 *
 * Board: ESP32-2432S028 (Cheap Yellow Display)
 * FQBN: esp32:esp32:esp32
 *
 * @dependencies TFT_eSPI
 *
 * Note: Copier ../User_Setup.h dans le dossier de la bibliothèque TFT_eSPI
 *       avant de compiler (~/.arduino15/libraries/TFT_eSPI/User_Setup.h)
 */

#include <TFT_eSPI.h>
#include <SPI.h>

// Pin du rétroéclairage
#define TFT_BACKLIGHT 21

TFT_eSPI tft = TFT_eSPI();

// Couleurs personnalisées
#define DARK_BLUE    0x000F
#define LIGHT_BLUE   0x7D7C
#define ORANGE       0xFBE0
#define PINK         0xFE19
#define PURPLE       0x780F

void setup() {
  Serial.begin(115200);
  Serial.println("Display_Test - ESP32-2432S028");

  // Activer le rétroéclairage
  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);

  // Initialiser l'écran
  tft.init();
  tft.setRotation(1);  // Paysage, USB à droite
  tft.fillScreen(TFT_BLACK);

  Serial.println("Ecran initialise");
  Serial.printf("Resolution: %dx%d\n", tft.width(), tft.height());

  // Séquence de test
  testInfo();
  delay(2000);

  testColors();
  delay(2000);

  testShapes();
  delay(2000);

  testText();
  delay(2000);

  testGradient();
}

void loop() {
  // Animation continue : rectangles aléatoires
  testRandomRects();
  delay(50);
}

// Affiche les infos de l'écran
void testInfo() {
  tft.fillScreen(TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);

  tft.setCursor(10, 20);
  tft.println("ESP32-2432S028");

  tft.setCursor(10, 50);
  tft.println("Cheap Yellow Display");

  tft.setTextSize(1);
  tft.setCursor(10, 90);
  tft.printf("Resolution: %d x %d", tft.width(), tft.height());

  tft.setCursor(10, 110);
  tft.println("Driver: ILI9341");

  tft.setCursor(10, 130);
  tft.println("Interface: SPI (HSPI)");

  // Dessiner un cadre
  tft.drawRect(5, 5, tft.width() - 10, tft.height() - 10, TFT_CYAN);
  tft.drawRect(6, 6, tft.width() - 12, tft.height() - 12, TFT_CYAN);
}

// Test des couleurs primaires et secondaires
void testColors() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 5);
  tft.println("Test couleurs");

  int w = tft.width() / 4;
  int h = (tft.height() - 20) / 2;
  int y1 = 20;
  int y2 = 20 + h;

  // Première rangée
  tft.fillRect(0 * w, y1, w, h, TFT_RED);
  tft.fillRect(1 * w, y1, w, h, TFT_GREEN);
  tft.fillRect(2 * w, y1, w, h, TFT_BLUE);
  tft.fillRect(3 * w, y1, w, h, TFT_WHITE);

  // Deuxième rangée
  tft.fillRect(0 * w, y2, w, h, TFT_YELLOW);
  tft.fillRect(1 * w, y2, w, h, TFT_CYAN);
  tft.fillRect(2 * w, y2, w, h, TFT_MAGENTA);
  tft.fillRect(3 * w, y2, w, h, TFT_ORANGE);
}

// Test des formes géométriques
void testShapes() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 5);
  tft.println("Test formes");

  int cx = tft.width() / 2;
  int cy = tft.height() / 2 + 10;

  // Cercles concentriques
  for (int r = 10; r < 100; r += 15) {
    uint16_t color = tft.color565(r * 2, 255 - r * 2, 128);
    tft.drawCircle(cx, cy, r, color);
  }

  // Rectangle avec coins arrondis
  tft.drawRoundRect(10, 30, 80, 50, 10, TFT_YELLOW);
  tft.fillRoundRect(15, 35, 70, 40, 8, DARK_BLUE);

  // Triangle
  tft.fillTriangle(250, 30, 300, 100, 200, 100, TFT_GREEN);

  // Lignes
  for (int i = 0; i < 8; i++) {
    int x = 20 + i * 5;
    tft.drawLine(x, 180, x + 80, 220, TFT_CYAN);
  }
}

// Test des polices de texte
void testText() {
  tft.fillScreen(TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);

  tft.setTextSize(1);
  tft.setCursor(10, 10);
  tft.println("Taille 1: ABCDEFGHIJKLMNOPQRSTUVWXYZ");

  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("Taille 2: Hello World!");

  tft.setTextSize(3);
  tft.setCursor(10, 60);
  tft.setTextColor(TFT_YELLOW, TFT_DARKGREY);
  tft.println("Taille 3");

  tft.setTextSize(4);
  tft.setCursor(10, 100);
  tft.setTextColor(TFT_CYAN, TFT_DARKGREY);
  tft.println("Grande");

  // Texte avec différentes couleurs
  tft.setTextSize(2);
  tft.setCursor(10, 160);
  tft.setTextColor(TFT_RED);    tft.print("R");
  tft.setTextColor(TFT_ORANGE); tft.print("A");
  tft.setTextColor(TFT_YELLOW); tft.print("I");
  tft.setTextColor(TFT_GREEN);  tft.print("N");
  tft.setTextColor(TFT_CYAN);   tft.print("B");
  tft.setTextColor(TFT_BLUE);   tft.print("O");
  tft.setTextColor(TFT_MAGENTA);tft.print("W");

  // Afficher des caractères spéciaux
  tft.setTextSize(1);
  tft.setCursor(10, 200);
  tft.setTextColor(TFT_WHITE);
  tft.println("Symboles: @#$%^&*(){}[]<>+=");
}

// Test de dégradé
void testGradient() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 5);
  tft.println("Test degrade");

  // Dégradé horizontal rouge -> bleu
  for (int x = 0; x < tft.width(); x++) {
    uint8_t r = 255 - (x * 255 / tft.width());
    uint8_t b = x * 255 / tft.width();
    uint16_t color = tft.color565(r, 0, b);
    tft.drawFastVLine(x, 20, 70, color);
  }

  // Dégradé horizontal vert -> jaune
  for (int x = 0; x < tft.width(); x++) {
    uint8_t r = x * 255 / tft.width();
    uint16_t color = tft.color565(r, 255, 0);
    tft.drawFastVLine(x, 100, 70, color);
  }

  // Dégradé de gris
  for (int x = 0; x < tft.width(); x++) {
    uint8_t gray = x * 255 / tft.width();
    uint16_t color = tft.color565(gray, gray, gray);
    tft.drawFastVLine(x, 180, 50, color);
  }
}

// Animation de rectangles aléatoires
void testRandomRects() {
  int x = random(tft.width() - 40);
  int y = random(tft.height() - 40);
  int w = random(10, 40);
  int h = random(10, 40);
  uint16_t color = tft.color565(random(256), random(256), random(256));

  tft.fillRect(x, y, w, h, color);
}
