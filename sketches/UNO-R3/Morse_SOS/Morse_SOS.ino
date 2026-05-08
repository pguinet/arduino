/*
 * Morse_SOS - Arduino UNO R3
 *
 * Clignote le message "SOS" en code morse sur la LED integree (pin 13).
 * S = ... (3 courts), O = --- (3 longs).
 *
 * Board: Arduino UNO R3
 * FQBN: arduino:avr:uno
 *
 * @dependencies (aucune)
 */

const int LED_PIN = LED_BUILTIN;

const int DOT = 200;
const int DASH = DOT * 3;
const int GAP_SYMBOL = DOT;
const int GAP_LETTER = DOT * 3;
const int GAP_WORD = DOT * 7;

void blink(int duration) {
  digitalWrite(LED_PIN, HIGH);
  delay(duration);
  digitalWrite(LED_PIN, LOW);
  delay(GAP_SYMBOL);
}

void letterS() {
  blink(DOT);
  blink(DOT);
  blink(DOT);
}

void letterO() {
  blink(DASH);
  blink(DASH);
  blink(DASH);
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(9600);
  Serial.println("Morse SOS - UNO R3");
}

void loop() {
  Serial.println("... --- ...");

  letterS();
  delay(GAP_LETTER - GAP_SYMBOL);
  letterO();
  delay(GAP_LETTER - GAP_SYMBOL);
  letterS();

  delay(GAP_WORD);
}
