/*
 * AccelDemo - Circuit Playground Express
 *
 * Demonstration de l'accelerometre avec affichage sur LEDs.
 *
 * Board: Adafruit Circuit Playground Express
 * FQBN: adafruit:samd:adafruit_circuitplayground_m0
 *
 * @dependencies Adafruit_CircuitPlayground
 */

#include <Adafruit_CircuitPlayground.h>

void setup() {
  CircuitPlayground.begin();
  CircuitPlayground.setBrightness(30);
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("Accelerometre - Circuit Playground Express");
  Serial.println("X\tY\tZ");
}

void loop() {
  float x = CircuitPlayground.motionX();
  float y = CircuitPlayground.motionY();
  float z = CircuitPlayground.motionZ();

  // Affichage série
  Serial.print(x, 2);
  Serial.print("\t");
  Serial.print(y, 2);
  Serial.print("\t");
  Serial.println(z, 2);

  // Affichage LEDs - les LEDs sont en cercle (0 en haut, 5 en bas)
  CircuitPlayground.clearPixels();

  // Calcul de l'angle d'inclinaison (0-9 pour les 10 LEDs)
  // atan2 donne l'angle, on le mappe sur les positions des LEDs
  float angle = atan2(y, x);
  int ledPos = (int)((angle + PI) / (2 * PI) * 10 + 2.5) % 10;

  // Intensité basée sur l'inclinaison (plus c'est penché, plus c'est lumineux)
  float tilt = sqrt(x * x + y * y);
  int brightness = constrain(map(tilt * 10, 0, 100, 20, 255), 20, 255);

  // Couleur selon l'axe Z (rouge = face vers bas, bleu = face vers haut)
  uint8_t red = (z < 0) ? map(constrain(-z * 25, 0, 255), 0, 255, 50, 255) : 0;
  uint8_t blue = (z > 0) ? map(constrain(z * 25, 0, 255), 0, 255, 50, 255) : 0;
  uint8_t green = 50;

  // Allume 3 LEDs autour de la position d'inclinaison
  for (int i = -1; i <= 1; i++) {
    int led = (ledPos + i + 10) % 10;
    int b = (i == 0) ? brightness : brightness / 3;
    CircuitPlayground.setPixelColor(led, (red * b) / 255, (green * b) / 255, (blue * b) / 255);
  }

  delay(50);
}
