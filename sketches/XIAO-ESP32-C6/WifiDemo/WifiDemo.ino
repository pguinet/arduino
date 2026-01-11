/*
 * WifiDemo - XIAO ESP32-C6
 *
 * Connexion WiFi basique.
 *
 * Board: Seeed XIAO ESP32-C6
 * FQBN: esp32:esp32:XIAO_ESP32C6
 *
 * @dependencies (aucune)
 */

#include <WiFi.h>
#include "credentials.h"

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.print("Connexion a ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connecte!");
  Serial.print("Adresse IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Force du signal: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}

void loop() {
  // Vérification périodique de la connexion
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connexion perdue, reconnexion...");
    WiFi.reconnect();
    delay(5000);
  }
  delay(10000);
}
