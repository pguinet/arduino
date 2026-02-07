/*
 * Crypto Tracker - ESP32-2432S028 (Cheap Yellow Display)
 *
 * Affiche les cours de cryptomonnaies avec graphique historique 7 jours.
 * Navigation tactile entre les differentes cryptos.
 * API: CoinGecko (gratuite, sans cle)
 *
 * Board: ESP32-2432S028 (Cheap Yellow Display)
 * FQBN: esp32:esp32:esp32
 *
 * @dependencies TFT_eSPI, XPT2046_Touchscreen, ArduinoJson
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "credentials.h"

// Backlight
#define TFT_BACKLIGHT 21

// Pins tactile XPT2046
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK  25

// Calibration tactile
#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3800
#define TOUCH_MIN_Y 200
#define TOUCH_MAX_Y 3800

// Bus SPI pour le tactile
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

TFT_eSPI tft = TFT_eSPI();

// Liste des cryptos a suivre
struct Crypto {
  const char* id;       // ID CoinGecko
  const char* symbol;   // Symbole affiche
  const char* name;     // Nom complet
  uint16_t color;       // Couleur du graphique
  float price;
  float change24h;
  float chartData[50];  // Historique pour le graphique
  int chartPoints;
  bool dataValid;
};

#define NUM_CRYPTOS 6
Crypto cryptos[NUM_CRYPTOS] = {
  {"bitcoin",  "BTC", "Bitcoin",  TFT_ORANGE, 0, 0, {}, 0, false},
  {"ethereum", "ETH", "Ethereum", TFT_CYAN,   0, 0, {}, 0, false},
  {"solana",   "SOL", "Solana",   TFT_MAGENTA,0, 0, {}, 0, false},
  {"cardano",  "ADA", "Cardano",  TFT_BLUE,   0, 0, {}, 0, false},
  {"ripple",   "XRP", "Ripple",   TFT_DARKGREY, 0, 0, {}, 0, false},
  {"dogecoin", "DOGE","Dogecoin", TFT_YELLOW, 0, 0, {}, 0, false}
};

int currentCrypto = 0;
unsigned long lastUpdate = 0;
unsigned long lastTouch = 0;
unsigned long lastInteraction = 0;
unsigned long lastAutoRotate = 0;
#define UPDATE_INTERVAL 60000   // 60 secondes
#define TOUCH_DEBOUNCE 300
#define AUTO_ROTATE_DELAY 10000 // 10 secondes d'inactivite avant rotation
#define AUTO_ROTATE_INTERVAL 5000 // 5 secondes entre chaque rotation

// Dimensions ecran (mode paysage)
#define SCREEN_W 320
#define SCREEN_H 240

// Zones UI
#define HEADER_H 40
#define FOOTER_H 50
#define CHART_Y (HEADER_H + 10)
#define CHART_H (SCREEN_H - HEADER_H - FOOTER_H - 20)
#define CHART_W 280
#define CHART_X 20

// Boutons navigation
#define BTN_W 50
#define BTN_H 40
#define BTN_PREV_X 10
#define BTN_NEXT_X (SCREEN_W - BTN_W - 10)
#define BTN_Y (SCREEN_H - FOOTER_H + 5)

void setup() {
  Serial.begin(115200);
  Serial.println("Crypto Tracker - ESP32-2432S028");

  // Backlight
  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);

  // Init ecran
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // Init tactile
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  // Ecran de connexion WiFi
  drawConnecting();
  connectWiFi();

  // Premier chargement
  drawUI();
  fetchAllPrices();
  fetchChartData(currentCrypto);
  drawCrypto();

  // Initialiser les timers
  lastInteraction = millis();
  lastAutoRotate = millis();
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    tft.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connecte");
    Serial.println(WiFi.localIP());
  }
}

void drawConnecting() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(60, 100);
  tft.print("Connexion WiFi");
  tft.setCursor(60, 130);
}

void drawUI() {
  tft.fillScreen(TFT_BLACK);

  // Bouton precedent
  tft.fillRoundRect(BTN_PREV_X, BTN_Y, BTN_W, BTN_H, 8, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.setCursor(BTN_PREV_X + 15, BTN_Y + 8);
  tft.print("<");

  // Bouton suivant
  tft.fillRoundRect(BTN_NEXT_X, BTN_Y, BTN_W, BTN_H, 8, TFT_DARKGREY);
  tft.setCursor(BTN_NEXT_X + 15, BTN_Y + 8);
  tft.print(">");

  // Indicateur de position (dots)
  drawDots();
}

void drawDots() {
  int dotY = BTN_Y + BTN_H / 2;
  int totalWidth = NUM_CRYPTOS * 12;
  int startX = (SCREEN_W - totalWidth) / 2;

  for (int i = 0; i < NUM_CRYPTOS; i++) {
    int x = startX + i * 12;
    if (i == currentCrypto) {
      tft.fillCircle(x, dotY, 4, cryptos[i].color);
    } else {
      tft.drawCircle(x, dotY, 3, TFT_DARKGREY);
    }
  }
}

void drawCrypto() {
  Crypto& c = cryptos[currentCrypto];

  // Effacer zone principale (garder footer)
  tft.fillRect(0, 0, SCREEN_W, SCREEN_H - FOOTER_H, TFT_BLACK);

  // Header: symbole et nom
  tft.setTextColor(c.color);
  tft.setTextSize(3);
  tft.setCursor(10, 8);
  tft.print(c.symbol);

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(80, 12);
  tft.print(c.name);

  if (!c.dataValid) {
    tft.setTextColor(TFT_DARKGREY);
    tft.setTextSize(2);
    tft.setCursor(100, 100);
    tft.print("Chargement...");
    return;
  }

  // Prix
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  char priceStr[20];
  if (c.price >= 1000) {
    sprintf(priceStr, "$%.2f", c.price);
  } else if (c.price >= 1) {
    sprintf(priceStr, "$%.4f", c.price);
  } else {
    sprintf(priceStr, "$%.6f", c.price);
  }

  // Afficher le prix en grand a droite du header
  tft.setTextSize(2);
  int priceWidth = strlen(priceStr) * 12;
  tft.setCursor(SCREEN_W - priceWidth - 10, 12);
  tft.print(priceStr);

  // Variation 24h
  uint16_t changeColor = (c.change24h >= 0) ? TFT_GREEN : TFT_RED;
  tft.setTextColor(changeColor);
  tft.setTextSize(2);

  char changeStr[15];
  if (c.change24h >= 0) {
    sprintf(changeStr, "+%.2f%%", c.change24h);
  } else {
    sprintf(changeStr, "%.2f%%", c.change24h);
  }

  tft.setCursor(SCREEN_W - 90, HEADER_H + 5);
  tft.print(changeStr);

  // Dessiner le graphique
  drawChart();

  // Mettre a jour les dots
  tft.fillRect(60, BTN_Y, 200, BTN_H, TFT_BLACK);
  drawDots();
}

void drawChart() {
  Crypto& c = cryptos[currentCrypto];

  if (c.chartPoints < 2) return;

  // Cadre du graphique
  tft.drawRect(CHART_X - 1, CHART_Y - 1, CHART_W + 2, CHART_H + 2, TFT_DARKGREY);

  // Trouver min/max
  float minVal = c.chartData[0];
  float maxVal = c.chartData[0];
  for (int i = 1; i < c.chartPoints; i++) {
    if (c.chartData[i] < minVal) minVal = c.chartData[i];
    if (c.chartData[i] > maxVal) maxVal = c.chartData[i];
  }

  // Ajouter une marge de 5%
  float range = maxVal - minVal;
  if (range < 0.01) range = 0.01;
  minVal -= range * 0.05;
  maxVal += range * 0.05;

  // Lignes de grille horizontales
  for (int i = 0; i <= 4; i++) {
    int y = CHART_Y + (CHART_H * i / 4);
    tft.drawFastHLine(CHART_X, y, CHART_W, TFT_DARKGREY);
  }

  // Dessiner la courbe
  int prevX = 0, prevY = 0;
  for (int i = 0; i < c.chartPoints; i++) {
    int x = CHART_X + (i * CHART_W / (c.chartPoints - 1));
    int y = CHART_Y + CHART_H - (int)((c.chartData[i] - minVal) / (maxVal - minVal) * CHART_H);

    if (i > 0) {
      tft.drawLine(prevX, prevY, x, y, c.color);
      // Ligne plus epaisse
      tft.drawLine(prevX, prevY + 1, x, y + 1, c.color);
    }

    prevX = x;
    prevY = y;
  }

  // Labels min/max
  tft.setTextColor(TFT_DARKGREY);
  tft.setTextSize(1);

  char minStr[15], maxStr[15];
  formatPrice(maxVal, maxStr);
  formatPrice(minVal, minStr);

  tft.setCursor(CHART_X + 5, CHART_Y + 5);
  tft.print(maxStr);
  tft.setCursor(CHART_X + 5, CHART_Y + CHART_H - 10);
  tft.print(minStr);

  // Label "7 jours"
  tft.setCursor(CHART_X + CHART_W - 50, CHART_Y + CHART_H - 10);
  tft.print("7 jours");
}

void formatPrice(float price, char* buf) {
  if (price >= 10000) {
    sprintf(buf, "$%.0f", price);
  } else if (price >= 100) {
    sprintf(buf, "$%.1f", price);
  } else if (price >= 1) {
    sprintf(buf, "$%.2f", price);
  } else {
    sprintf(buf, "$%.4f", price);
  }
}

void fetchAllPrices() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;

  // Construire la liste des IDs
  String ids = "";
  for (int i = 0; i < NUM_CRYPTOS; i++) {
    if (i > 0) ids += ",";
    ids += cryptos[i].id;
  }

  String url = "https://api.coingecko.com/api/v3/simple/price?ids=" + ids +
               "&vs_currencies=usd&include_24hr_change=true";

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      for (int i = 0; i < NUM_CRYPTOS; i++) {
        JsonObject coin = doc[cryptos[i].id];
        if (!coin.isNull()) {
          cryptos[i].price = coin["usd"].as<float>();
          cryptos[i].change24h = coin["usd_24h_change"].as<float>();
          cryptos[i].dataValid = true;
          Serial.printf("%s: $%.2f (%.2f%%)\n",
            cryptos[i].symbol, cryptos[i].price, cryptos[i].change24h);
        }
      }
    }
  } else {
    Serial.printf("HTTP error: %d\n", httpCode);
  }

  http.end();
}

void fetchChartData(int index) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (index < 0 || index >= NUM_CRYPTOS) return;

  Crypto& c = cryptos[index];

  // Afficher indicateur de chargement
  tft.setTextColor(TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setCursor(CHART_X + CHART_W - 30, CHART_Y + 5);
  tft.print("...");

  HTTPClient http;

  String url = "https://api.coingecko.com/api/v3/coins/" + String(c.id) +
               "/market_chart?vs_currency=usd&days=7";

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();

    // Parser le JSON (structure: {"prices": [[timestamp, price], ...]})
    DynamicJsonDocument doc(32768);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      JsonArray prices = doc["prices"];
      int total = prices.size();

      // Echantillonner pour avoir ~50 points
      int step = max(1, total / 50);
      c.chartPoints = 0;

      for (int i = 0; i < total && c.chartPoints < 50; i += step) {
        c.chartData[c.chartPoints++] = prices[i][1].as<float>();
      }

      Serial.printf("%s: %d points charges\n", c.symbol, c.chartPoints);
    }
  } else {
    Serial.printf("Chart HTTP error: %d\n", httpCode);
  }

  http.end();
}

void handleTouch() {
  if (!ts.touched()) return;

  unsigned long now = millis();
  if (now - lastTouch < TOUCH_DEBOUNCE) return;
  lastTouch = now;
  lastInteraction = now;  // Reset auto-rotate timer

  TS_Point p = ts.getPoint();

  // Mapper les coordonnees
  int x = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_W);
  int y = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_H);

  x = constrain(x, 0, SCREEN_W - 1);
  y = constrain(y, 0, SCREEN_H - 1);

  Serial.printf("Touch: %d, %d\n", x, y);

  // Bouton precedent
  if (x < BTN_PREV_X + BTN_W + 20 && y > BTN_Y - 10) {
    // Animation bouton
    tft.fillRoundRect(BTN_PREV_X, BTN_Y, BTN_W, BTN_H, 8, TFT_BLUE);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(3);
    tft.setCursor(BTN_PREV_X + 15, BTN_Y + 8);
    tft.print("<");

    currentCrypto = (currentCrypto - 1 + NUM_CRYPTOS) % NUM_CRYPTOS;
    delay(100);

    // Restaurer bouton
    tft.fillRoundRect(BTN_PREV_X, BTN_Y, BTN_W, BTN_H, 8, TFT_DARKGREY);
    tft.setCursor(BTN_PREV_X + 15, BTN_Y + 8);
    tft.print("<");

    fetchChartData(currentCrypto);
    drawCrypto();
  }
  // Bouton suivant
  else if (x > BTN_NEXT_X - 20 && y > BTN_Y - 10) {
    // Animation bouton
    tft.fillRoundRect(BTN_NEXT_X, BTN_Y, BTN_W, BTN_H, 8, TFT_BLUE);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(3);
    tft.setCursor(BTN_NEXT_X + 15, BTN_Y + 8);
    tft.print(">");

    currentCrypto = (currentCrypto + 1) % NUM_CRYPTOS;
    delay(100);

    // Restaurer bouton
    tft.fillRoundRect(BTN_NEXT_X, BTN_Y, BTN_W, BTN_H, 8, TFT_DARKGREY);
    tft.setCursor(BTN_NEXT_X + 15, BTN_Y + 8);
    tft.print(">");

    fetchChartData(currentCrypto);
    drawCrypto();
  }
}

void loop() {
  handleTouch();

  unsigned long now = millis();

  // Rotation automatique si inactif
  if (now - lastInteraction > AUTO_ROTATE_DELAY) {
    if (now - lastAutoRotate > AUTO_ROTATE_INTERVAL) {
      lastAutoRotate = now;
      currentCrypto = (currentCrypto + 1) % NUM_CRYPTOS;
      fetchChartData(currentCrypto);
      drawCrypto();
    }
  }

  // Mise a jour periodique des prix
  if (now - lastUpdate > UPDATE_INTERVAL) {
    lastUpdate = now;
    fetchAllPrices();
    fetchChartData(currentCrypto);
    drawCrypto();
  }

  delay(50);
}
