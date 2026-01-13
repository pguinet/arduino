/*
 * Bus Tracker - HW-364B (ESP8266 + OLED integre)
 *
 * Affiche les prochains passages de bus via l'API PRIM
 * Ile-de-France Mobilites.
 * Configuration possible via interface web.
 *
 * Pins OLED sur HW-364B:
 *   SDA -> GPIO14 (D5)
 *   SCL -> GPIO12 (D6)
 *
 * Board: NodeMCU 1.0 (ESP-12E Module)
 * FQBN: esp8266:esp8266:nodemcuv2
 *
 * @dependencies U8g2, ArduinoJson
 */

#include <U8g2lib.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <time.h>
#include "credentials.h"
#include "prim_config.h"

// Configuration OLED
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(
  U8G2_R0,
  /* SCL */ 12,
  /* SDA */ 14,
  U8X8_PIN_NONE
);

#define YELLOW_ZONE_HEIGHT 16
#define MAX_DEPARTURES 2

// Intervalles selon plages horaires (en millisecondes)
#define INTERVAL_RUSH_HOUR 60000      // 1 minute pendant heures de pointe
#define INTERVAL_NORMAL 120000         // 2 minutes en heures creuses
#define NIGHT_START_HOUR 20            // Debut mode nuit (20h00)
#define NIGHT_END_HOUR 6                // Fin du mode nuit (06h00)

// Variables pour gestion du mode nuit
bool nightMode = false;
bool displayOff = false;

// Serveur web
ESP8266WebServer server(80);

// Configuration (sauvegardee en EEPROM)
#define CONFIG_MAGIC 0xB056  // Magic number pour detecter config valide (incremente pour reset)
struct Config {
  uint16_t magic;        // Magic number pour validation
  char stopId[20];       // Ex: "413248"
  char stopName[30];     // Ex: "Marechal Foch"
  char lineName[10];     // Ex: "269"
  char lineRef[12];      // Ex: "C01252" (ID ligne pour filtrage)
  char direction[40];    // Ex: "Garges-Sarcelles RER"
} config;

// Donnees des prochains passages
struct Departure {
  int minutesLeft;
  char destination[30];
  bool atStop;
};

Departure departures[MAX_DEPARTURES];
int departureCount = 0;
bool dataValid = false;
unsigned long lastUpdate = 0;
char lastUpdateTime[10] = "--:--";
char errorMsg[50] = "";

// WiFi client
WiFiClientSecure client;

void loadConfig() {
  EEPROM.begin(sizeof(Config));
  EEPROM.get(0, config);

  // Verifier le magic number pour detecter une config corrompue
  if (config.magic != CONFIG_MAGIC || strlen(config.stopId) == 0) {
    Serial.println("Config invalide, reinitialisation...");
    // Config par defaut: Marechal Foch -> Garges-Sarcelles RER (ligne 269)
    config.magic = CONFIG_MAGIC;
    strcpy(config.stopId, "413248");
    strcpy(config.stopName, "Marechal Foch");
    strcpy(config.lineName, "269");
    strcpy(config.lineRef, "C01252");
    strcpy(config.direction, "Garges-Sarcelles RER");
    saveConfig();
  }
}

void saveConfig() {
  config.magic = CONFIG_MAGIC;
  EEPROM.put(0, config);
  EEPROM.commit();
}

void setupWiFi() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  const char* title = "Bus Tracker";
  int x = (128 - u8g2.getStrWidth(title)) / 2;
  u8g2.drawStr(x, 13, title);
  u8g2.drawHLine(0, YELLOW_ZONE_HEIGHT, 128);
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(5, 35, "Connexion WiFi...");
  u8g2.sendBuffer();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    u8g2.drawStr(5, 50, WiFi.localIP().toString().c_str());
    u8g2.sendBuffer();

    // Config NTP (timezone Paris avec gestion auto ete/hiver)
    configTime(0, 0, "pool.ntp.org");
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    delay(1500);
  }
}

// Verifie si on est en mode nuit (20h00-06h00)
bool isNightMode() {
  time_t now = time(nullptr);
  struct tm* ti = localtime(&now);
  int hour = ti->tm_hour;
  return (hour >= NIGHT_START_HOUR || hour < NIGHT_END_HOUR);
}

// Calcule l'intervalle de mise a jour selon l'heure
unsigned long getUpdateInterval() {
  time_t now = time(nullptr);
  struct tm* ti = localtime(&now);
  int hour = ti->tm_hour;

  // Heures de pointe du matin (06h00-09h00)
  if (hour >= 6 && hour < 9) {
    return INTERVAL_RUSH_HOUR;
  }
  // Heures de pointe du soir (16h00-18h00)
  else if (hour >= 16 && hour < 18) {
    return INTERVAL_RUSH_HOUR;
  }
  // Heures creuses (09h00-16h00 et 18h00-20h00)
  else {
    return INTERVAL_NORMAL;
  }
}

void fetchDepartures() {
  fetchDeparturesWithRetry(2);  // 2 tentatives max
}

void fetchDeparturesWithRetry(int maxRetries) {
  if (WiFi.status() != WL_CONNECTED) {
    strcpy(errorMsg, "WiFi deconnecte");
    dataValid = false;
    return;
  }

  client.setInsecure();

  for (int attempt = 1; attempt <= maxRetries; attempt++) {
    if (attempt > 1) {
      Serial.printf("Retry %d/%d\n", attempt, maxRetries);
      delay(1000);  // Attendre 1s avant de reessayer
    }

    if (tryFetchDepartures()) {
      return;  // Succes
    }
  }
  // Toutes les tentatives ont echoue, errorMsg est deja defini
  lastUpdate = millis();
}

bool tryFetchDepartures() {
  HTTPClient https;
  // URL avec MonitoringRef encode (: -> %3A)
  String url = "https://prim.iledefrance-mobilites.fr/marketplace/stop-monitoring?MonitoringRef=STIF%3AStopPoint%3AQ%3A";
  url += config.stopId;
  url += "%3A";

  if (https.begin(client, url)) {
    https.addHeader("Accept", "application/json");
    https.addHeader("apikey", PRIM_API_KEY);

    int httpCode = https.GET();

    if (httpCode == HTTP_CODE_OK) {
      // Lire via stream avec timeout long (chunked encoding peut etre lent)
      WiFiClient* stream = https.getStreamPtr();
      stream->setTimeout(30000);
      String payload = stream->readString();

      Serial.printf("Heap: %d, len: %d\n", ESP.getFreeHeap(), payload.length());

      if (payload.length() < 100) {
        sprintf(errorMsg, "Len=%d", payload.length());
        dataValid = false;
        https.end();
        return false;
      }

      // Trouver le debut du JSON
      int start = payload.indexOf('{');
      if (start < 0) {
        strcpy(errorMsg, "No JSON");
        dataValid = false;
        https.end();
        return false;
      }

      // Filtre pour ne garder que les champs utiles (reduit la memoire)
      StaticJsonDocument<200> filter;
      JsonObject visitFilter = filter["Siri"]["ServiceDelivery"]["StopMonitoringDelivery"][0]["MonitoredStopVisit"][0]["MonitoredVehicleJourney"];
      visitFilter["LineRef"]["value"] = true;
      visitFilter["DestinationName"][0]["value"] = true;
      visitFilter["MonitoredCall"]["ExpectedDepartureTime"] = true;
      visitFilter["MonitoredCall"]["VehicleAtStop"] = true;

      // Parser JSON avec filtre (2KB suffisent avec le filtre)
      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, payload.c_str() + start,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(15));

      // Liberer le payload
      payload = String();

      if (!error) {
        JsonArray visits = doc["Siri"]["ServiceDelivery"]["StopMonitoringDelivery"][0]["MonitoredStopVisit"];

        departureCount = 0;
        time_t now = time(nullptr);

        for (JsonObject visit : visits) {
          if (departureCount >= MAX_DEPARTURES) break;

          // Filtrer par ligne si configuree
          const char* lineRef = visit["MonitoredVehicleJourney"]["LineRef"]["value"];
          if (strlen(config.lineRef) > 0 && lineRef) {
            if (strstr(lineRef, config.lineRef) == NULL) {
              continue;  // Pas la bonne ligne, ignorer
            }
          }

          const char* expectedTime = visit["MonitoredVehicleJourney"]["MonitoredCall"]["ExpectedDepartureTime"];
          const char* destName = visit["MonitoredVehicleJourney"]["DestinationName"][0]["value"];
          bool atStop = visit["MonitoredVehicleJourney"]["MonitoredCall"]["VehicleAtStop"] | false;

          if (expectedTime) {
            // Parser l'heure ISO 8601 (ex: "2026-01-11T14:05:19.000Z")
            // L'heure est en UTC (suffixe Z)
            int year, month, day, hour, minute, second;
            sscanf(expectedTime, "%d-%d-%dT%d:%d:%d",
                   &year, &month, &day, &hour, &minute, &second);

            // Calculer epoch UTC directement (sans passer par mktime qui utilise timezone local)
            // Formule simplifiee pour 2020-2030
            int days = (year - 1970) * 365 + (year - 1969) / 4;  // Jours depuis 1970
            int monthDays[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
            days += monthDays[month - 1] + day - 1;
            if (month > 2 && (year % 4 == 0)) days++;  // Annee bissextile

            time_t departureTime = (time_t)days * 86400 + hour * 3600 + minute * 60 + second;

            int minutes = (departureTime - now) / 60;

            if (minutes >= 0) {
              departures[departureCount].minutesLeft = minutes;
              departures[departureCount].atStop = atStop;
              strncpy(departures[departureCount].destination, destName ? destName : "", 29);
              departureCount++;
            }
          }
        }

        dataValid = true;
        strcpy(errorMsg, "");

        // Mise a jour de l'heure
        struct tm* ti = localtime(&now);
        sprintf(lastUpdateTime, "%02d:%02d", ti->tm_hour, ti->tm_min);

        https.end();
        lastUpdate = millis();
        return true;  // Succes

      } else {
        sprintf(errorMsg, "JSON: %s", error.c_str());
        dataValid = false;
      }
    } else {
      sprintf(errorMsg, "HTTP %d", httpCode);
      dataValid = false;
    }

    https.end();
  } else {
    strcpy(errorMsg, "Connexion impossible");
    dataValid = false;
  }

  return false;  // Echec, peut reessayer
}

void updateDisplay() {
  // Mode nuit : ecran de veille
  if (nightMode) {
    if (!displayOff) {
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tr);
      const char* msg = "Mode veille";
      int x = (128 - u8g2.getStrWidth(msg)) / 2;
      u8g2.drawStr(x, 35, msg);
      u8g2.setFont(u8g2_font_5x7_tr);
      const char* hours = "Service 06h00-20h00";
      x = (128 - u8g2.getStrWidth(hours)) / 2;
      u8g2.drawStr(x, 50, hours);
      u8g2.sendBuffer();
      displayOff = true;
    }
    return;
  }

  displayOff = false;
  u8g2.clearBuffer();

  // === ZONE JAUNE ===
  u8g2.setFont(u8g2_font_6x10_tr);

  // Ligne et arret
  char header[40];
  sprintf(header, "%s - %s", config.lineName, config.stopName);
  int headerX = (128 - u8g2.getStrWidth(header)) / 2;
  if (headerX < 0) headerX = 0;
  u8g2.drawStr(headerX, 12, header);

  u8g2.drawHLine(0, YELLOW_ZONE_HEIGHT, 128);

  // === ZONE BLEUE ===
  if (!dataValid) {
    u8g2.setFont(u8g2_font_6x10_tr);
    if (strlen(errorMsg) > 0) {
      u8g2.drawStr(5, 35, errorMsg);
    } else {
      u8g2.drawStr(5, 35, "Chargement...");
    }
  } else if (departureCount == 0) {
    u8g2.setFont(u8g2_font_ncenB12_tr);
    u8g2.drawStr(5, 40, "Aucun bus prevu");
  } else {
    // Police plus grande pour 2 passages
    u8g2.setFont(u8g2_font_ncenB14_tr);

    int yPos = 34;
    for (int i = 0; i < departureCount && i < 2; i++) {
      char line[20];

      if (departures[i].atStop) {
        sprintf(line, "A L'ARRET");
      } else if (departures[i].minutesLeft == 0) {
        sprintf(line, "Imminent");
      } else if (departures[i].minutesLeft < 60) {
        sprintf(line, "%d min", departures[i].minutesLeft);
      } else {
        int h = departures[i].minutesLeft / 60;
        int m = departures[i].minutesLeft % 60;
        sprintf(line, "%dh%02d", h, m);
      }

      u8g2.drawStr(5, yPos, line);
      yPos += 18;
    }
  }

  // Derniere MAJ en bas a gauche
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 63, lastUpdateTime);

  // Direction en bas
  u8g2.setFont(u8g2_font_5x7_tr);
  char dirShort[20];
  strncpy(dirShort, config.direction, 18);
  dirShort[18] = '\0';
  int dirX = 128 - u8g2.getStrWidth(dirShort);
  u8g2.drawStr(dirX, 63, dirShort);

  u8g2.sendBuffer();
}

// === Pages Web ===
void handleRoot() {
  String html = R"=====(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Bus Tracker</title>
  <style>
    body {
      font-family: -apple-system, sans-serif;
      margin: 0; padding: 1rem;
      background: linear-gradient(135deg, #1e3c72, #2a5298);
      color: white; min-height: 100vh;
    }
    h1 { text-align: center; }
    .card {
      background: rgba(255,255,255,0.1);
      border-radius: 1rem;
      padding: 1.5rem;
      max-width: 500px;
      margin: 1rem auto;
    }
    .departures { margin-bottom: 1rem; }
    .departure {
      display: flex;
      justify-content: space-between;
      padding: 0.8rem;
      background: rgba(255,255,255,0.1);
      border-radius: 0.5rem;
      margin-bottom: 0.5rem;
    }
    .time { font-size: 1.5rem; font-weight: bold; }
    .imminent { color: #ff5722; }
    .config { margin-top: 2rem; }
    label { display: block; margin: 1rem 0 0.3rem; }
    input, select {
      width: 100%; padding: 0.5rem;
      border-radius: 0.3rem; border: none;
      font-size: 1rem; box-sizing: border-box;
    }
    button {
      margin-top: 1.5rem; padding: 0.8rem;
      width: 100%; font-size: 1rem;
      border: none; border-radius: 0.5rem;
      background: #4CAF50; color: white; cursor: pointer;
    }
    .info { text-align: center; margin-top: 1rem; opacity: 0.6; font-size: 0.8rem; }
  </style>
</head>
<body>
  <h1>Bus Tracker</h1>
  <div class="card">
    <h3>)=====";
  html += config.lineName;
  html += " - ";
  html += config.stopName;
  html += R"=====(</h3>
    <div class="departures" id="deps">Chargement...</div>
    <div class="info">Mise a jour: 1 min (6h-9h, 16h-18h), 2 min (autres), veille (20h-6h)</div>
  </div>

  <div class="card config">
    <h3>Configuration</h3>
    <form action="/config" method="POST">
      <label>ID Arret (ex: 413248)</label>
      <input type="text" name="stopId" value=")=====";
  html += config.stopId;
  html += R"=====(">
      <label>Nom Arret</label>
      <input type="text" name="stopName" value=")=====";
  html += config.stopName;
  html += R"=====(">
      <label>Ligne (affichage)</label>
      <input type="text" name="lineName" value=")=====";
  html += config.lineName;
  html += R"=====(">
      <label>ID Ligne (ex: C01252)</label>
      <input type="text" name="lineRef" value=")=====";
  html += config.lineRef;
  html += R"=====(">
      <label>Direction</label>
      <input type="text" name="direction" value=")=====";
  html += config.direction;
  html += R"=====(">
      <button type="submit">Enregistrer</button>
    </form>
    <div class="info">
      Trouver les IDs: <a href="https://data.iledefrance-mobilites.fr/explore/dataset/arrets-lignes/table/" target="_blank" style="color:#90caf9">data.iledefrance-mobilites.fr</a>
    </div>
  </div>

  <script>
    function refresh() {
      fetch('/api').then(r=>r.json()).then(d=>{
        let html = '';
        if (d.departures.length === 0) {
          html = '<div class="departure">Aucun bus prevu</div>';
        } else {
          d.departures.forEach(dep => {
            let timeStr = dep.atStop ? "A l'arret" :
                          dep.minutes === 0 ? "Imminent" :
                          dep.minutes < 60 ? dep.minutes + " min" :
                          Math.floor(dep.minutes/60) + "h" + String(dep.minutes%60).padStart(2,'0');
            let cls = dep.minutes <= 2 ? 'imminent' : '';
            html += '<div class="departure"><span>' + dep.destination + '</span><span class="time ' + cls + '">' + timeStr + '</span></div>';
          });
        }
        document.getElementById('deps').innerHTML = html;
      });
    }
    refresh();
    setInterval(refresh, 10000);
  </script>
</body>
</html>
)=====";

  server.send(200, "text/html", html);
}

void handleApi() {
  String json = "{\"departures\":[";

  for (int i = 0; i < departureCount; i++) {
    if (i > 0) json += ",";
    json += "{\"minutes\":";
    json += departures[i].minutesLeft;
    json += ",\"destination\":\"";
    json += departures[i].destination;
    json += "\",\"atStop\":";
    json += departures[i].atStop ? "true" : "false";
    json += "}";
  }

  json += "],\"stopName\":\"";
  json += config.stopName;
  json += "\",\"lineName\":\"";
  json += config.lineName;
  json += "\",\"lastUpdate\":\"";
  json += lastUpdateTime;
  json += "\"}";

  server.send(200, "application/json", json);
}

void handleRefresh() {
  fetchDepartures();
  server.sendHeader("Location", "/api");
  server.send(303);
}

void handleConfig() {
  if (server.hasArg("stopId")) {
    server.arg("stopId").toCharArray(config.stopId, sizeof(config.stopId));
  }
  if (server.hasArg("stopName")) {
    server.arg("stopName").toCharArray(config.stopName, sizeof(config.stopName));
  }
  if (server.hasArg("lineName")) {
    server.arg("lineName").toCharArray(config.lineName, sizeof(config.lineName));
  }
  if (server.hasArg("lineRef")) {
    server.arg("lineRef").toCharArray(config.lineRef, sizeof(config.lineRef));
  }
  if (server.hasArg("direction")) {
    server.arg("direction").toCharArray(config.direction, sizeof(config.direction));
  }

  saveConfig();

  // Rafraichir les donnees
  dataValid = false;
  lastUpdate = 0;

  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nBus Tracker - HW-364B");

  u8g2.begin();
  loadConfig();
  setupWiFi();

  server.on("/", handleRoot);
  server.on("/api", handleApi);
  server.on("/refresh", handleRefresh);
  server.on("/config", HTTP_POST, handleConfig);
  server.begin();

  Serial.print("Bus Tracker: http://");
  Serial.println(WiFi.localIP());

  // Premier fetch
  fetchDepartures();
}

void loop() {
  server.handleClient();

  // Verifier le mode nuit
  nightMode = isNightMode();

  // Mise a jour periodique (uniquement si pas en mode nuit)
  if (!nightMode) {
    unsigned long interval = getUpdateInterval();
    if (millis() - lastUpdate >= interval) {
      fetchDepartures();
    }
  }

  updateDisplay();
  delay(100);
}
