/*
 * Matrix_Scroller - WEMOS D1 R32 + bande WS2812B en matrice 60x10 serpentin
 *
 * Affiche un texte qui defile sur une matrice 60x10 LEDs adressables
 * cablees en serpentin. Le texte, la couleur, la luminosite et la vitesse
 * sont configurables via une interface web.
 *
 * Cablage suppose : pixel 0 en haut a gauche, premiere ligne va vers la
 * droite, deuxieme vers la gauche, etc. (zigzag par lignes).
 * Si l'affichage est inverse, modifier les flags du constructeur matrix.
 *
 * Board: WEMOS D1 R32 (ESP32)
 * FQBN: esp32:esp32:d1_uno32
 *
 * @dependencies Adafruit NeoPixel, Adafruit NeoMatrix, Adafruit GFX Library
 */

#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#include "credentials.h"

#define LED_PIN     16
#define MAT_WIDTH   60
#define MAT_HEIGHT  10
#define TZ_PARIS    "CET-1CEST,M3.5.0,M10.5.0/3"

Adafruit_NeoMatrix matrix(MAT_WIDTH, MAT_HEIGHT, LED_PIN,
  NEO_MATRIX_TOP + NEO_MATRIX_LEFT +
  NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800);

WebServer server(80);

String   userText      = "Bonjour Club Domontois !";
uint8_t  brightness    = 30;            // 0-255 (limite la conso !)
uint16_t scrollMs      = 70;            // delai entre deux deplacements d'1 px
uint8_t  colR = 255, colG = 140, colB = 0;  // orange par defaut
uint16_t timeIntervalS = 30;            // 0 = ne pas afficher l'heure

const char* JOURS[] = {"Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"};
const char* MOIS[]  = {"jan", "fev", "mars", "avril", "mai", "juin",
                       "juillet", "aout", "sept", "oct", "nov", "dec"};

// Etat du defilement
String        currentText  = userText;
bool          showingTime  = false;
unsigned long lastTimeShown = 0;
int           scrollX       = MAT_WIDTH;
unsigned long lastScroll    = 0;

String formatTimeString() {
  struct tm t;
  if (!getLocalTime(&t, 100)) return "Heure non synchronisee";
  char buf[64];
  snprintf(buf, sizeof(buf), "%s %d %s - %02d:%02d",
           JOURS[t.tm_wday], t.tm_mday, MOIS[t.tm_mon],
           t.tm_hour, t.tm_min);
  return String(buf);
}

int textPixelWidth(const String& s) {
  return s.length() * 6;  // font 5x7 + 1 px d'espace
}

String htmlPage() {
  String s;
  s.reserve(2500);
  s += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>Matrix Scroller</title><style>"
         "body{font-family:sans-serif;background:#1e1e2e;color:#cdd6f4;margin:0;padding:20px;max-width:600px;margin:auto;}"
         "h1{color:#89b4fa;}"
         ".card{background:#313244;padding:20px;border-radius:10px;margin:15px 0;}"
         "label{display:block;margin:10px 0 5px;font-weight:bold;}"
         "input[type=text]{width:100%;padding:10px;border-radius:6px;border:none;background:#45475a;color:#cdd6f4;font-size:16px;box-sizing:border-box;}"
         "input[type=range]{width:100%;}"
         "input[type=color]{width:80px;height:40px;border:none;border-radius:6px;cursor:pointer;}"
         "button{background:#89b4fa;color:#1e1e2e;padding:12px 24px;border:none;border-radius:6px;font-weight:bold;font-size:16px;cursor:pointer;width:100%;margin-top:15px;}"
         "button:hover{background:#74c7ec;}"
         ".val{color:#a6e3a1;float:right;}"
         "</style></head><body>");

  s += F("<h1>Matrix Scroller 60x10</h1>");
  s += F("<form method='POST' action='/set'>");

  s += F("<div class='card'><label>Texte</label>"
         "<input type='text' name='t' maxlength='120' value='");
  s += userText;
  s += F("'></div>");

  char buf[8];
  snprintf(buf, sizeof(buf), "#%02X%02X%02X", colR, colG, colB);
  s += F("<div class='card'><label>Couleur</label>"
         "<input type='color' name='c' value='");
  s += buf;
  s += F("'></div>");

  s += F("<div class='card'><label>Luminosite <span class='val'>");
  s += brightness;
  s += F("</span></label>"
         "<input type='range' name='b' min='5' max='150' value='");
  s += brightness;
  s += F("'><small>Limite a 150 pour proteger l'alim (600 LEDs).</small></div>");

  s += F("<div class='card'><label>Vitesse (ms par px) <span class='val'>");
  s += scrollMs;
  s += F("</span></label>"
         "<input type='range' name='s' min='20' max='300' value='");
  s += scrollMs;
  s += F("'><small>Plus petit = plus rapide.</small></div>");

  s += F("<div class='card'><label>Intervalle date+heure (s) <span class='val'>");
  s += timeIntervalS;
  s += F("</span></label>"
         "<input type='range' name='i' min='0' max='300' value='");
  s += timeIntervalS;
  s += F("'><small>0 = jamais. Sinon, intercale la date+heure (NTP) tous les N secondes.</small></div>");

  s += F("<button type='submit'>Appliquer</button></form></body></html>");
  return s;
}

uint8_t hex2(const String& s, int i) {
  auto v = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  return (v(s[i]) << 4) | v(s[i + 1]);
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", htmlPage());
}

void handleSet() {
  if (server.hasArg("t")) {
    userText = server.arg("t");
    if (!showingTime) currentText = userText;
  }
  if (server.hasArg("c")) {
    String c = server.arg("c");  // format "#RRGGBB" ou "%23RRGGBB"
    int hash = c.indexOf('#');
    if (hash >= 0 && c.length() >= hash + 7) {
      colR = hex2(c, hash + 1);
      colG = hex2(c, hash + 3);
      colB = hex2(c, hash + 5);
    }
  }
  if (server.hasArg("b")) {
    brightness = constrain(server.arg("b").toInt(), 5, 150);
    matrix.setBrightness(brightness);
  }
  if (server.hasArg("s")) {
    scrollMs = constrain(server.arg("s").toInt(), 20, 300);
  }
  if (server.hasArg("i")) {
    timeIntervalS = constrain(server.arg("i").toInt(), 0, 300);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== Matrix Scroller - D1 R32 ===");

  matrix.begin();
  matrix.setBrightness(brightness);
  matrix.setTextWrap(false);
  matrix.fillScreen(0);
  matrix.show();

  Serial.print("Connexion a ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connecte. Ouvre : http://");
  Serial.println(WiFi.localIP());

  // Affiche IP + port pendant ~5s au boot. L'IP complete tient pas sur 60 px
  // en font 5x7, alors on alterne 3 ecrans centres : IP partie 1, partie 2, port.
  matrix.setTextColor(matrix.Color(0, 200, 100));
  String ipStr = WiFi.localIP().toString();
  int dot2 = ipStr.indexOf('.', ipStr.indexOf('.') + 1);  // 2eme point
  String screens[3] = {
    ipStr.substring(0, dot2),       // "192.168"
    ipStr.substring(dot2 + 1),      // "0.214"
    "Port 80"
  };
  const uint16_t durations[3] = {1700, 1700, 1600};
  for (int i = 0; i < 3; i++) {
    matrix.fillScreen(0);
    int textW = screens[i].length() * 6;
    matrix.setCursor((MAT_WIDTH - textW) / 2, 1);
    matrix.print(screens[i]);
    matrix.show();
    delay(durations[i]);
  }
  matrix.fillScreen(0);
  matrix.show();

  // Sync NTP (Europe/Paris)
  configTime(0, 0, "pool.ntp.org", "time.google.com", "fr.pool.ntp.org");
  setenv("TZ", TZ_PARIS, 1);
  tzset();

  server.on("/", handleRoot);
  server.on("/set", HTTP_POST, handleSet);
  server.begin();
}

void loop() {
  server.handleClient();

  if (millis() - lastScroll >= scrollMs) {
    lastScroll = millis();
    matrix.fillScreen(0);
    matrix.setCursor(scrollX, 1);
    matrix.setTextColor(matrix.Color(colR, colG, colB));
    matrix.print(currentText);
    matrix.show();

    scrollX--;
    if (scrollX < -textPixelWidth(currentText)) {
      // Fin du cycle : on decide quoi afficher au prochain tour
      scrollX = MAT_WIDTH;
      if (showingTime) {
        showingTime  = false;
        currentText  = userText;
        lastTimeShown = millis();
      } else if (timeIntervalS > 0 &&
                 millis() - lastTimeShown >= (unsigned long)timeIntervalS * 1000UL) {
        currentText  = formatTimeString();
        showingTime  = true;
      } else {
        currentText  = userText;  // ressynchronise si modifie
      }
    }
  }
}
