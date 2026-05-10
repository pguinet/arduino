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
#include <XPT2046_Touchscreen.h>
#include "credentials.h"

TFT_eSPI tft = TFT_eSPI();

#define TOUCH_CS  33
#define TOUCH_IRQ 36
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// Calibration approx pour CYD en rotation 1 (landscape)
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3700
#define TOUCH_Y_MIN 240
#define TOUCH_Y_MAX 3800

struct Rect { int x, y, w, h; };

#define COLOR_BG       0x0000   // noir
#define COLOR_HEADER   0x18C3   // gris foncé
#define COLOR_TEXT     0xFFFF   // blanc
#define COLOR_OK       0x06A0   // vert
#define COLOR_KO       0xE186   // rouge
#define COLOR_UNKNOWN  0x6B4D   // gris

#define CHECK_INTERVAL_MS 5000
#define BEEP_INTERVAL_MS 10000
#define SPEAKER_PIN 26
#define DNS_TARGET "cloudflare.com"
#define NTP_SERVER "pool.ntp.org"
#define TZ_PARIS "CET-1CEST,M3.5.0,M10.5.0/3"
const IPAddress INTERNET_TARGET(8, 8, 8, 8);

unsigned long lastBeepMs = 0;
unsigned long silenceUntilMs = 0;
bool silencePermanent = false;
unsigned long lastTapMs = 0;

bool isSilenced() {
  if (silencePermanent) return true;
  if (silenceUntilMs > 0 && millis() < silenceUntilMs) return true;
  return false;
}

void setSilence5min()      { silenceUntilMs = millis() + 5UL * 60UL * 1000UL; silencePermanent = false; }
void setSilence30min()     { silenceUntilMs = millis() + 30UL * 60UL * 1000UL; silencePermanent = false; }
void setSilencePermanent() { silenceUntilMs = 0; silencePermanent = true; }
void clearSilence()        { silenceUntilMs = 0; silencePermanent = false; }

const Rect btn5min   = {6,   206, 100, 32};
const Rect btn30min  = {110, 206, 100, 32};
const Rect btnPerm   = {214, 206, 100, 32};
const Rect bandeau   = {6,   206, 308, 32};

bool inRect(int x, int y, Rect r) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

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

#define LATENCY_HISTORY_SIZE 120
int16_t latencyHistory[LATENCY_HISTORY_SIZE];
int latencyHead = 0;
bool latencyFull = false;

void pushLatency(int16_t v) {
  latencyHistory[latencyHead] = v;
  latencyHead = (latencyHead + 1) % LATENCY_HISTORY_SIZE;
  if (latencyHead == 0) latencyFull = true;
}

void playTone(int freq, int durationMs) {
  ledcWriteTone(SPEAKER_PIN, freq);
  delay(durationMs);
  ledcWriteTone(SPEAKER_PIN, 0);
}

void playDownPattern() {
  for (int i = 0; i < 3; i++) {
    playTone(800, 100);
    delay(80);
  }
}

void playUpPattern() {
  playTone(600, 100);
  playTone(800, 100);
  playTone(1000, 100);
}

void drawCascadeRow(int y, const char* label, bool ok, bool checked,
                    const char* detail, int latency) {
  uint16_t dotColor = checked ? (ok ? COLOR_OK : COLOR_KO) : COLOR_UNKNOWN;

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  tft.drawString(label, 6, y, 2);

  tft.fillCircle(48, y + 8, 5, dotColor);

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextPadding(190);  // detail field reserved width (60..250)
  tft.drawString(detail, 60, y, 2);

  char buf[16];
  if (checked && ok) {
    if (latency > 999) snprintf(buf, sizeof(buf), ">999ms");
    else snprintf(buf, sizeof(buf), "%dms", latency);
  } else snprintf(buf, sizeof(buf), "-");
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(64);   // latency field reserved width
  tft.drawString(buf, 314, y, 2);
  tft.setTextPadding(0);
}

void drawCascade() {
  bool wifi_ok = (WiFi.status() == WL_CONNECTED);
  char wifiDet[40];
  if (wifi_ok) snprintf(wifiDet, sizeof(wifiDet), "%s %ddBm",
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

void drawButton(Rect r, const char* label, uint16_t color) {
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, color);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, COLOR_TEXT);
  tft.setTextColor(COLOR_TEXT, color);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, r.x + r.w / 2, r.y + r.h / 2, 2);
}

int footerLastMode = -1;  // -1=non draw, 0=boutons, 1=bandeau

void drawFooter() {
  bool silenced = isSilenced();
  int mode = silenced ? 1 : 0;

  // Si transition, repeindre la zone et redessiner les elements stables
  if (mode != footerLastMode) {
    tft.fillRect(0, 200, 320, 40, COLOR_BG);
    if (silenced) {
      tft.fillRoundRect(bandeau.x, bandeau.y, bandeau.w, bandeau.h, 6, COLOR_HEADER);
    } else {
      drawButton(btn5min,  "5min",      COLOR_HEADER);
      drawButton(btn30min, "30min",     COLOR_HEADER);
      drawButton(btnPerm,  "Permanent", COLOR_HEADER);
    }
    footerLastMode = mode;
  }

  // En mode bandeau, raffraichir le countdown sans repeindre
  if (silenced) {
    char buf[64];
    if (silencePermanent) {
      snprintf(buf, sizeof(buf), "MUTE permanent - tap pour reactiver");
    } else {
      unsigned long left = (silenceUntilMs - millis()) / 1000;
      snprintf(buf, sizeof(buf), "MUTE %lum%02lus - tap pour reactiver",
               left / 60, left % 60);
    }
    tft.setTextColor(COLOR_TEXT, COLOR_HEADER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextPadding(300);
    tft.drawString(buf, bandeau.x + bandeau.w / 2, bandeau.y + bandeau.h / 2, 2);
    tft.setTextPadding(0);
  }
}

void mapTouch(int rawX, int rawY, int& sx, int& sy) {
  sx = map(rawX, TOUCH_X_MIN, TOUCH_X_MAX, 0, 320);
  sy = map(rawY, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 240);
}

void handleTouch() {
  if (!ts.tirqTouched() || !ts.touched()) return;
  if (millis() - lastTapMs < 500) return;
  lastTapMs = millis();
  TS_Point p = ts.getPoint();
  int sx, sy;
  mapTouch(p.x, p.y, sx, sy);
  Serial.printf("TAP raw=(%d,%d) screen=(%d,%d)\n", p.x, p.y, sx, sy);

  if (isSilenced()) {
    if (inRect(sx, sy, bandeau)) clearSilence();
  } else {
    if      (inRect(sx, sy, btn5min))  setSilence5min();
    else if (inRect(sx, sy, btn30min)) setSilence30min();
    else if (inRect(sx, sy, btnPerm))  setSilencePermanent();
  }
}

int graphLastDrawnHead = -1;

void drawStats() {
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextDatum(TL_DATUM);

  char buf[64];
  snprintf(buf, sizeof(buf), "Uptime %.1f%%", uptimePct());
  tft.setTextPadding(120);
  tft.drawString(buf, 6, 124, 2);

  if (lastOutageDurationS > 0) {
    struct tm tm;
    localtime_r(&lastOutageStartEpoch, &tm);
    snprintf(buf, sizeof(buf), "Dern: %02d:%02d (%lum%02lus)",
             tm.tm_hour, tm.tm_min,
             lastOutageDurationS / 60, lastOutageDurationS % 60);
  } else {
    snprintf(buf, sizeof(buf), "Aucune coupure");
  }
  tft.setTextPadding(180);
  tft.drawString(buf, 140, 124, 2);
  tft.setTextPadding(0);

  // Cadre graphe (zone 6..314 x 148..198) - dessine une fois
  static bool graphFrameDrawn = false;
  if (!graphFrameDrawn) {
    tft.drawRect(6, 148, 308, 50, COLOR_HEADER);
    graphFrameDrawn = true;
  }

  // Redraw du graphe seulement si une nouvelle valeur a ete poussee
  if (graphLastDrawnHead == latencyHead) return;
  graphLastDrawnHead = latencyHead;

  // Effacement et redessin complet (1x toutes les 5s a chaque check)
  tft.fillRect(7, 149, 306, 48, COLOR_BG);
  int barWidth = 308 / LATENCY_HISTORY_SIZE;
  int graphH = 48;

  int maxLat = 50;
  for (int i = 0; i < LATENCY_HISTORY_SIZE; i++) {
    if (latencyHistory[i] > maxLat) maxLat = latencyHistory[i];
  }

  int startIdx = latencyFull ? latencyHead : 0;
  int count = latencyFull ? LATENCY_HISTORY_SIZE : latencyHead;
  for (int i = 0; i < count; i++) {
    int idx = (startIdx + i) % LATENCY_HISTORY_SIZE;
    int16_t v = latencyHistory[idx];
    int x = 7 + i * barWidth;
    if (v < 0) {
      tft.fillRect(x, 149, barWidth, graphH, COLOR_KO);
    } else if (v > 0) {
      int h = (v * graphH) / maxLat;
      if (h < 1) h = 1;
      tft.fillRect(x, 149 + (graphH - h), barWidth, h, COLOR_OK);
    }
  }
}

State headerLastState = ST_CHECKING;

void drawHeader() {
  static bool headerStaticDrawn = false;
  if (!headerStaticDrawn) {
    tft.fillRect(0, 0, 320, 24, COLOR_HEADER);
    tft.setTextColor(COLOR_TEXT, COLOR_HEADER);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Internet Monitor", 6, 4, 2);
    headerStaticDrawn = true;
    headerLastState = ST_CHECKING;  // force badge redraw
  }

  // Horloge (changement chaque seconde)
  struct tm tm;
  char clock[16] = "--:--:--";
  if (getLocalTime(&tm, 5)) {
    snprintf(clock, sizeof(clock), "%02d:%02d:%02d",
             tm.tm_hour, tm.tm_min, tm.tm_sec);
  }
  tft.setTextColor(COLOR_TEXT, COLOR_HEADER);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(80);
  tft.drawString(clock, 200, 4, 2);
  tft.setTextPadding(0);

  // Badge etat - redraw seulement sur changement
  if (currentState != headerLastState) {
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
    headerLastState = currentState;
  }
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

  if (currentState == ST_OK) pushLatency(inetLatency);
  else pushLatency(-1);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Internet Monitor ===");

  bootMs = millis();

  ledcAttach(SPEAKER_PIN, 1000, 8);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COLOR_BG);

  touchSPI.begin(25, 39, 32, TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);
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

unsigned long lastCheckMs = 0;
unsigned long lastUiFastMs = 0;
unsigned long lastUiSlowMs = 0;
unsigned long lastNtpSyncMs = 0;

void loop() {
  handleTouch();

  if (millis() - lastNtpSyncMs > 3600UL * 1000UL) {
    configTzTime(TZ_PARIS, NTP_SERVER);
    lastNtpSyncMs = millis();
  }

  if (millis() - lastCheckMs >= CHECK_INTERVAL_MS) {
    lastCheckMs = millis();
    checkCascade();

    bool wasOk = (previousState == ST_OK);
    bool isOk = (currentState == ST_OK);

    if (wasOk && !isOk) {
      outageStartEpoch = time(nullptr);
      outageStartMs = millis();
      if (!isSilenced()) playDownPattern();
      lastBeepMs = millis();
      Serial.printf("⚠ Coupure debut a epoch %ld\n", (long)outageStartEpoch);
    }
    if (!wasOk && isOk && previousState != ST_CHECKING) {
      lastOutageStartEpoch = outageStartEpoch;
      lastOutageDurationS = (millis() - outageStartMs) / 1000;
      totalDowntimeMs += millis() - outageStartMs;
      playUpPattern();
      clearSilence();
      Serial.printf("✓ Retour reseau, coupure de %lus\n", lastOutageDurationS);
    }
    previousState = currentState;

    Serial.printf("[%s] RSSI:%d GW:%dms NET:%dms DNS:%dms Uptime:%.2f%% TotalDown:%lus\n",
      stateNames[currentState], WiFi.RSSI(),
      gwLatency, inetLatency, dnsLatency,
      uptimePct(), totalDowntimeMs / 1000);
  }

  if (currentState != ST_OK && currentState != ST_CHECKING && !isSilenced()) {
    if (millis() - lastBeepMs >= BEEP_INTERVAL_MS) {
      playDownPattern();
      lastBeepMs = millis();
    }
  }

  unsigned long now = millis();
  if (now - lastUiFastMs >= 200) {
    drawHeader();
    drawFooter();
    lastUiFastMs = now;
  }
  if (now - lastUiSlowMs >= 1000) {
    drawCascade();
    drawStats();
    lastUiSlowMs = now;
  }
}
