/*
 * RGB_LED_Test - ESP32-2432S028
 *
 * Test de la LED RGB intégrée. Défilement des couleurs et effet arc-en-ciel.
 * La LED est active LOW (LOW = allumé, HIGH = éteint).
 *
 * Board: ESP32-2432S028 (Cheap Yellow Display)
 * FQBN: esp32:esp32:esp32
 *
 * @dependencies (aucune)
 */

// Pins LED RGB (active LOW)
#define LED_R 4
#define LED_G 16
#define LED_B 17

// Canaux PWM
#define PWM_FREQ 5000
#define PWM_RES  8  // 8 bits = 0-255

void setup() {
  Serial.begin(115200);
  Serial.println("RGB_LED_Test - ESP32-2432S028");

  // Configurer les canaux PWM
  ledcAttach(LED_R, PWM_FREQ, PWM_RES);
  ledcAttach(LED_G, PWM_FREQ, PWM_RES);
  ledcAttach(LED_B, PWM_FREQ, PWM_RES);

  // Éteindre la LED au démarrage
  setRGB(0, 0, 0);

  Serial.println("Demarrage du test LED RGB...");
}

// Définir la couleur RGB (0-255 pour chaque composante)
// Inverse les valeurs car la LED est active LOW
void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(LED_R, 255 - r);
  ledcWrite(LED_G, 255 - g);
  ledcWrite(LED_B, 255 - b);
}

void loop() {
  Serial.println("--- Couleurs primaires ---");

  // Rouge
  Serial.println("Rouge");
  setRGB(255, 0, 0);
  delay(1000);

  // Vert
  Serial.println("Vert");
  setRGB(0, 255, 0);
  delay(1000);

  // Bleu
  Serial.println("Bleu");
  setRGB(0, 0, 255);
  delay(1000);

  Serial.println("--- Couleurs secondaires ---");

  // Jaune (R + G)
  Serial.println("Jaune");
  setRGB(255, 255, 0);
  delay(1000);

  // Cyan (G + B)
  Serial.println("Cyan");
  setRGB(0, 255, 255);
  delay(1000);

  // Magenta (R + B)
  Serial.println("Magenta");
  setRGB(255, 0, 255);
  delay(1000);

  // Blanc (R + G + B)
  Serial.println("Blanc");
  setRGB(255, 255, 255);
  delay(1000);

  // Éteint
  Serial.println("Eteint");
  setRGB(0, 0, 0);
  delay(500);

  Serial.println("--- Effet arc-en-ciel ---");
  rainbow(3000);  // 3 secondes

  Serial.println("--- Fondu ---");
  fadeColors(2000);  // 2 secondes par transition
}

// Effet arc-en-ciel fluide
void rainbow(int duration) {
  int steps = duration / 10;
  for (int i = 0; i < steps; i++) {
    int hue = (i * 360) / steps;
    uint8_t r, g, b;
    hueToRGB(hue, &r, &g, &b);
    setRGB(r, g, b);
    delay(10);
  }
}

// Fondu entre couleurs
void fadeColors(int transitionTime) {
  // Rouge vers Vert
  fadeBetween(255, 0, 0, 0, 255, 0, transitionTime);
  // Vert vers Bleu
  fadeBetween(0, 255, 0, 0, 0, 255, transitionTime);
  // Bleu vers Rouge
  fadeBetween(0, 0, 255, 255, 0, 0, transitionTime);
}

void fadeBetween(uint8_t r1, uint8_t g1, uint8_t b1,
                 uint8_t r2, uint8_t g2, uint8_t b2,
                 int duration) {
  int steps = duration / 10;
  for (int i = 0; i <= steps; i++) {
    uint8_t r = r1 + (r2 - r1) * i / steps;
    uint8_t g = g1 + (g2 - g1) * i / steps;
    uint8_t b = b1 + (b2 - b1) * i / steps;
    setRGB(r, g, b);
    delay(10);
  }
}

// Convertir Hue (0-360) en RGB
void hueToRGB(int hue, uint8_t *r, uint8_t *g, uint8_t *b) {
  int sector = hue / 60;
  int remainder = (hue % 60) * 255 / 60;

  switch (sector) {
    case 0:
      *r = 255; *g = remainder; *b = 0;
      break;
    case 1:
      *r = 255 - remainder; *g = 255; *b = 0;
      break;
    case 2:
      *r = 0; *g = 255; *b = remainder;
      break;
    case 3:
      *r = 0; *g = 255 - remainder; *b = 255;
      break;
    case 4:
      *r = remainder; *g = 0; *b = 255;
      break;
    default:
      *r = 255; *g = 0; *b = 255 - remainder;
      break;
  }
}
