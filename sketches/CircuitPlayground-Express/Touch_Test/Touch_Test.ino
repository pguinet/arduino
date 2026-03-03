/*
 * Touch_Test - Circuit Playground Express
 *
 * Test des pads capacitifs. Affiche les valeurs brutes sur le port série
 * et allume la LED correspondante quand un pad est touché.
 *
 * Board: Adafruit Circuit Playground Express
 * FQBN: adafruit:samd:adafruit_circuitplayground_m0
 *
 * @dependencies Adafruit_CircuitPlayground
 */

#include <Adafruit_CircuitPlayground.h>

#define THRESH 200

// Pads capacitifs disponibles : A1, A2, A3, A4, A5, A6, A7
// Correspondance LED approximative sur le cercle
struct Pad {
  uint8_t pin;
  const char* name;
  uint8_t led;  // LED la plus proche
};

const Pad pads[] = {
  { A1, "A1", 6 },
  { A2, "A2", 9 },
  { A3, "A3", 10 },
  { A4, "A4", 3 },
  { A5, "A5", 2 },
  { A6, "A6", 1 },
  { A7, "A7", 0 },
};
const int NUM_PADS = 7;

void setup() {
  CircuitPlayground.begin();
  CircuitPlayground.setBrightness(30);
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println("Touch Test - Circuit Playground Express");
  Serial.println("Touche les pads et regarde les valeurs.");
  Serial.println();
  Serial.print("Seuil actuel: ");
  Serial.println(THRESH);
  Serial.println();

  // En-tête
  for (int i = 0; i < NUM_PADS; i++) {
    Serial.print(pads[i].name);
    Serial.print("\t");
  }
  Serial.println();
}

void loop() {
  CircuitPlayground.clearPixels();

  for (int i = 0; i < NUM_PADS; i++) {
    uint16_t val = CircuitPlayground.readCap(pads[i].pin);
    Serial.print(val);
    Serial.print("\t");

    if (val > THRESH) {
      CircuitPlayground.setPixelColor(pads[i].led, 0, 255, 0);
    }
  }
  Serial.println();

  delay(100);
}
