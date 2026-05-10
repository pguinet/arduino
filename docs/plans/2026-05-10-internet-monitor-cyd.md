# Internet Monitor CYD — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:executing-plans to implement this plan task-by-task.

**Goal:** Construire un moniteur de connexion Internet pour CYD ESP32-2432S028 avec diagnostic en cascade (WiFi/LAN/Net/DNS), affichage permanent + bip répétitif et boutons silence.

**Architecture:** Sketch `.ino` unique compilé via arduino-cli. Cascade de checks toutes les 5 s avec hystérésis 2 échecs. UI 320×240 sur TFT_eSPI. Audio via ledcWriteTone GPIO26. Touch via XPT2046_Touchscreen.

**Tech Stack:** ESP32 Arduino core 3.x, TFT_eSPI, XPT2046_Touchscreen, ESP32Ping, NTP (time.h)

**Design source:** `docs/plans/2026-05-10-internet-monitor-cyd-design.md`

---

## Conventions de vérification

Pas de tests unitaires (pas de runner host pour Arduino). À chaque task :
- **Vérification compile** : la commande `arduino-cli compile` doit réussir
- **Vérification runtime** : upload + monitor série (15s timeout) ou inspection visuelle / sonore
- **Commit** à la fin de chaque task pour avoir un historique propre. Possibilité de squash final si désiré.

Toutes les commandes sont à exécuter depuis `/home/pascal/github/arduino`.

---

## Task 0 : Skeleton + connexion WiFi

**Files:**
- Create: `sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino`
- Create (symlink): `sketches/ESP32-2432S028/Internet_Monitor/credentials.h`

**Step 1 : Créer le dossier + symlink credentials**

```bash
mkdir -p sketches/ESP32-2432S028/Internet_Monitor
ln -s /home/pascal/github/arduino/sketches/common/credentials.h \
      sketches/ESP32-2432S028/Internet_Monitor/credentials.h
```

**Step 2 : Écrire le sketch initial**

Contenu `Internet_Monitor.ino` :

```c
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
#include "credentials.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Internet Monitor ===");

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
  } else {
    Serial.println("\nWiFi connection timeout");
  }
}

void loop() {
  delay(5000);
  Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
}
```

**Step 3 : Compiler**

```bash
./bin/arduino-cli compile --fqbn esp32:esp32:esp32 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino
```

Attendu : `Sketch uses ~270000 bytes (~20%) ... Global variables use ~22000 bytes (~6%)`.

**Step 4 : Upload**

```bash
./bin/arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB0 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino
```

**Step 5 : Vérifier (monitor série)**

```bash
timeout 15 ./bin/arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

Attendu :
```
=== Internet Monitor ===
Connecting to WiFi...
Connected: IP 192.168.x.x, Gateway 192.168.x.1, RSSI -XX dBm
RSSI: -XX dBm
```

**Step 6 : Commit**

```bash
git add sketches/ESP32-2432S028/Internet_Monitor/
git commit -m "Internet_Monitor: skeleton + connexion WiFi"
```

---

## Task 1 : Bibliothèque ESP32Ping + cascade de checks

**Files:**
- Modify: `libraries.txt`
- Modify: `sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino`

**Step 1 : Installer la lib**

```bash
./bin/arduino-cli lib install ESP32Ping
```

Attendu : `Installed ESP32Ping@x.x.x` (ou "déjà installée").

**Step 2 : Mettre à jour libraries.txt**

Ajouter une ligne `ESP32Ping` à la fin du fichier (avant les éventuels commentaires terminaux).

**Step 3 : Ajouter la cascade dans le sketch**

Remplacer le contenu du sketch par :

```c
/*
 * Internet_Monitor - ESP32-2432S028 (Cheap Yellow Display)
 * (header inchangé)
 */

#include <WiFi.h>
#include <ESP32Ping.h>
#include "credentials.h"

#define CHECK_INTERVAL_MS 5000
#define DNS_TARGET "cloudflare.com"
const IPAddress INTERNET_TARGET(8, 8, 8, 8);

enum State { CHECKING, OK, WIFI_DOWN, LAN_DOWN, INTERNET_DOWN, DNS_DOWN };
const char* stateNames[] = {"CHECKING", "OK", "WIFI_DOWN", "LAN_DOWN", "INTERNET_DOWN", "DNS_DOWN"};

State currentState = CHECKING;
int wifiFail = 0, lanFail = 0, inetFail = 0, dnsFail = 0;
bool wifiDown = false, lanDown = false, inetDown = false, dnsDown = false;
int gwLatency = 0, inetLatency = 0, dnsLatency = 0;

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
    if (lan_ok) { lanFail = 0; lanDown = false; gwLatency = Ping.averageTime(); }
    else { lanFail++; if (lanFail >= 2) lanDown = true; }
  }

  // Layer 3 : Internet
  bool inet_ok = false;
  if (lan_ok) {
    inet_ok = Ping.ping(INTERNET_TARGET, 1);
    if (inet_ok) { inetFail = 0; inetDown = false; inetLatency = Ping.averageTime(); }
    else { inetFail++; if (inetFail >= 2) inetDown = true; }
  }

  // Layer 4 : DNS
  bool dns_ok = false;
  if (inet_ok) {
    IPAddress resolved;
    unsigned long t0 = millis();
    dns_ok = WiFi.hostByName(DNS_TARGET, resolved);
    if (dns_ok) { dnsFail = 0; dnsDown = false; dnsLatency = millis() - t0; }
    else { dnsFail++; if (dnsFail >= 2) dnsDown = true; }
  }

  // Détermination de l'état (couche la plus "haute" en panne)
  if (wifiDown) currentState = WIFI_DOWN;
  else if (lanDown) currentState = LAN_DOWN;
  else if (inetDown) currentState = INTERNET_DOWN;
  else if (dnsDown) currentState = DNS_DOWN;
  else currentState = OK;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Internet Monitor ===");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(500); Serial.print(".");
  }
  Serial.println();
}

void loop() {
  checkCascade();
  Serial.printf("[%s] RSSI:%d GW:%dms NET:%dms DNS:%dms\n",
    stateNames[currentState], WiFi.RSSI(),
    gwLatency, inetLatency, dnsLatency);
  delay(CHECK_INTERVAL_MS);
}
```

**Step 4 : Compiler + uploader**

```bash
./bin/arduino-cli compile --fqbn esp32:esp32:esp32 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino && \
./bin/arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB0 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino
```

**Step 5 : Vérifier**

```bash
timeout 20 ./bin/arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

Attendu : lignes répétées toutes les 5 s, par ex. `[OK] RSSI:-52 GW:2ms NET:18ms DNS:24ms`. Pour tester un échec : couper le WiFi local quelques secondes → après 10 s tu dois voir `[WIFI_DOWN]`.

**Step 6 : Commit**

```bash
git add libraries.txt sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino
git commit -m "Internet_Monitor: cascade WiFi/LAN/Net/DNS avec hysteresis"
```

---

## Task 2 : NTP + tracking outage

**Files:**
- Modify: `Internet_Monitor.ino`

**Step 1 : Ajouter NTP en haut du fichier**

Après `#include "credentials.h"` :

```c
#include <time.h>

#define NTP_SERVER "pool.ntp.org"
#define TZ_PARIS "CET-1CEST,M3.5.0,M10.5.0/3"
```

Dans `setup()` après la connexion WiFi :

```c
configTzTime(TZ_PARIS, NTP_SERVER);
Serial.println("Sync NTP...");
struct tm tm;
unsigned long ntpStart = millis();
while (!getLocalTime(&tm) && millis() - ntpStart < 10000) {
  delay(200);
}
if (getLocalTime(&tm)) {
  Serial.printf("Heure : %02d:%02d:%02d\n", tm.tm_hour, tm.tm_min, tm.tm_sec);
}
```

**Step 2 : Tracking outage en variables globales**

```c
unsigned long bootMs = 0;
unsigned long totalDowntimeMs = 0;
time_t outageStartEpoch = 0;
unsigned long outageStartMs = 0;
time_t lastOutageStartEpoch = 0;
unsigned long lastOutageDurationS = 0;

State previousState = CHECKING;
```

**Step 3 : Détecter les transitions dans `loop()`**

Ajouter après `checkCascade()` :

```c
bool wasOk = (previousState == OK);
bool isOk = (currentState == OK);

if (wasOk && !isOk) {
  // Transition OK -> DOWN
  outageStartEpoch = time(nullptr);
  outageStartMs = millis();
  Serial.printf("⚠ Coupure début à %ld\n", (long)outageStartEpoch);
}
if (!wasOk && isOk && previousState != CHECKING) {
  // Transition DOWN -> OK
  lastOutageStartEpoch = outageStartEpoch;
  lastOutageDurationS = (millis() - outageStartMs) / 1000;
  totalDowntimeMs += millis() - outageStartMs;
  Serial.printf("✓ Retour réseau, coupure de %lus\n", lastOutageDurationS);
}
previousState = currentState;
```

Dans `setup()` initialiser `bootMs = millis();`.

**Step 4 : Calcul uptime % (helper)**

Ajouter avant `loop()` :

```c
float uptimePct() {
  unsigned long now = millis();
  unsigned long total = now - bootMs;
  if (total == 0) return 100.0;
  unsigned long down = totalDowntimeMs;
  if (currentState != OK && currentState != CHECKING) {
    down += now - outageStartMs;
  }
  if (down > total) down = total;
  return 100.0f * (total - down) / total;
}
```

Et logger dans `loop()` après le print précédent :

```c
Serial.printf("Uptime: %.2f%% Total downtime: %lus\n",
              uptimePct(), totalDowntimeMs / 1000);
```

**Step 5 : Compile + upload + monitor**

```bash
./bin/arduino-cli compile --fqbn esp32:esp32:esp32 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino && \
./bin/arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB0 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino && \
timeout 30 ./bin/arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

Attendu : `Heure : HH:MM:SS` au boot, puis lignes RSSI + uptime pct. Couper le WiFi 10 s, le rebrancher, vérifier les messages `⚠ Coupure début` et `✓ Retour réseau`.

**Step 6 : Commit**

```bash
git add sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino
git commit -m "Internet_Monitor: NTP + tracking duree des coupures"
```

---

## Task 3 : Display init + header

**Files:**
- Modify: `Internet_Monitor.ino`

**Pré-requis :** `User_Setup.h` doit être copié dans la lib TFT_eSPI (déjà fait pour les autres sketches CYD, vérifier avec `ls ~/Arduino/libraries/TFT_eSPI/User_Setup.h`).

**Step 1 : Includes + objet TFT**

En haut du fichier, après les autres includes :

```c
#include <SPI.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

#define COLOR_BG       0x0000   // noir
#define COLOR_HEADER   0x18C3   // gris foncé
#define COLOR_TEXT     0xFFFF   // blanc
#define COLOR_OK       0x06A0   // vert
#define COLOR_KO       0xE186   // rouge
#define COLOR_UNKNOWN  0x6B4D   // gris
```

**Step 2 : Init display dans `setup()`**

Avant le `WiFi.begin()` :

```c
tft.init();
tft.setRotation(1);  // landscape 320x240
tft.fillScreen(COLOR_BG);
tft.setTextColor(COLOR_TEXT, COLOR_BG);
tft.setTextDatum(TL_DATUM);
tft.drawString("Internet Monitor", 5, 5, 2);
tft.drawString("Connecting WiFi...", 5, 30, 2);
```

**Step 3 : Helper de rendu header**

Ajouter avant `loop()` :

```c
void drawHeader() {
  // Bandeau header (24px)
  tft.fillRect(0, 0, 320, 24, COLOR_HEADER);
  tft.setTextColor(COLOR_TEXT, COLOR_HEADER);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("Internet Monitor", 6, 4, 2);

  // Horloge (centrée à droite)
  struct tm tm;
  char clock[16] = "--:--:--";
  if (getLocalTime(&tm, 5)) {
    snprintf(clock, sizeof(clock), "%02d:%02d:%02d",
             tm.tm_hour, tm.tm_min, tm.tm_sec);
  }
  tft.setTextDatum(TC_DATUM);
  tft.drawString(clock, 200, 4, 2);

  // Badge état (à droite)
  uint16_t color = (currentState == OK) ? COLOR_OK :
                   (currentState == CHECKING) ? COLOR_UNKNOWN : COLOR_KO;
  const char* label =
    (currentState == OK)            ? "OK"   :
    (currentState == CHECKING)      ? "..."  :
    (currentState == WIFI_DOWN)     ? "WIFI" :
    (currentState == LAN_DOWN)      ? "BOX"  :
    (currentState == INTERNET_DOWN) ? "NET"  : "DNS";
  tft.fillRoundRect(265, 2, 50, 20, 4, color);
  tft.setTextColor(COLOR_TEXT, color);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, 290, 12, 2);
}
```

**Step 4 : Appeler `drawHeader()` dans `loop()`**

À la fin de `loop()` (avant `delay`) :

```c
drawHeader();
```

**Step 5 : Compile + upload + vérifier visuellement**

```bash
./bin/arduino-cli compile --fqbn esp32:esp32:esp32 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino && \
./bin/arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB0 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino
```

Attendu sur l'écran : header gris en haut avec "Internet Monitor", l'horloge mise à jour ~chaque 5 s, et un badge vert "OK" à droite.

**Step 6 : Commit**

```bash
git add sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino
git commit -m "Internet_Monitor: header TFT (titre + horloge + badge etat)"
```

---

## Task 4 : Cascade panel rendering

**Files:**
- Modify: `Internet_Monitor.ino`

**Step 1 : Helper draw cascade**

Ajouter avant `loop()` :

```c
void drawCascadeRow(int y, const char* label, bool ok, bool checked,
                    const char* detail, int latency) {
  uint16_t dotColor = checked ? (ok ? COLOR_OK : COLOR_KO) : COLOR_UNKNOWN;

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(label, 6, y, 2);            // ex: "WiFi"

  tft.fillCircle(48, y + 8, 5, dotColor);    // LED état

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawString(detail, 60, y, 2);           // détails

  // Latence à droite
  char buf[16];
  if (checked && ok) snprintf(buf, sizeof(buf), "%d ms", latency);
  else snprintf(buf, sizeof(buf), "-");
  tft.setTextDatum(TR_DATUM);
  tft.drawString(buf, 314, y, 2);
}

void drawCascade() {
  // Effacer la zone (24..120)
  tft.fillRect(0, 24, 320, 96, COLOR_BG);

  // 1. WiFi
  bool wifi_ok = (WiFi.status() == WL_CONNECTED);
  char wifiDet[40];
  if (wifi_ok) snprintf(wifiDet, sizeof(wifiDet), "%s  %d dBm",
                        WiFi.SSID().c_str(), WiFi.RSSI());
  else snprintf(wifiDet, sizeof(wifiDet), "disconnected");
  drawCascadeRow(28, "WiFi", wifi_ok && !wifiDown, true, wifiDet, 0);

  // 2. Box
  String gw = WiFi.gatewayIP().toString();
  drawCascadeRow(50, "Box ", !lanDown, wifi_ok, gw.c_str(), gwLatency);

  // 3. Net
  drawCascadeRow(72, "Net ", !inetDown, !lanDown && wifi_ok,
                 "8.8.8.8", inetLatency);

  // 4. DNS
  drawCascadeRow(94, "DNS ", !dnsDown, !inetDown && !lanDown && wifi_ok,
                 DNS_TARGET, dnsLatency);
}
```

**Step 2 : Appeler `drawCascade()` dans `loop()`**

Après `drawHeader()` :

```c
drawCascade();
```

**Step 3 : Compile + upload + vérifier**

```bash
./bin/arduino-cli compile --fqbn esp32:esp32:esp32 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino && \
./bin/arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB0 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino
```

Attendu : 4 lignes en dessous du header avec une LED ronde colorée + détails + latence ms. Toutes vertes en condition normale. Couper le WiFi → la première ligne passe rouge après ~10 s.

**Step 4 : Commit**

```bash
git add sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino
git commit -m "Internet_Monitor: panel cascade (4 couches avec LED + latence)"
```

---

## Task 5 : Stats panel + graphe latence

**Files:**
- Modify: `Internet_Monitor.ino`

**Step 1 : Ring buffer en globales**

```c
#define LATENCY_HISTORY_SIZE 120  // 10 min à 5s
int16_t latencyHistory[LATENCY_HISTORY_SIZE];
int latencyHead = 0;
bool latencyFull = false;

void pushLatency(int16_t v) {
  latencyHistory[latencyHead] = v;
  latencyHead = (latencyHead + 1) % LATENCY_HISTORY_SIZE;
  if (latencyHead == 0) latencyFull = true;
}
```

Initialiser dans `setup()` :

```c
for (int i = 0; i < LATENCY_HISTORY_SIZE; i++) latencyHistory[i] = 0;
```

Dans `checkCascade()`, après `currentState = ...`, push :

```c
if (currentState == OK) pushLatency(inetLatency);
else pushLatency(-1);  // marqueur d'échec
```

**Step 2 : Helpers stats display**

Ajouter avant `loop()` :

```c
void drawStats() {
  // Zone 120..200 (80px)
  tft.fillRect(0, 120, 320, 80, COLOR_BG);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextDatum(TL_DATUM);

  // Ligne stats
  char buf[64];
  snprintf(buf, sizeof(buf), "Uptime %.1f%%", uptimePct());
  tft.drawString(buf, 6, 124, 2);

  if (lastOutageDurationS > 0) {
    struct tm tm;
    localtime_r(&lastOutageStartEpoch, &tm);
    snprintf(buf, sizeof(buf), "Dern: %02d:%02d (%lum%lus)",
             tm.tm_hour, tm.tm_min,
             lastOutageDurationS / 60, lastOutageDurationS % 60);
  } else {
    snprintf(buf, sizeof(buf), "Aucune coupure");
  }
  tft.drawString(buf, 140, 124, 2);

  // Graphe latence (zone 6..314 x 148..198)
  tft.drawRect(6, 148, 308, 50, COLOR_HEADER);
  int barCount = LATENCY_HISTORY_SIZE;
  int barWidth = 308 / barCount;  // = 2 pixels
  int graphH = 48;

  // Trouver max latence (pour échelle)
  int maxLat = 50;
  for (int i = 0; i < LATENCY_HISTORY_SIZE; i++) {
    if (latencyHistory[i] > maxLat) maxLat = latencyHistory[i];
  }

  // Tracer du plus ancien au plus récent
  int startIdx = latencyFull ? latencyHead : 0;
  for (int i = 0; i < LATENCY_HISTORY_SIZE; i++) {
    int idx = (startIdx + i) % LATENCY_HISTORY_SIZE;
    int16_t v = latencyHistory[idx];
    int x = 7 + i * barWidth;
    if (v < 0) {
      // Échec : barre rouge plein cadre
      tft.fillRect(x, 149, barWidth, graphH, COLOR_KO);
    } else if (v > 0) {
      int h = (v * graphH) / maxLat;
      if (h < 1) h = 1;
      tft.fillRect(x, 149 + (graphH - h), barWidth, h, COLOR_OK);
    }
  }
}
```

**Step 3 : Appeler dans `loop()`**

Après `drawCascade()` :

```c
drawStats();
```

**Step 4 : Compile + upload + vérifier**

```bash
./bin/arduino-cli compile --fqbn esp32:esp32:esp32 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino && \
./bin/arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB0 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino
```

Attendu : panel stats avec uptime % + libellé "Aucune coupure" au début, et un graphe encadré qui se remplit progressivement de barres vertes. Une coupure simulée fait apparaître quelques barres rouges.

**Step 5 : Commit**

```bash
git add sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino
git commit -m "Internet_Monitor: panel stats (uptime + derniere coupure + graphe)"
```

---

## Task 6 : Audio (patterns DOWN + UP + boucle)

**Files:**
- Modify: `Internet_Monitor.ino`

**Step 1 : Setup LEDC + globales**

En haut du fichier :

```c
#define SPEAKER_PIN 26
#define BEEP_INTERVAL_MS 10000

unsigned long lastBeepMs = 0;
```

Dans `setup()`, avant la connexion WiFi :

```c
ledcAttach(SPEAKER_PIN, 1000, 8);
```

**Step 2 : Helpers audio**

Ajouter avant `loop()` :

```c
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
```

**Step 3 : Logique de déclenchement dans `loop()`**

Modifier le bloc des transitions (Task 2) pour déclencher les patterns :

```c
if (wasOk && !isOk) {
  outageStartEpoch = time(nullptr);
  outageStartMs = millis();
  playDownPattern();
  lastBeepMs = millis();
  Serial.printf("⚠ Coupure début à %ld\n", (long)outageStartEpoch);
}
if (!wasOk && isOk && previousState != CHECKING) {
  lastOutageStartEpoch = outageStartEpoch;
  lastOutageDurationS = (millis() - outageStartMs) / 1000;
  totalDowntimeMs += millis() - outageStartMs;
  playUpPattern();
  Serial.printf("✓ Retour réseau, coupure de %lus\n", lastOutageDurationS);
}
```

Et après le bloc transitions, ajouter la boucle de répétition :

```c
if (currentState != OK && currentState != CHECKING) {
  if (millis() - lastBeepMs >= BEEP_INTERVAL_MS) {
    playDownPattern();
    lastBeepMs = millis();
  }
}
```

**Step 4 : Compile + upload + vérifier**

```bash
./bin/arduino-cli compile --fqbn esp32:esp32:esp32 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino && \
./bin/arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB0 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino
```

Attendu : aucun son en condition normale. Couper le WiFi → 3 bips graves immédiatement, puis 3 bips toutes les 10 s. Reconnecter → 3 notes ascendantes une fois.

**Step 5 : Commit**

```bash
git add sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino
git commit -m "Internet_Monitor: alerte sonore (patterns DOWN/UP + repetition)"
```

---

## Task 7 : Touch + boutons silence

**Files:**
- Modify: `Internet_Monitor.ino`

**Step 1 : Includes + setup XPT2046**

En haut du fichier :

```c
#include <XPT2046_Touchscreen.h>

#define TOUCH_CS  33
#define TOUCH_IRQ 36

SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// Calibration approximative pour CYD en rotation 1
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3700
#define TOUCH_Y_MIN 240
#define TOUCH_Y_MAX 3800
```

Dans `setup()` après l'init `tft` :

```c
touchSPI.begin(25, 39, 32, TOUCH_CS);
ts.begin(touchSPI);
ts.setRotation(1);
```

**Step 2 : Globales silence**

```c
unsigned long silenceUntilMs = 0;
bool silencePermanent = false;
unsigned long lastTapMs = 0;

bool isSilenced() {
  if (silencePermanent) return true;
  if (silenceUntilMs > 0 && millis() < silenceUntilMs) return true;
  return false;
}

void setSilence5min()    { silenceUntilMs = millis() + 5UL * 60UL * 1000UL; silencePermanent = false; }
void setSilence30min()   { silenceUntilMs = millis() + 30UL * 60UL * 1000UL; silencePermanent = false; }
void setSilencePermanent() { silenceUntilMs = 0; silencePermanent = true; }
void clearSilence()      { silenceUntilMs = 0; silencePermanent = false; }
```

**Step 3 : Auto-clear silence à la transition DOWN→OK**

Dans le bloc `if (!wasOk && isOk ...)` ajouter :

```c
clearSilence();
```

**Step 4 : Suppression du bip si silence**

Modifier la boucle de répétition :

```c
if (currentState != OK && currentState != CHECKING && !isSilenced()) {
  if (millis() - lastBeepMs >= BEEP_INTERVAL_MS) {
    playDownPattern();
    lastBeepMs = millis();
  }
}
```

Et le pattern DOWN immédiat à la transition :

```c
if (wasOk && !isOk) {
  outageStartEpoch = time(nullptr);
  outageStartMs = millis();
  if (!isSilenced()) playDownPattern();
  lastBeepMs = millis();
  ...
}
```

**Step 5 : Rendu footer (boutons ou bandeau silence)**

```c
struct Rect { int x, y, w, h; };
const Rect btn5min   = {6,   206, 100, 32};
const Rect btn30min  = {110, 206, 100, 32};
const Rect btnPerm   = {214, 206, 100, 32};
const Rect bandeau   = {6,   206, 308, 32};

bool inRect(int x, int y, Rect r) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void drawButton(Rect r, const char* label, uint16_t color) {
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, color);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, COLOR_TEXT);
  tft.setTextColor(COLOR_TEXT, color);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, r.x + r.w / 2, r.y + r.h / 2, 2);
}

void drawFooter() {
  tft.fillRect(0, 200, 320, 40, COLOR_BG);
  if (isSilenced()) {
    char buf[64];
    if (silencePermanent) {
      snprintf(buf, sizeof(buf), "🔇 Silence permanent — tap pour reactiver");
    } else {
      unsigned long left = (silenceUntilMs - millis()) / 1000;
      snprintf(buf, sizeof(buf), "🔇 Silence %lum%02lus — tap pour reactiver",
               left / 60, left % 60);
    }
    tft.fillRoundRect(bandeau.x, bandeau.y, bandeau.w, bandeau.h, 6, COLOR_HEADER);
    tft.setTextColor(COLOR_TEXT, COLOR_HEADER);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(buf, bandeau.x + bandeau.w / 2, bandeau.y + bandeau.h / 2, 2);
  } else {
    drawButton(btn5min,  "5min",      COLOR_HEADER);
    drawButton(btn30min, "30min",     COLOR_HEADER);
    drawButton(btnPerm,  "Permanent", COLOR_HEADER);
  }
}
```

Note : si l'emoji 🔇 n'est pas rendu par la police, remplacer par `MUTE`.

**Step 6 : Hit-test + handling tap**

Avant `loop()` :

```c
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

  if (isSilenced()) {
    if (inRect(sx, sy, bandeau)) clearSilence();
  } else {
    if      (inRect(sx, sy, btn5min))  setSilence5min();
    else if (inRect(sx, sy, btn30min)) setSilence30min();
    else if (inRect(sx, sy, btnPerm))  setSilencePermanent();
  }
}
```

**Step 7 : Intégrer dans `loop()`**

Restructurer pour avoir un tick UI plus rapide que les checks (sinon l'horloge et le countdown ne se mettent à jour que toutes les 5 s) :

```c
unsigned long lastCheckMs = 0;

void loop() {
  handleTouch();

  if (millis() - lastCheckMs >= CHECK_INTERVAL_MS) {
    lastCheckMs = millis();
    checkCascade();

    bool wasOk = (previousState == OK);
    bool isOk = (currentState == OK);

    if (wasOk && !isOk) {
      outageStartEpoch = time(nullptr);
      outageStartMs = millis();
      if (!isSilenced()) playDownPattern();
      lastBeepMs = millis();
    }
    if (!wasOk && isOk && previousState != CHECKING) {
      lastOutageStartEpoch = outageStartEpoch;
      lastOutageDurationS = (millis() - outageStartMs) / 1000;
      totalDowntimeMs += millis() - outageStartMs;
      playUpPattern();
      clearSilence();
    }
    previousState = currentState;
  }

  if (currentState != OK && currentState != CHECKING && !isSilenced()) {
    if (millis() - lastBeepMs >= BEEP_INTERVAL_MS) {
      playDownPattern();
      lastBeepMs = millis();
    }
  }

  drawHeader();
  drawCascade();
  drawStats();
  drawFooter();
  delay(200);  // ~5fps UI
}
```

**Step 8 : Compile + upload + vérifier**

```bash
./bin/arduino-cli compile --fqbn esp32:esp32:esp32 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino && \
./bin/arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB0 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino
```

Tests à faire à la main :
- 3 boutons visibles en bas, vert/grisé
- Couper le WiFi → bips, taper "5min" → bandeau apparaît avec countdown, plus de bips
- Re-tap sur le bandeau → boutons reviennent, bips reprennent
- "Permanent" → bandeau permanent, retour au réseau l'efface

**Step 9 : Commit**

```bash
git add sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino
git commit -m "Internet_Monitor: tactile + boutons silence (5min/30min/permanent)"
```

---

## Task 8 : Polish + edges + intégration

**Files:**
- Modify: `Internet_Monitor.ino`
- Modify: `CLAUDE.md`

**Step 1 : Edge cases UI**

Dans `drawCascade()`, gérer la latence > 999 ms :

```c
char buf[16];
if (checked && ok) {
  if (latency > 999) snprintf(buf, sizeof(buf), ">999ms");
  else snprintf(buf, sizeof(buf), "%d ms", latency);
} else snprintf(buf, sizeof(buf), "-");
```

Dans `drawHeader()`, déjà géré (`--:--:--` si pas NTP).

**Step 2 : Anti-flicker du redessin**

Le redessin complet à 5fps peut faire scintiller. Limiter le redessin du `drawStats()` (graphe) à 1/sec et `drawCascade()` à chaque check (5s). Header (horloge) à 1/sec. Footer toujours.

```c
unsigned long lastUiSlowMs = 0, lastUiFastMs = 0;

// Dans loop, à la fin :
unsigned long now = millis();
if (now - lastUiFastMs >= 200) {  // 5 fps : header + footer
  drawHeader();
  drawFooter();
  lastUiFastMs = now;
}
if (now - lastUiSlowMs >= 1000) {  // 1 fps : stats + cascade
  drawCascade();
  drawStats();
  lastUiSlowMs = now;
}
```

Et supprimer le `delay(200)` final.

**Step 3 : Sync NTP périodique (1h)**

```c
unsigned long lastNtpSyncMs = 0;
// dans loop :
if (millis() - lastNtpSyncMs > 3600UL * 1000UL) {
  configTzTime(TZ_PARIS, NTP_SERVER);
  lastNtpSyncMs = millis();
}
```

**Step 4 : Mettre à jour CLAUDE.md**

Ajouter à la liste des sketches CYD :

```
- `Internet_Monitor/` - Moniteur connexion en cascade (WiFi/Box/Net/DNS) avec alerte sonore et boutons silence tactiles
```

(à insérer entre `Buzzer_Test/` et `Crypto_Tracker/`)

**Step 5 : Compile + upload + vérifier**

```bash
./bin/arduino-cli compile --fqbn esp32:esp32:esp32 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino && \
./bin/arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB0 \
    sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino
```

Test global : laisser tourner 5 min, vérifier que tout est fluide, sans scintillement, et que les transitions de coupure simulée font ce qui est attendu.

**Step 6 : Commit final**

```bash
git add sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino CLAUDE.md
git commit -m "Internet_Monitor: polish (edges, anti-flicker, NTP resync) + doc"
```

**Optionnel : squash en un commit unique** (pour cohérence avec le style du repo : un sketch = un commit) :

```bash
git rebase -i HEAD~9
# garder pick sur le premier, squash sur les autres
# message final : "CYD: ajouter Internet_Monitor (cascade + alerte sonore + silence tactile)"
```

---

## Acceptance criteria globaux

- [ ] Sketch compile sans warning bloquant, `<25%` flash, `<10%` RAM
- [ ] Au boot : connexion WiFi + sync NTP + écran initialisé en `<10s`
- [ ] Cascade détecte correctement chacune des 4 pannes (testées en débranchant WiFi, en pingouant fausse passerelle si possible)
- [ ] Hystérésis fonctionne : un seul check raté ne bascule pas en DOWN
- [ ] Bip répétitif présent à la coupure, silencé par chacun des 3 boutons
- [ ] Auto-clear silence au retour réseau
- [ ] Pattern UP joué au retour, même si silence permanent actif
- [ ] Uptime % et "dernière coupure" reflètent la réalité
- [ ] Graphe rempli de barres vertes (latence) et rouges (échecs)
