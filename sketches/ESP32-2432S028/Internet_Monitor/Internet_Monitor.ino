/*
 * Internet_Monitor - ESP32-2432S028 (Cheap Yellow Display)
 *
 * Moniteur de connexion Internet avec diagnostic en cascade
 * (WiFi / Box / Internet / DNS), affichage TFT permanent et
 * alerte sonore avec boutons silence tactiles.
 *
 * Board: ESP32 Dev Module (CYD)
 * FQBN: esp32:esp32:esp32
 *
 * @dependencies TFT_eSPI, XPT2046_Touchscreen, ESP32Ping
 */

#include <WiFi.h>
#include <ESP32Ping.h>
#include <time.h>
#include "credentials.h"

#define CHECK_INTERVAL_MS 5000
#define DNS_TARGET "cloudflare.com"
#define NTP_SERVER "pool.ntp.org"
#define TZ_PARIS "CET-1CEST,M3.5.0,M10.5.0/3"
const IPAddress INTERNET_TARGET(8, 8, 8, 8);

enum State { ST_CHECKING, ST_OK, ST_WIFI_DOWN, ST_LAN_DOWN, ST_INTERNET_DOWN, ST_DNS_DOWN };
const char* stateNames[] = {"CHECKING", "OK", "WIFI_DOWN", "LAN_DOWN", "INTERNET_DOWN", "DNS_DOWN"};

State currentState = ST_CHECKING;
State previousState = ST_CHECKING;
int wifiFail = 0, lanFail = 0, inetFail = 0, dnsFail = 0;
bool wifiDown = false, lanDown = false, inetDown = false, dnsDown = false;
int gwLatency = 0, inetLatency = 0, dnsLatency = 0;

unsigned long bootMs = 0;
unsigned long totalDowntimeMs = 0;
time_t outageStartEpoch = 0;
unsigned long outageStartMs = 0;
time_t lastOutageStartEpoch = 0;
unsigned long lastOutageDurationS = 0;

float uptimePct() {
  unsigned long now = millis();
  unsigned long total = now - bootMs;
  if (total == 0) return 100.0f;
  unsigned long down = totalDowntimeMs;
  if (currentState != ST_OK && currentState != ST_CHECKING) {
    down += now - outageStartMs;
  }
  if (down > total) down = total;
  return 100.0f * (total - down) / total;
}

void checkCascade() {
  // Layer 1 : WiFi
  bool wifi_ok = (WiFi.status() == WL_CONNECTED);
  if (wifi_ok) { wifiFail = 0; wifiDown = false; }
  else {
    wifiFail++;
    if (wifiFail >= 2) wifiDown = true;
    WiFi.reconnect();
  }

  // Layer 2 : Gateway
  bool lan_ok = false;
  if (wifi_ok) {
    lan_ok = Ping.ping(WiFi.gatewayIP(), 1);
    if (lan_ok) {
      lanFail = 0; lanDown = false;
      gwLatency = (int)Ping.averageTime();
    } else {
      lanFail++;
      if (lanFail >= 2) lanDown = true;
    }
  }

  // Layer 3 : Internet
  bool inet_ok = false;
  if (lan_ok) {
    inet_ok = Ping.ping(INTERNET_TARGET, 1);
    if (inet_ok) {
      inetFail = 0; inetDown = false;
      inetLatency = (int)Ping.averageTime();
    } else {
      inetFail++;
      if (inetFail >= 2) inetDown = true;
    }
  }

  // Layer 4 : DNS
  bool dns_ok = false;
  if (inet_ok) {
    IPAddress resolved;
    unsigned long t0 = millis();
    dns_ok = WiFi.hostByName(DNS_TARGET, resolved);
    if (dns_ok) {
      dnsFail = 0; dnsDown = false;
      dnsLatency = (int)(millis() - t0);
    } else {
      dnsFail++;
      if (dnsFail >= 2) dnsDown = true;
    }
  }

  // Détermination de l'état (couche la plus haute en panne)
  if (wifiDown) currentState = ST_WIFI_DOWN;
  else if (lanDown) currentState = ST_LAN_DOWN;
  else if (inetDown) currentState = ST_INTERNET_DOWN;
  else if (dnsDown) currentState = ST_DNS_DOWN;
  else currentState = ST_OK;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Internet Monitor ===");

  bootMs = millis();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nConnected: IP %s, Gateway %s, RSSI %d dBm\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.gatewayIP().toString().c_str(),
                  WiFi.RSSI());

    configTzTime(TZ_PARIS, NTP_SERVER);
    Serial.println("Sync NTP...");
    struct tm tm;
    unsigned long ntpStart = millis();
    while (!getLocalTime(&tm, 200) && millis() - ntpStart < 10000) {
      delay(200);
    }
    if (getLocalTime(&tm)) {
      Serial.printf("Heure : %02d:%02d:%02d\n", tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else {
      Serial.println("NTP timeout");
    }
  } else {
    Serial.println("\nWiFi connection timeout");
  }
}

void loop() {
  checkCascade();

  bool wasOk = (previousState == ST_OK);
  bool isOk = (currentState == ST_OK);

  if (wasOk && !isOk) {
    outageStartEpoch = time(nullptr);
    outageStartMs = millis();
    Serial.printf("⚠ Coupure debut a epoch %ld\n", (long)outageStartEpoch);
  }
  if (!wasOk && isOk && previousState != ST_CHECKING) {
    lastOutageStartEpoch = outageStartEpoch;
    lastOutageDurationS = (millis() - outageStartMs) / 1000;
    totalDowntimeMs += millis() - outageStartMs;
    Serial.printf("✓ Retour reseau, coupure de %lus\n", lastOutageDurationS);
  }
  previousState = currentState;

  Serial.printf("[%s] RSSI:%d GW:%dms NET:%dms DNS:%dms Uptime:%.2f%% TotalDown:%lus\n",
    stateNames[currentState], WiFi.RSSI(),
    gwLatency, inetLatency, dnsLatency,
    uptimePct(), totalDowntimeMs / 1000);

  delay(CHECK_INTERVAL_MS);
}
