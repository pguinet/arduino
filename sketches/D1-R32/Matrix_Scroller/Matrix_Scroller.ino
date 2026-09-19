/*
 * Matrix_Scroller - WEMOS D1 R32 + bande WS2812B en matrice 60x10 serpentin
 *
 * Affiche un texte qui defile sur une matrice 60x10 LEDs adressables
 * cablees en serpentin. Le texte, la couleur, la luminosite et la vitesse
 * sont configurables via une interface web.
 *
 * L'affichage ne depend jamais du reseau : le defilement demarre des le boot,
 * la connexion WiFi se fait en tache de fond. Sans reseau au bout de 15 s,
 * la carte ouvre son propre point d'acces avec portail captif pour rester
 * configurable (cas du forum des associations, sans WiFi sur place).
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
#include <DNSServer.h>
#include <time.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#include "credentials.h"

#define LED_PIN     16
#define MAT_WIDTH   60
#define MAT_HEIGHT  10
#define TZ_PARIS    "CET-1CEST,M3.5.0,M10.5.0/3"

// Delai avant de renoncer au WiFi et d'ouvrir le point d'acces de secours
#define WIFI_TIMEOUT_MS 15000UL
// Vitesse de defilement des messages d'info reseau (plus rapide que le texte)
#define INFO_SCROLL_MS  35

// Point d'acces de secours. Surchargeable dans credentials.h.
#ifndef AP_SSID
#define AP_SSID     "Afficheur-CID"
#endif
#ifndef AP_PASSWORD
#define AP_PASSWORD ""   // moins de 8 caracteres = reseau ouvert
#endif

Adafruit_NeoMatrix matrix(MAT_WIDTH, MAT_HEIGHT, LED_PIN,
  NEO_MATRIX_TOP + NEO_MATRIX_LEFT +
  NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800);

WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

String   userText      = "Bonjour Club Domontois !";
uint8_t  brightness    = 30;            // 0-255 (limite la conso !)
uint16_t scrollMs      = 70;            // delai entre deux deplacements d'1 px
uint8_t  colR = 255, colG = 140, colB = 0;  // orange par defaut
uint16_t timeIntervalS = 30;            // 0 = ne pas afficher l'heure

void loadPrefs() {
  prefs.begin("matrix", true);  // read-only
  userText      = prefs.getString("text",     userText);
  brightness    = prefs.getUChar ("bright",   brightness);
  scrollMs      = prefs.getUShort("scroll",   scrollMs);
  colR          = prefs.getUChar ("colR",     colR);
  colG          = prefs.getUChar ("colG",     colG);
  colB          = prefs.getUChar ("colB",     colB);
  timeIntervalS = prefs.getUShort("interval", timeIntervalS);
  prefs.end();
}

void savePrefs() {
  prefs.begin("matrix", false);  // read-write
  prefs.putString("text",     userText);
  prefs.putUChar ("bright",   brightness);
  prefs.putUShort("scroll",   scrollMs);
  prefs.putUChar ("colR",     colR);
  prefs.putUChar ("colG",     colG);
  prefs.putUChar ("colB",     colB);
  prefs.putUShort("interval", timeIntervalS);
  prefs.end();
}

const char* JOURS[] = {"Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"};
const char* MOIS[]  = {"jan", "fev", "mars", "avril", "mai", "juin",
                       "juillet", "aout", "sept", "oct", "nov", "dec"};

// Etat du reseau. L'afficheur tourne dans les trois cas.
enum NetState { NET_CONNECTING, NET_STA, NET_AP };
NetState      netState     = NET_CONNECTING;
unsigned long wifiStartMs  = 0;
bool          serverStarted = false;

// Etat du defilement
String        currentText  = userText;
bool          showingTime  = false;
bool          showingInfo  = false;   // message reseau, passe une seule fois
String        infoText     = "";
unsigned long lastTimeShown = 0;
int           scrollX       = MAT_WIDTH;
unsigned long lastScroll    = 0;

void queueInfo(const String& s) {
  infoText = s;
  Serial.println(s);
}

bool timeIsSynced() {
  struct tm t;
  if (!getLocalTime(&t, 5)) return false;
  return t.tm_year > (2021 - 1900);  // 1970 tant que NTP n'a pas repondu
}

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

String netStatusLine() {
  switch (netState) {
    case NET_STA:
      return String("Connecte a ") + WIFI_SSID + " - " + WiFi.localIP().toString();
    case NET_AP:
      return String("Point d'acces ") + AP_SSID + " (pas de WiFi trouve) - " +
             WiFi.softAPIP().toString();
    default:
      return String("Connexion a ") + WIFI_SSID + " en cours...";
  }
}

String htmlPage() {
  String s;
  s.reserve(2700);
  s += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>Matrix Scroller</title><style>"
         "body{font-family:sans-serif;background:#1e1e2e;color:#cdd6f4;margin:0;padding:20px;max-width:600px;margin:auto;}"
         "h1{color:#89b4fa;}"
         ".card{background:#313244;padding:20px;border-radius:10px;margin:15px 0;}"
         ".net{background:#313244;padding:10px 15px;border-radius:8px;font-size:14px;color:#a6adc8;}"
         "label{display:block;margin:10px 0 5px;font-weight:bold;}"
         "input[type=text]{width:100%;padding:10px;border-radius:6px;border:none;background:#45475a;color:#cdd6f4;font-size:16px;box-sizing:border-box;}"
         "input[type=range]{width:100%;}"
         "input[type=color]{width:80px;height:40px;border:none;border-radius:6px;cursor:pointer;}"
         "button{background:#89b4fa;color:#1e1e2e;padding:12px 24px;border:none;border-radius:6px;font-weight:bold;font-size:16px;cursor:pointer;width:100%;margin-top:15px;}"
         "button:hover{background:#74c7ec;}"
         ".val{color:#a6e3a1;float:right;}"
         "</style></head><body>");

  s += F("<h1>Matrix Scroller 60x10</h1>");
  s += F("<div class='net'>");
  s += netStatusLine();
  s += F("</div>");
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
  s += F("'><small>0 = jamais. Sinon, intercale la date+heure (NTP) tous les N secondes. "
         "Ignore tant que l'heure n'est pas synchronisee (mode point d'acces).</small></div>");

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
    if (!showingTime && !showingInfo) currentText = userText;
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
  savePrefs();
  server.sendHeader("Location", "/");
  server.send(303);
}

void startServer() {
  if (serverStarted) return;
  server.on("/", handleRoot);
  server.on("/set", HTTP_POST, handleSet);
  server.onNotFound(handleRoot);  // portail captif : toute URL renvoie la config
  server.begin();
  serverStarted = true;
}

// Plus de WiFi utilisable : on devient nous-meme le reseau.
void startAccessPoint() {
  Serial.println("Pas de WiFi : bascule en point d'acces.");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  const char* pass = (sizeof(AP_PASSWORD) - 1 >= 8) ? AP_PASSWORD : nullptr;
  WiFi.softAP(AP_SSID, pass);
  IPAddress ip = WiFi.softAPIP();
  dnsServer.start(53, "*", ip);  // toutes les requetes DNS pointent sur nous
  netState = NET_AP;
  startServer();
  queueInfo(String("Reseau ") + AP_SSID + " puis http://" + ip.toString());
}

// Machine a etats non bloquante : le defilement continue pendant ce temps.
void handleNetwork() {
  if (netState == NET_AP) {
    dnsServer.processNextRequest();
    return;
  }
  if (netState == NET_STA) return;

  if (WiFi.status() == WL_CONNECTED) {
    netState = NET_STA;
    configTime(0, 0, "pool.ntp.org", "time.google.com", "fr.pool.ntp.org");
    setenv("TZ", TZ_PARIS, 1);
    tzset();
    startServer();
    queueInfo("IP " + WiFi.localIP().toString() + " - port 80");
    return;
  }
  if (millis() - wifiStartMs >= WIFI_TIMEOUT_MS) startAccessPoint();
}

// Choisit le message du prochain cycle de defilement.
void nextMessage() {
  if (showingInfo) {
    showingInfo = false;
    infoText    = "";
  } else if (showingTime) {
    showingTime   = false;
    lastTimeShown = millis();
  }

  if (infoText.length()) {            // info reseau : prioritaire, passe une fois
    currentText = infoText;
    showingInfo = true;
  } else if (timeIntervalS > 0 && timeIsSynced() &&
             millis() - lastTimeShown >= (unsigned long)timeIntervalS * 1000UL) {
    currentText = formatTimeString();
    showingTime = true;
  } else {
    currentText = userText;           // ressynchronise si modifie
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== Matrix Scroller - D1 R32 ===");

  loadPrefs();
  currentText = userText;

  matrix.begin();
  matrix.setBrightness(brightness);
  matrix.setTextWrap(false);
  matrix.fillScreen(0);
  matrix.show();

  // WiFi non bloquant : l'afficheur doit tourner meme sans reseau du tout.
  Serial.print("Connexion a ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiStartMs = millis();
}

void loop() {
  handleNetwork();
  if (serverStarted) server.handleClient();

  unsigned long stepMs = showingInfo ? INFO_SCROLL_MS : scrollMs;
  if (millis() - lastScroll >= stepMs) {
    lastScroll = millis();
    matrix.fillScreen(0);
    matrix.setCursor(scrollX, 1);
    matrix.setTextColor(showingInfo ? matrix.Color(0, 200, 100)
                                    : matrix.Color(colR, colG, colB));
    matrix.print(currentText);
    matrix.show();

    scrollX--;
    if (scrollX < -textPixelWidth(currentText)) {
      scrollX = MAT_WIDTH;
      nextMessage();
    }
  }
}
