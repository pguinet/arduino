#include <Adafruit_CircuitPlayground.h>

void setup() {
  CircuitPlayground.begin();
  CircuitPlayground.setBrightness(30);
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("Microphone - Circuit Playground Express");
  Serial.println("Niveau sonore (dB)");
}

void loop() {
  // Lecture du niveau sonore
  float sound = CircuitPlayground.mic.soundPressureLevel(50);

  // Affichage série
  Serial.println(sound, 1);

  // VU-mètre sur les LEDs (0-10 LEDs selon le niveau)
  // Plage typique : 50-100 dB
  int numLeds = map(constrain(sound, 50, 95), 50, 95, 0, 10);

  CircuitPlayground.clearPixels();
  for (int i = 0; i < numLeds; i++) {
    // Dégradé vert -> jaune -> rouge
    uint8_t r, g;
    if (i < 4) {
      r = 0; g = 255;           // Vert
    } else if (i < 7) {
      r = 255; g = 255;         // Jaune
    } else {
      r = 255; g = 0;           // Rouge
    }
    CircuitPlayground.setPixelColor(i, r, g, 0);
  }

  delay(50);
}
