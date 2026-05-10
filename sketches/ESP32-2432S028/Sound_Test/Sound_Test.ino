/*
 * Sound_Test - ESP32-2432S028 (Cheap Yellow Display)
 *
 * Sketch temporaire pour comparer 6 patterns d'alerte audio
 * (speaker GPIO26). Tap sur un bouton joue le pattern.
 * Sert a choisir le meilleur pattern "DOWN" pour Internet_Monitor.
 *
 * Board: ESP32 Dev Module (CYD)
 * FQBN: esp32:esp32:esp32
 *
 * @dependencies TFT_eSPI, XPT2046_Touchscreen
 */

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#define SPEAKER_PIN 26
#define TOUCH_CS  33
#define TOUCH_IRQ 36

#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3700
#define TOUCH_Y_MIN 240
#define TOUCH_Y_MAX 3800

#define COLOR_BG       0x0000
#define COLOR_HEADER   0x18C3
#define COLOR_TEXT     0xFFFF
#define COLOR_BTN      0x2945  // bleu fonce
#define COLOR_BTN_HI   0xE186  // rouge (highlight)

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

unsigned long lastTapMs = 0;
int playingPattern = -1;

// ---------- patterns ----------

void playTone(int freq, int durationMs) {
  ledcWriteTone(SPEAKER_PIN, freq);
  delay(durationMs);
  ledcWriteTone(SPEAKER_PIN, 0);
}

// 0 : pattern actuel - 3 bips 800Hz
void pattern0() {
  for (int i = 0; i < 3; i++) {
    playTone(800, 100);
    delay(80);
  }
}

// 1 : sirene police - alternance 800/400 Hz
void pattern1() {
  for (int i = 0; i < 6; i++) {
    playTone(800, 200);
    playTone(400, 200);
  }
}

// 2 : klaxon descendant - glissando 1500 -> 500 Hz, 3 fois
void pattern2() {
  for (int rep = 0; rep < 3; rep++) {
    for (int f = 1500; f >= 500; f -= 50) {
      ledcWriteTone(SPEAKER_PIN, f);
      delay(15);
    }
    ledcWriteTone(SPEAKER_PIN, 0);
    delay(150);
  }
}

// 3 : pulsations rapides - 1000Hz 50ms ON/OFF x 10
void pattern3() {
  for (int i = 0; i < 10; i++) {
    playTone(1000, 50);
    delay(50);
  }
}

// 4 : SOS morse - 800Hz
void pattern4() {
  // S : 3 points
  for (int i = 0; i < 3; i++) { playTone(800, 100); delay(100); }
  delay(200);
  // O : 3 traits
  for (int i = 0; i < 3; i++) { playTone(800, 300); delay(100); }
  delay(200);
  // S : 3 points
  for (int i = 0; i < 3; i++) { playTone(800, 100); delay(100); }
}

// 5 : escalade crescendo - 600 -> 1200 Hz montant
void pattern5() {
  for (int f = 600; f <= 1200; f += 50) {
    ledcWriteTone(SPEAKER_PIN, f);
    delay(40);
  }
  ledcWriteTone(SPEAKER_PIN, 0);
}

void playPattern(int n) {
  switch (n) {
    case 0: pattern0(); break;
    case 1: pattern1(); break;
    case 2: pattern2(); break;
    case 3: pattern3(); break;
    case 4: pattern4(); break;
    case 5: pattern5(); break;
  }
}

// ---------- UI ----------

const char* patternNames[6] = {
  "0: Actuel - 3 bips 800Hz",
  "1: Sirene police 800/400Hz",
  "2: Klaxon descendant 1500->500",
  "3: Pulsations rapides 1000Hz",
  "4: SOS morse",
  "5: Escalade crescendo 600->1200"
};

struct Btn { int y; };
const int BTN_X = 10;
const int BTN_W = 300;
const int BTN_H = 32;
const Btn buttons[6] = {{24}, {60}, {96}, {132}, {168}, {204}};

void drawButton(int idx, bool highlighted) {
  uint16_t color = highlighted ? COLOR_BTN_HI : COLOR_BTN;
  tft.fillRoundRect(BTN_X, buttons[idx].y, BTN_W, BTN_H, 6, color);
  tft.drawRoundRect(BTN_X, buttons[idx].y, BTN_W, BTN_H, 6, COLOR_TEXT);
  tft.setTextColor(COLOR_TEXT, color);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(patternNames[idx], BTN_X + BTN_W / 2, buttons[idx].y + BTN_H / 2, 2);
}

void drawAllButtons() {
  for (int i = 0; i < 6; i++) drawButton(i, i == playingPattern);
}

void mapTouch(int rawX, int rawY, int& sx, int& sy) {
  sx = map(rawX, TOUCH_X_MIN, TOUCH_X_MAX, 0, 320);
  sy = map(rawY, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 240);
}

int hitTest(int sx, int sy) {
  if (sx < BTN_X || sx > BTN_X + BTN_W) return -1;
  for (int i = 0; i < 6; i++) {
    if (sy >= buttons[i].y && sy < buttons[i].y + BTN_H) return i;
  }
  return -1;
}

// ---------- setup / loop ----------

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Sound Test ===");

  ledcAttach(SPEAKER_PIN, 1000, 8);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COLOR_BG);

  // Header
  tft.fillRect(0, 0, 320, 20, COLOR_HEADER);
  tft.setTextColor(COLOR_TEXT, COLOR_HEADER);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Sound Test - tap pour jouer", 160, 4, 2);

  touchSPI.begin(25, 39, 32, TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  drawAllButtons();
}

void loop() {
  if (ts.tirqTouched() && ts.touched()) {
    if (millis() - lastTapMs > 500) {
      lastTapMs = millis();
      TS_Point p = ts.getPoint();
      int sx, sy;
      mapTouch(p.x, p.y, sx, sy);
      int idx = hitTest(sx, sy);
      if (idx >= 0) {
        Serial.printf("Pattern %d : %s\n", idx, patternNames[idx]);
        playingPattern = idx;
        drawButton(idx, true);
        playPattern(idx);
        playingPattern = -1;
        drawButton(idx, false);
      }
    }
  }
  delay(20);
}
