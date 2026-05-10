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
#include <SPI.h>
#include <TFT_eSPI.h>
#include "credentials.h"

TFT_eSPI tft = TFT_eSPI();

#define COLOR_BG       0x0000   // noir
#define COLOR_HEADER   0x18C3   // gris foncé
#define COLOR_TEXT     0xFFFF   // blanc
#define COLOR_OK       0x06A0   // vert
#define COLOR_KO       0xE186   // rouge
#define COLOR_UNKNOWN  0x6B4D   // gris

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

void drawCascadeRow(int y, const char* label, bool ok, bool checked,
                    const char* detail, int latency) {
  uint16_t dotColor = checked ? (ok ? COLOR_OK : COLOR_KO) : COLOR_UNKNOWN;

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(label, 6, y, 2);

  tft.fillCircle(48, y + 8, 5, dotColor);

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawString(detail, 60, y, 2);

  char buf[16];
  if (checked && ok) snprintf(buf, sizeof(buf), "%d ms", latency);
  else snprintf(buf, sizeof(buf), "-");
  tft.setTextDatum(TR_DATUM);
  tft.drawString(buf, 314, y, 2);
}

void drawCascade() {
  tft.fillRect(0, 24, 320, 96, COLOR_BG);

  bool wifi_ok = (WiFi.status() == WL_CONNECTED);
  char wifiDet[40];
  if (wifi_ok) snprintf(wifiDet, sizeof(wifiDet), "%s  %ddBm",
                        WiFi.SSID().c_str(), WiFi.RSSI());
  else snprintf(wifiDet, sizeof(wifiDet), "disconnected");
  drawCascadeRow(28, "WiFi", wifi_ok && !wifiDown, true, wifiDet, 0);

  String gw = WiFi.gatewayIP().toString();
  drawCascadeRow(50, "Box ", !lanDown, wifi_ok, gw.c_str(), gwLatency);

  drawCascadeRow(72, "Net ", !inetDown, !lanDown && wifi_ok,
                 "8.8.8.8", inetLatency);

  drawCascadeRow(94, "DNS ", !dnsDown, !inetDown && !lanDown && wifi_ok,
                 DNS_TARGET, dnsLatency);
}

void drawHeader() {
  tft.fillRect(0, 0, 320, 24, COLOR_HEADER);
  tft.setTextColor(COLOR_TEXT, COLOR_HEADER);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("Internet Monitor", 6, 4, 2);

  struct tm tm;
  char clock[16] = "--:--:--";
  if (getLocalTime(&tm, 5)) {
    snprintf(clock, sizeof(clock), "%02d:%02d:%02d",
             tm.tm_hour, tm.tm_min, tm.tm_sec);
  }
  tft.setTextDatum(TC_DATUM);
  tft.drawString(clock, 200, 4, 2);

  uint16_t color = (currentState == ST_OK) ? COLOR_OK :
                   (currentState == ST_CHECKING) ? COLOR_UNKNOWN : COLOR_KO;
  const char* label =
    (currentState == ST_OK)            ? "OK"   :
    (currentState == ST_CHECKING)      ? "..."  :
    (currentState == ST_WIFI_DOWN)     ? "WIFI" :
    (currentState == ST_LAN_DOWN)      ? "BOX"  :
    (currentState == ST_INTERNET_DOWN) ? "NET"  : "DNS";
  tft.fillRoundRect(265, 2, 50, 20, 4, color);
  tft.setTextColor(COLOR_TEXT, color);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, 290, 12, 2);
}

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

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("Internet Monitor", 6, 4, 2);
  tft.drawString("Connecting WiFi...", 6, 30, 2);

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
    tft.fillScreen(COLOR_BG);

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

  drawHeader();
  drawCascade();

  delay(CHECK_INTERVAL_MS);
}
