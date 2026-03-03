/*
 * Servo_Potentiometer - NodeMCU (ESP8266)
 *
 * Controle d'un servo-moteur avec un potentiometre.
 * La valeur analogique du potentiometre est convertie
 * en angle (0-180 degres) pour piloter le servo.
 *
 * Cablage :
 *   Potentiometre : signal -> A0, VCC -> 3.3V, GND -> GND
 *   Servo : signal -> D1 (GPIO5), VCC -> 5V (alimentation externe), GND -> GND commun
 *
 * Board: NodeMCU 1.0 (ESP-12E Module)
 * FQBN: esp8266:esp8266:nodemcuv2
 *
 * @dependencies (aucune)
 */

#include <Servo.h>

#define POT_PIN   A0
#define SERVO_PIN D1  // GPIO5

Servo myServo;

int lastAngle = -1;

void setup() {
  Serial.begin(115200);
  Serial.println("\nServo Potentiometer");

  myServo.attach(SERVO_PIN, 544, 2400);
}

void loop() {
  int potValue = analogRead(POT_PIN);
  int angle = map(potValue, 0, 1023, 0, 180);

  // Envoyer la commande au servo uniquement si l'angle change
  if (angle != lastAngle) {
    myServo.write(angle);
    lastAngle = angle;
    Serial.printf("Pot: %d -> Angle: %d°\n", potValue, angle);
  }

  delay(20);
}
