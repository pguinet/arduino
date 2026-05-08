/*
 * NTP_Clock - WEMOS D1 R32 (ESP32)
 *
 * Horloge NTP avec affichage sur moniteur serie.
 * Synchronise l'heure via pool.ntp.org, gere le fuseau Europe/Paris
 * (heure d'ete automatique), met a jour toutes les secondes.
 *
 * Board: WEMOS D1 R32
 * FQBN: esp32:esp32:d1_uno32
 *
 * @dependencies (aucune)
 */

#include <WiFi.h>
#include <time.h>
#include "credentials.h"

#define TZ_PARIS "CET-1CEST,M3.5.0,M10.5.0/3"

const char* jours[]   = {"Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"};
const char* mois[]    = {"janvier", "fevrier", "mars", "avril", "mai", "juin",
                         "juillet", "aout", "septembre", "octobre", "novembre", "decembre"};

void connectWiFi() {
  Serial.print("Connexion a ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connecte. IP : ");
  Serial.println(WiFi.localIP());
}

void syncNTP() {
  Serial.print("Synchronisation NTP");
  configTime(0, 0, "pool.ntp.org", "time.google.com", "fr.pool.ntp.org");
  setenv("TZ", TZ_PARIS, 1);
  tzset();

  struct tm tm_now;
  while (!getLocalTime(&tm_now, 1000)) {
    Serial.print(".");
  }
  Serial.println(" OK");
}

void printTime() {
  struct tm tm_now;
  if (!getLocalTime(&tm_now)) {
    Serial.println("Erreur lecture heure");
    return;
  }
  char line[80];
  snprintf(line, sizeof(line), "%s %d %s %d  -  %02d:%02d:%02d",
           jours[tm_now.tm_wday],
           tm_now.tm_mday, mois[tm_now.tm_mon], tm_now.tm_year + 1900,
           tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
  Serial.println(line);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== NTP Clock - D1 R32 ===");

  connectWiFi();
  syncNTP();

  Serial.println();
  Serial.println("Heure courante (Europe/Paris) :");
  Serial.println("---------------------------------");
}

void loop() {
  printTime();
  delay(1000);
}
