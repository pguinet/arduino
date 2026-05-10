/*
 * Buzzer_Test - ESP32-2432S028 (Cheap Yellow Display)
 *
 * Teste le speaker sur GPIO26 : gamme ascendante puis thème Mario Bros,
 * en boucle avec pause de 3s entre chaque cycle.
 * LED RGB sert d'indicateur visuel de l'état.
 *
 * Board: ESP32 Dev Module (CYD)
 * FQBN: esp32:esp32:esp32
 *
 * @dependencies (aucune)
 */

#define SPEAKER_PIN 26
#define LED_R 4
#define LED_G 16
#define LED_B 17

// Fréquences des notes (Hz)
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_G5  784
#define NOTE_A5  880
#define NOTE_B5  988
#define REST     0

struct Note {
  int freq;
  int duration;  // ms
};

const Note scale[] = {
  {NOTE_C4, 150}, {NOTE_D4, 150}, {NOTE_E4, 150}, {NOTE_F4, 150},
  {NOTE_G4, 150}, {NOTE_A4, 150}, {NOTE_B4, 150}, {NOTE_C5, 300},
};

const Note mario[] = {
  {NOTE_E5, 120}, {REST, 40},
  {NOTE_E5, 120}, {REST, 200},
  {NOTE_E5, 120}, {REST, 200},
  {NOTE_C5, 120}, {NOTE_E5, 120}, {REST, 200},
  {NOTE_G5, 120}, {REST, 500},
  {NOTE_G4, 120}, {REST, 500},
};

// LED RGB active LOW : LOW = allumé, HIGH = éteint
void setLed(bool r, bool g, bool b) {
  digitalWrite(LED_R, r ? LOW : HIGH);
  digitalWrite(LED_G, g ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
}

void playNote(int freq, int duration) {
  if (freq > 0) {
    ledcWriteTone(SPEAKER_PIN, freq);
  } else {
    ledcWriteTone(SPEAKER_PIN, 0);
  }
  delay(duration);
  ledcWriteTone(SPEAKER_PIN, 0);
  delay(30);
}

void playMelody(const Note* melody, size_t length) {
  for (size_t i = 0; i < length; i++) {
    playNote(melody[i].freq, melody[i].duration);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== Buzzer Test v2 - LEDC API ===");
  Serial.flush();

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  setLed(true, false, false);  // ROUGE = setup
  Serial.println("LED rouge ON");
  Serial.flush();

  // Configuration LEDC sur GPIO26 : freq initiale 1kHz, résolution 8 bits
  if (!ledcAttach(SPEAKER_PIN, 1000, 8)) {
    Serial.println("ERREUR: ledcAttach a échoué !");
    Serial.flush();
  } else {
    Serial.println("LEDC attaché OK sur GPIO26");
    Serial.flush();
  }

  // Bip de démarrage
  Serial.println("Bip 880 Hz pendant 200ms");
  Serial.flush();
  setLed(false, false, true);  // BLEU pendant le bip
  ledcWriteTone(SPEAKER_PIN, 880);
  delay(200);
  ledcWriteTone(SPEAKER_PIN, 0);
  setLed(false, false, false);  // OFF
  Serial.println("Setup terminé");
  Serial.flush();
}

void loop() {
  Serial.println(">> Gamme ascendante");
  Serial.flush();
  setLed(false, true, false);  // VERT
  playMelody(scale, sizeof(scale) / sizeof(scale[0]));
  setLed(false, false, false);
  delay(1000);

  Serial.println(">> Mario Bros theme");
  Serial.flush();
  setLed(false, false, true);  // BLEU
  playMelody(mario, sizeof(mario) / sizeof(mario[0]));
  setLed(false, false, false);
  delay(3000);
}
