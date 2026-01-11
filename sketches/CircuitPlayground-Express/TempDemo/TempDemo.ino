#include <Adafruit_CircuitPlayground.h>

void setup() {
  CircuitPlayground.begin();
  CircuitPlayground.setBrightness(30);
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("Temperature - Circuit Playground Express");
  Serial.println("Celsius\tFahrenheit");
}

void loop() {
  float tempC = CircuitPlayground.temperature();
  float tempF = tempC * 1.8 + 32;

  // Affichage série
  Serial.print(tempC, 1);
  Serial.print("\t");
  Serial.println(tempF, 1);

  // LEDs : dégradé selon la température (15-35°C)
  // Bleu = froid, Vert = confortable, Rouge = chaud
  CircuitPlayground.clearPixels();

  int numLeds = map(constrain(tempC, 15, 35), 15, 35, 1, 10);

  for (int i = 0; i < numLeds; i++) {
    uint8_t r, g, b;
    float ratio = (float)i / 9.0;

    if (ratio < 0.5) {
      // Bleu -> Vert
      b = 255 * (1 - ratio * 2);
      g = 255 * (ratio * 2);
      r = 0;
    } else {
      // Vert -> Rouge
      b = 0;
      g = 255 * (1 - (ratio - 0.5) * 2);
      r = 255 * ((ratio - 0.5) * 2);
    }
    CircuitPlayground.setPixelColor(i, r, g, b);
  }

  delay(500);
}
