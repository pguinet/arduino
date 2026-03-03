/*
 * Simon_Game - Circuit Playground Express
 *
 * Jeu de mémoire Simon. La carte joue une séquence lumineuse et sonore
 * de plus en plus longue. Le joueur la reproduit en touchant les pads
 * capacitifs. Bouton A pour démarrer une nouvelle partie.
 *
 * Pads utilisés (en face des LEDs correspondantes) :
 *   A7 = Rouge (LEDs 3,4),  A1 = Bleu (LEDs 6,5),
 *   A3 = Vert (LEDs 8,9),   A4 = Jaune (LEDs 0,1)
 *
 * Board: Adafruit Circuit Playground Express
 * FQBN: adafruit:samd:adafruit_circuitplayground_m0
 *
 * @dependencies Adafruit_CircuitPlayground
 */

#include <Adafruit_CircuitPlayground.h>

#define MAX_SEQ      50
#define DEBOUNCE_MS  200
#define CALIB_MARGIN 300  // Marge au-dessus du repos pour détecter un toucher

// 4 zones du jeu, chacune avec ses LEDs, sa note et son pad
struct Zone {
  uint8_t padPin;       // Pin du pad capacitif
  uint8_t leds[2];      // 2 LEDs par zone (indices 0-9)
  uint32_t color;       // Couleur
  uint16_t freq;        // Fréquence du son
};

const Zone zones[4] = {
  { A7, {3, 4}, 0xFF0000, 330 },  // Rouge
  { A1, {6, 5}, 0x0000FF, 440 },  // Bleu
  { A3, {8, 9}, 0x00FF00, 550 },  // Vert
  { A4, {0, 1}, 0xFFFF00, 660 },  // Jaune
};

uint8_t sequence[MAX_SEQ];
int seqLen = 0;
int score = 0;
bool gameActive = false;
uint16_t touchThresh[4];  // Seuil calibré par pad

void calibrate() {
  Serial.println("Calibrage... ne touche rien !");
  flashAll(0x0000FF, 500);

  // Lire plusieurs fois chaque pad au repos pour trouver la valeur max
  uint16_t maxVal[4] = {0, 0, 0, 0};
  for (int sample = 0; sample < 20; sample++) {
    for (int z = 0; z < 4; z++) {
      uint16_t val = CircuitPlayground.readCap(zones[z].padPin);
      if (val > maxVal[z]) maxVal[z] = val;
    }
    delay(50);
  }

  for (int z = 0; z < 4; z++) {
    touchThresh[z] = maxVal[z] + CALIB_MARGIN;
    Serial.print("  ");
    Serial.print(zones[z].padPin);
    Serial.print(" repos=");
    Serial.print(maxVal[z]);
    Serial.print(" seuil=");
    Serial.println(touchThresh[z]);
  }

  Serial.println("Calibrage OK !");
  flashAll(0x00FF00, 300);
}

void setup() {
  CircuitPlayground.begin();
  CircuitPlayground.setBrightness(30);
  Serial.begin(115200);
  Serial.println("Simon Game - Circuit Playground Express");
  randomSeed(analogRead(0));
  calibrate();
  Serial.println("Touche le bouton A pour commencer !");
  idleAnimation();
}

void loop() {
  if (CircuitPlayground.leftButton()) {
    delay(200);  // Anti-rebond
    startGame();
  }

  if (!gameActive) {
    idleAnimation();
  }
}

void startGame() {
  gameActive = true;
  seqLen = 0;
  score = 0;
  Serial.println("\n--- Nouvelle partie ---");

  while (gameActive) {
    // Ajouter un élément à la séquence
    sequence[seqLen] = random(4);
    seqLen++;

    Serial.print("Niveau ");
    Serial.println(seqLen);

    delay(500);

    // Jouer la séquence
    playSequence();

    delay(300);

    // Attendre la réponse du joueur
    if (!getPlayerInput()) {
      gameOver();
      return;
    }

    score = seqLen;

    // Feedback de succès
    flashAll(0x00FF00, 200);
    delay(300);

    if (seqLen >= MAX_SEQ) {
      victory();
      return;
    }
  }
}

// Joue la séquence complète
void playSequence() {
  int delayMs = max(200, 500 - seqLen * 20);  // Accélère avec le niveau

  for (int i = 0; i < seqLen; i++) {
    lightZone(sequence[i], delayMs);
    delay(100);
  }
}

// Allume une zone avec son
void lightZone(int z, int duration) {
  CircuitPlayground.setPixelColor(zones[z].leds[0], zones[z].color);
  CircuitPlayground.setPixelColor(zones[z].leds[1], zones[z].color);
  CircuitPlayground.playTone(zones[z].freq, duration);
  CircuitPlayground.clearPixels();
}

// Attend que le joueur reproduise la séquence
bool getPlayerInput() {
  for (int i = 0; i < seqLen; i++) {
    int touched = waitForTouch(5000);  // 5s timeout

    if (touched < 0) {
      Serial.println("Timeout !");
      return false;
    }

    if (touched != sequence[i]) {
      Serial.print("Erreur ! Attendu: ");
      Serial.print(sequence[i]);
      Serial.print(" Recu: ");
      Serial.println(touched);
      return false;
    }

    // Feedback tactile
    lightZone(touched, 150);
  }
  return true;
}

// Attend qu'un pad soit touché, retourne l'index de zone (0-3) ou -1
int waitForTouch(unsigned long timeoutMs) {
  unsigned long start = millis();

  // Attendre que tous les pads soient relâchés d'abord
  while (millis() - start < timeoutMs) {
    bool anyTouched = false;
    for (int z = 0; z < 4; z++) {
      if (CircuitPlayground.readCap(zones[z].padPin) > touchThresh[z]) {
        anyTouched = true;
      }
    }
    if (!anyTouched) break;
    delay(10);
  }

  // Attendre un nouveau toucher
  while (millis() - start < timeoutMs) {
    for (int z = 0; z < 4; z++) {
      if (CircuitPlayground.readCap(zones[z].padPin) > touchThresh[z]) {
        // Allumer pendant le toucher
        CircuitPlayground.setPixelColor(zones[z].leds[0], zones[z].color);
        CircuitPlayground.setPixelColor(zones[z].leds[1], zones[z].color);
        delay(DEBOUNCE_MS);
        CircuitPlayground.clearPixels();
        return z;
      }
    }

    // Bouton A = abandon
    if (CircuitPlayground.leftButton()) {
      gameActive = false;
      return -1;
    }
    delay(10);
  }
  return -1;  // Timeout
}

void gameOver() {
  gameActive = false;
  Serial.print("Game Over ! Score: ");
  Serial.println(score);

  // Animation de défaite : flash rouge
  for (int i = 0; i < 3; i++) {
    flashAll(0xFF0000, 200);
    delay(200);
  }

  // Jouer un son triste
  CircuitPlayground.playTone(200, 300);
  delay(100);
  CircuitPlayground.playTone(150, 500);

  // Afficher le score sur les LEDs
  displayScore(score);
  delay(3000);
  CircuitPlayground.clearPixels();
}

void victory() {
  gameActive = false;
  Serial.println("VICTOIRE ! Sequence max atteinte !");

  // Animation arc-en-ciel
  for (int j = 0; j < 3; j++) {
    for (int offset = 0; offset < 256; offset += 8) {
      for (int i = 0; i < 10; i++) {
        CircuitPlayground.setPixelColor(i,
          CircuitPlayground.colorWheel((i * 25 + offset) % 256));
      }
      delay(20);
    }
  }
  CircuitPlayground.clearPixels();
}

// Affiche le score : LEDs allumées = dizaines en vert, dernière = unités en bleu
void displayScore(int s) {
  CircuitPlayground.clearPixels();
  int display = min(s, 10);
  for (int i = 0; i < display; i++) {
    CircuitPlayground.setPixelColor(i, 0, 100, 0);
  }
}

void flashAll(uint32_t color, int duration) {
  for (int i = 0; i < 10; i++) {
    CircuitPlayground.setPixelColor(i, color);
  }
  delay(duration);
  CircuitPlayground.clearPixels();
}

// Animation d'attente
void idleAnimation() {
  static uint8_t pos = 0;
  CircuitPlayground.clearPixels();

  // Point lumineux qui tourne doucement
  CircuitPlayground.setPixelColor(pos % 10, 0, 0, 40);
  CircuitPlayground.setPixelColor((pos + 1) % 10, 0, 0, 20);

  pos++;
  delay(150);
}
