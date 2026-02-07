/*
 * Zigbee Monitor - HW-364B (ESP8266 + OLED integre)
 *
 * Affiche les donnees des capteurs Zigbee via MQTT (Zigbee2MQTT).
 * Capteurs supportes: detecteur de mouvement IKEA, capteur fuite SONOFF.
 *
 * Pins OLED sur HW-364B:
 *   SDA -> GPIO14 (D5)
 *   SCL -> GPIO12 (D6)
 *
 * Board: NodeMCU 1.0 (ESP-12E Module)
 * FQBN: esp8266:esp8266:nodemcuv2
 *
 * @dependencies U8g2, ArduinoJson, PubSubClient
 */

#include <U8g2lib.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "credentials.h"

// Configuration OLED
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(
  U8G2_R0,
  /* SCL */ 12,
  /* SDA */ 14,
  U8X8_PIN_NONE
);

#define YELLOW_ZONE_HEIGHT 16

// Configuration MQTT (depuis credentials.h)

// Topics Zigbee2MQTT
const char* TOPIC_DETECTEUR = "zigbee2mqtt/détecteur";
const char* TOPIC_WATER_LEAK = "zigbee2mqtt/water_leak";

// Donnees capteurs
struct {
  bool occupancy = false;
  bool illuminanceHigh = false;
  int battery = -1;
  bool available = false;
  unsigned long lastUpdate = 0;
} detecteur;

struct {
  bool waterLeak = false;
  bool batteryLow = false;
  int battery = -1;
  bool available = false;
  unsigned long lastUpdate = 0;
} waterSensor;

// Clients
WiFiClient espClient;
PubSubClient mqtt(espClient);

unsigned long lastDisplayUpdate = 0;
unsigned long lastReconnectAttempt = 0;

void setupWiFi() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  const char* title = "Zigbee";
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
    delay(1000);
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Parser le JSON
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }

  String topicStr = String(topic);
  Serial.printf("MQTT: %s\n", topic);

  // Detecteur de mouvement IKEA
  if (topicStr == TOPIC_DETECTEUR) {
    if (doc.containsKey("occupancy")) {
      detecteur.occupancy = doc["occupancy"].as<bool>();
    }
    if (doc.containsKey("illuminance_above_threshold")) {
      detecteur.illuminanceHigh = doc["illuminance_above_threshold"].as<bool>();
    }
    if (doc.containsKey("battery")) {
      detecteur.battery = doc["battery"].as<int>();
    }
    detecteur.available = true;
    detecteur.lastUpdate = millis();
  }
  // Availability detecteur
  else if (topicStr == String(TOPIC_DETECTEUR) + "/availability") {
    String state = doc["state"].as<String>();
    detecteur.available = (state == "online");
  }
  // Capteur fuite SONOFF
  else if (topicStr == TOPIC_WATER_LEAK) {
    if (doc.containsKey("water_leak")) {
      waterSensor.waterLeak = doc["water_leak"].as<bool>();
    }
    if (doc.containsKey("battery_low")) {
      waterSensor.batteryLow = doc["battery_low"].as<bool>();
    }
    if (doc.containsKey("battery")) {
      waterSensor.battery = doc["battery"].as<int>();
    }
    waterSensor.available = true;
    waterSensor.lastUpdate = millis();
  }
  // Availability water leak
  else if (topicStr == String(TOPIC_WATER_LEAK) + "/availability") {
    String state = doc["state"].as<String>();
    waterSensor.available = (state == "online");
  }
}

bool mqttConnect() {
  Serial.print("Connexion MQTT...");

  String clientId = "HW364B-" + String(ESP.getChipId(), HEX);

  if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
    Serial.println("OK");

    // S'abonner aux topics
    mqtt.subscribe(TOPIC_DETECTEUR);
    mqtt.subscribe((String(TOPIC_DETECTEUR) + "/availability").c_str());
    mqtt.subscribe(TOPIC_WATER_LEAK);
    mqtt.subscribe((String(TOPIC_WATER_LEAK) + "/availability").c_str());

    return true;
  } else {
    Serial.printf("Echec, rc=%d\n", mqtt.state());
    return false;
  }
}

void drawBatteryIcon(int x, int y, int percent) {
  // Contour batterie
  u8g2.drawFrame(x, y, 12, 6);
  u8g2.drawBox(x + 12, y + 1, 2, 4);

  // Remplissage selon pourcentage
  if (percent > 0) {
    int fill = map(constrain(percent, 0, 100), 0, 100, 0, 10);
    u8g2.drawBox(x + 1, y + 1, fill, 4);
  }
}

void drawSensorStatus(int y, const char* name, const char* status, int battery, bool warning) {
  u8g2.setFont(u8g2_font_6x10_tr);

  // Nom du capteur
  u8g2.drawStr(2, y, name);

  // Statut
  u8g2.setFont(u8g2_font_5x8_tr);
  if (warning) {
    // Encadrer si alerte
    int w = u8g2.getStrWidth(status);
    u8g2.drawFrame(44, y - 8, w + 4, 10);
  }
  u8g2.drawStr(46, y, status);

  // Batterie
  if (battery >= 0) {
    drawBatteryIcon(100, y - 6, battery);
  }
}

void updateDisplay() {
  u8g2.clearBuffer();

  // === ZONE JAUNE (titre) ===
  u8g2.setFont(u8g2_font_ncenB10_tr);
  const char* title = "Zigbee";
  int titleX = (128 - u8g2.getStrWidth(title)) / 2;
  u8g2.drawStr(titleX, 13, title);

  // Indicateur MQTT
  u8g2.setFont(u8g2_font_5x7_tr);
  if (mqtt.connected()) {
    u8g2.drawStr(110, 7, "MQTT");
  } else {
    u8g2.drawStr(118, 7, "X");
  }

  u8g2.drawHLine(0, YELLOW_ZONE_HEIGHT, 128);

  // === ZONE BLEUE (capteurs) ===

  // Detecteur de mouvement
  if (detecteur.available) {
    const char* status;
    bool warning = false;

    if (detecteur.occupancy) {
      status = "MOUVEMENT";
      warning = true;
    } else {
      status = detecteur.illuminanceHigh ? "Clair" : "Sombre";
    }
    drawSensorStatus(30, "Motion", status, detecteur.battery, warning);
  } else {
    drawSensorStatus(30, "Motion", "offline", -1, false);
  }

  // Capteur fuite d'eau
  if (waterSensor.available) {
    const char* status;
    bool warning = false;

    if (waterSensor.waterLeak) {
      status = "FUITE!";
      warning = true;
    } else if (waterSensor.batteryLow) {
      status = "Batt faible";
      warning = true;
    } else {
      status = "OK";
    }
    drawSensorStatus(45, "Water", status, waterSensor.battery, warning);
  } else {
    drawSensorStatus(45, "Water", "offline", -1, false);
  }

  // Ligne de separation
  u8g2.drawHLine(0, 50, 128);

  // Informations en bas
  u8g2.setFont(u8g2_font_5x7_tr);

  // Uptime
  unsigned long uptime = millis() / 1000;
  int hours = uptime / 3600;
  int mins = (uptime % 3600) / 60;
  char uptimeStr[12];
  sprintf(uptimeStr, "Up: %dh%02d", hours, mins);
  u8g2.drawStr(2, 62, uptimeStr);

  // Signal WiFi
  int rssi = WiFi.RSSI();
  int bars = (rssi > -50) ? 4 : (rssi > -60) ? 3 : (rssi > -70) ? 2 : (rssi > -80) ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    int h = 2 + i * 2;
    int bx = 115 + i * 3;
    if (i < bars) u8g2.drawBox(bx, 62 - h, 2, h);
    else u8g2.drawFrame(bx, 62 - h, 2, h);
  }

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nZigbee Monitor - HW-364B");

  u8g2.begin();
  setupWiFi();

  // Configuration MQTT
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);

  mqttConnect();
  updateDisplay();
}

void loop() {
  // Maintenir connexion MQTT
  if (!mqtt.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      if (mqttConnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    mqtt.loop();
  }

  // Mise a jour affichage toutes les secondes
  if (millis() - lastDisplayUpdate >= 1000) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
}
