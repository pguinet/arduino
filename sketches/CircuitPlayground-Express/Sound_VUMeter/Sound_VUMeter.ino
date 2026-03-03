/*
 * Sound_VUMeter - Circuit Playground Express
 *
 * VU-mètre avancé avec 3 modes d'affichage et peak hold.
 * Bouton A : changer de mode. Bouton B : ajuster la sensibilité.
 *
 * Modes :
 *   1. Barre classique (vert→jaune→rouge) avec peak hold
 *   2. Symétrique depuis le centre
 *   3. Arc-en-ciel réactif (couleur selon l'intensité)
 *
 * Board: Adafruit Circuit Playground Express
 * FQBN: adafruit:samd:adafruit_circuitplayground_m0
 *
 * @dependencies Adafruit_CircuitPlayground
 */

#include <Adafruit_CircuitPlayground.h>

#define NUM_LEDS     10
#define SAMPLE_MS    50
#define PEAK_DECAY   3     // Vitesse de descente du peak (frames)
#define SMOOTH       0.3   // Lissage exponentiel (0-1, plus bas = plus lisse)

// Seuils de sensibilité (min dB, max dB)
const int sensPresets[][2] = {
  {55, 85},   // Calme (bureau)
  {50, 95},   // Normal
  {45, 100},  // Fort (musique)
};
const int NUM_SENS = 3;
const char* sensNames[] = {"Calme", "Normal", "Fort"};

int mode = 0;
int sensitivity = 1;  // Normal par défaut
float smoothLevel = 0;
int peakLed = 0;
int peakHold = 0;

bool lastBtnA = false;
bool lastBtnB = false;

// Couleur dégradé vert → jaune → rouge pour une position 0-9
void gradientColor(int pos, uint8_t &r, uint8_t &g, uint8_t &b) {
  float t = pos / 9.0;
  if (t < 0.5) {
    r = (uint8_t)(255 * t * 2);
    g = 255;
  } else {
    r = 255;
    g = (uint8_t)(255 * (1.0 - t) * 2);
  }
  b = 0;
}

void setup() {
  CircuitPlayground.begin();
  CircuitPlayground.setBrightness(30);
  Serial.begin(115200);
  Serial.println("Sound VUMeter - Circuit Playground Express");
  Serial.println("Bouton A: mode | Bouton B: sensibilite");
  printStatus();
}

void printStatus() {
  Serial.print("Mode: ");
  Serial.print(mode + 1);
  Serial.print(" | Sensibilite: ");
  Serial.println(sensNames[sensitivity]);
}

void loop() {
  // Lecture boutons (front montant)
  bool btnA = CircuitPlayground.leftButton();
  bool btnB = CircuitPlayground.rightButton();

  if (btnA && !lastBtnA) {
    mode = (mode + 1) % 3;
    peakLed = 0;
    peakHold = 0;
    printStatus();
  }
  if (btnB && !lastBtnB) {
    sensitivity = (sensitivity + 1) % NUM_SENS;
    printStatus();
  }
  lastBtnA = btnA;
  lastBtnB = btnB;

  // Lecture du niveau sonore
  float sound = CircuitPlayground.mic.soundPressureLevel(SAMPLE_MS);
  int minDb = sensPresets[sensitivity][0];
  int maxDb = sensPresets[sensitivity][1];
  float normalized = constrain((sound - minDb) / (float)(maxDb - minDb), 0.0, 1.0);

  // Lissage exponentiel
  smoothLevel = smoothLevel * (1.0 - SMOOTH) + normalized * SMOOTH;

  int numLeds = (int)(smoothLevel * NUM_LEDS + 0.5);

  // Peak hold
  if (numLeds > peakLed) {
    peakLed = numLeds;
    peakHold = 0;
  } else {
    peakHold++;
    if (peakHold > PEAK_DECAY) {
      peakLed = max(peakLed - 1, 0);
      peakHold = 0;
    }
  }

  CircuitPlayground.clearPixels();

  switch (mode) {
    case 0: drawBarMode(numLeds); break;
    case 1: drawSymmetricMode(numLeds); break;
    case 2: drawRainbowMode(numLeds); break;
  }
}

// Mode 1 : Barre classique avec peak hold
void drawBarMode(int numLeds) {
  uint8_t r, g, b;
  for (int i = 0; i < numLeds && i < NUM_LEDS; i++) {
    gradientColor(i, r, g, b);
    CircuitPlayground.setPixelColor(i, r, g, b);
  }
  // Peak hold en blanc
  if (peakLed > 0 && peakLed <= NUM_LEDS) {
    CircuitPlayground.setPixelColor(peakLed - 1, 80, 80, 80);
  }
}

// Mode 2 : Symétrique depuis le centre
void drawSymmetricMode(int numLeds) {
  int half = numLeds / 2;
  uint8_t r, g, b;
  for (int i = 0; i < half && i < 5; i++) {
    gradientColor(i * 2, r, g, b);
    CircuitPlayground.setPixelColor(4 - i, r, g, b);  // Vers la gauche
    CircuitPlayground.setPixelColor(5 + i, r, g, b);  // Vers la droite
  }
}

// Mode 3 : Arc-en-ciel dont la teinte dépend de l'intensité
void drawRainbowMode(int numLeds) {
  if (numLeds == 0) return;
  // Teinte basée sur le niveau : bleu (calme) → vert → rouge (fort)
  uint8_t baseHue = map(numLeds, 0, NUM_LEDS, 160, 0);
  for (int i = 0; i < numLeds && i < NUM_LEDS; i++) {
    uint8_t hue = (baseHue + i * 5) % 256;
    uint32_t color = CircuitPlayground.colorWheel(hue);
    CircuitPlayground.setPixelColor(i, color);
  }
}
