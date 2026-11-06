/*
 * Outdoor_Temp - HW-364B
 *
 * Afficheur de la temperature exterieure : la sonde Zigbee du jardin
 * (Sonoff SNZB-02LD) est lue sur MQTT via Zigbee2MQTT, et affichee en gros
 * sur l'OLED, avec l'heure NTP et l'age de la mesure.
 *
 * Pins OLED sur HW-364B (I2C logiciel) :
 *   SDA -> GPIO14 (D5)
 *   SCL -> GPIO12 (D6)
 *
 * Board: NodeMCU 1.0 (ESP-12E Module)
 * FQBN: esp8266:esp8266:nodemcuv2 (projet PlatformIO, env hw364b)
 *
 * @dependencies U8g2, PubSubClient, ArduinoJson
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <U8g2lib.h>
#include <time.h>

#include "credentials.h"
#include "outdoor_logic.h"

// Compte MQTT dedie si credentials.h le definit, sinon le compte commun.
#ifdef AFFICHEUR_MQTT_SERVER
#define OT_MQTT_SERVER AFFICHEUR_MQTT_SERVER
#else
#define OT_MQTT_SERVER MQTT_SERVER
#endif
#ifdef AFFICHEUR_MQTT_USER
#define OT_MQTT_USER AFFICHEUR_MQTT_USER
#define OT_MQTT_PASS AFFICHEUR_MQTT_PASS
#else
#define OT_MQTT_USER MQTT_USER
#define OT_MQTT_PASS MQTT_PASS
#endif

namespace {

const char *TOPIC_TEMP = "zigbee2mqtt/temperature_exterieure";
const char *TOPIC_AVAILABILITY = "zigbee2mqtt/temperature_exterieure/availability";
const char *TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";  // Europe/Paris

const int SCREEN_W = 128;
const int YELLOW_H = 16;                            // bande jaune de l'ecran
const uint32_t DISPLAY_PERIOD_MS = 1000;
const uint32_t MQTT_RETRY_MS = 5000;
const uint32_t PIXEL_SHIFT_PERIOD_MS = 10UL * 60UL * 1000UL;  // anti-marquage OLED
const uint32_t STALE_MS = 60UL * 60UL * 1000UL;   // perimee apres 1 h sans message

U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* SCL */ 12, /* SDA */ 14, U8X8_PIN_NONE);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
outdoor::SensorState sensor(STALE_MS);

uint32_t lastDisplayMs = 0;
uint32_t lastMqttAttemptMs = 0;
bool lastMqttAttemptDone = false;

// Signature imposee par PubSubClient (MQTT_CALLBACK_SIGNATURE) : pas de const.
// cppcheck-suppress [constParameterCallback, constParameterPointer, constParameter]
void onMqttMessage(char *topic, byte *payload, unsigned int length) {
    const char *data = reinterpret_cast<const char *>(payload);
    if (strcmp(topic, TOPIC_TEMP) == 0) {
        float celsius;
        if (outdoor::parseTemperature(data, length, celsius)) {
            sensor.onTemperature(outdoor::toTenths(celsius), millis());
            Serial.printf("Temperature : %.1f C\n", celsius);
        } else {
            Serial.printf("Message sonde ignore (%u octets)\n", length);
        }
    } else if (strcmp(topic, TOPIC_AVAILABILITY) == 0) {
        bool online;
        if (outdoor::parseAvailability(data, length, online)) {
            sensor.onAvailability(online);
            Serial.printf("Sonde %s\n", online ? "en ligne" : "hors ligne");
        }
    }
}

void connectMqtt() {
    char clientId[24];
    snprintf(clientId, sizeof clientId, "afficheur-%06x", ESP.getChipId());
    Serial.printf("MQTT %s... ", OT_MQTT_SERVER);
    if (!mqtt.connect(clientId, OT_MQTT_USER, OT_MQTT_PASS)) {
        Serial.printf("echec (rc=%d)\n", mqtt.state());
        return;
    }
    Serial.println("OK");
    // Avant l'abonnement : le message retenu arrive juste apres.
    sensor.onSubscribed(millis());
    mqtt.subscribe(TOPIC_TEMP);
    mqtt.subscribe(TOPIC_AVAILABILITY);
}

void maintainMqtt() {
    if (mqtt.connected()) {
        mqtt.loop();
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    const uint32_t now = millis();
    if (!lastMqttAttemptDone || now - lastMqttAttemptMs >= MQTT_RETRY_MS) {
        lastMqttAttemptDone = true;
        lastMqttAttemptMs = now;
        connectMqtt();
    }
}

void drawWifiBars(int x, int baseline) {
    const int rssi = WiFi.RSSI();
    const int bars = (rssi > -55) ? 4 : (rssi > -65) ? 3 : (rssi > -75) ? 2 : 1;
    for (int i = 0; i < 4; i++) {
        const int h = 2 + i * 2;
        if (i < bars) {
            u8g2.drawBox(x + i * 3, baseline - h, 2, h);
        } else {
            u8g2.drawFrame(x + i * 3, baseline - h, 2, h);
        }
    }
}

void drawHeader() {
    u8g2.setFont(u8g2_font_7x13B_tr);
    u8g2.drawStr(0, 12, "Exterieur");

    char clock[6] = "--:--";
    const time_t now = time(nullptr);
    if (now > 1700000000) {  // heure NTP recue
        struct tm local;
        localtime_r(&now, &local);
        strftime(clock, sizeof clock, "%H:%M", &local);
    }
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(78, 11, clock);

    if (WiFi.status() == WL_CONNECTED) {
        drawWifiBars(116, 11);
    } else {
        u8g2.drawStr(116, 11, "x");
    }
}

// Temperature en gros, centree, decalee de quelques pixels selon shift.
void drawTemperature(int shift) {
    char value[8] = "--";
    if (sensor.hasValue()) {
        outdoor::formatTenths(sensor.tenths(), value, sizeof value);
    }
    u8g2.setFont(u8g2_font_logisoso28_tn);
    const int valueW = u8g2.getStrWidth(value);
    u8g2.setFont(u8g2_font_helvB10_tf);
    const int unitW = u8g2.getStrWidth("C") + 7;  // degre + C
    const int x = (SCREEN_W - valueW - unitW - 2) / 2 + shift;

    u8g2.setFont(u8g2_font_logisoso28_tn);
    u8g2.drawStr(x, 49, value);
    u8g2.setFont(u8g2_font_helvB10_tf);
    u8g2.drawGlyph(x + valueW + 2, 32, 0xB0);  // signe degre (Latin-1)
    u8g2.drawStr(x + valueW + 9, 32, "C");
}

void drawFooter(uint32_t now) {
    char text[28];
    bool alert = false;
    if (WiFi.status() != WL_CONNECTED) {
        snprintf(text, sizeof text, "WiFi : connexion...");
    } else if (!mqtt.connected()) {
        snprintf(text, sizeof text, "MQTT : connexion...");
    } else {
        switch (sensor.status(now)) {
            case outdoor::Status::NoData:
                snprintf(text, sizeof text, "En attente de la sonde");
                break;
            case outdoor::Status::Fresh:
                outdoor::formatAge(sensor.ageMs(now), text, sizeof text);
                break;
            case outdoor::Status::AgeUnknown:
                snprintf(text, sizeof text, "Derniere valeur connue");
                break;
            case outdoor::Status::Stale: {
                char age[20];
                outdoor::formatAge(sensor.ageMs(now), age, sizeof age);
                snprintf(text, sizeof text, "Perimee (%s)", age);
                alert = true;
                break;
            }
            case outdoor::Status::Offline:
                snprintf(text, sizeof text, "Sonde hors ligne");
                alert = true;
                break;
        }
    }

    u8g2.setFont(u8g2_font_5x7_tr);
    const int w = u8g2.getStrWidth(text);
    const int x = (SCREEN_W - w) / 2;
    if (alert) {  // texte en video inverse pour attirer l'oeil
        u8g2.drawBox(x - 2, 55, w + 4, 9);
        u8g2.setDrawColor(0);
        u8g2.drawStr(x, 63, text);
        u8g2.setDrawColor(1);
    } else {
        u8g2.drawStr(x, 63, text);
    }
}

void updateDisplay() {
    const uint32_t now = millis();
    // Decalage horizontal lent (0, 1, 2, 1, 0, -1, -2, -1) contre le marquage.
    static const int SHIFTS[] = {0, 1, 2, 1, 0, -1, -2, -1};
    const int shift = SHIFTS[(now / PIXEL_SHIFT_PERIOD_MS) % 8];

    u8g2.clearBuffer();
    drawHeader();
    u8g2.drawHLine(0, YELLOW_H - 1, SCREEN_W);
    drawTemperature(shift);
    drawFooter(now);
    u8g2.sendBuffer();
}

}  // namespace

void setup() {
    Serial.begin(115200);
    Serial.println("\n\nOutdoor_Temp - HW-364B");

    u8g2.begin();
    updateDisplay();

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  // non bloquant : l'affichage tourne
    configTime(TIMEZONE, "pool.ntp.org", "time.google.com");

    mqtt.setServer(OT_MQTT_SERVER, MQTT_PORT);
    // Le message Zigbee2MQTT fait ~450 octets (bloc "update") : au-dela des
    // 256 octets par defaut, PubSubClient le jetterait sans rien dire.
    mqtt.setBufferSize(1024);
    mqtt.setCallback(onMqttMessage);
}

void loop() {
    maintainMqtt();
    const uint32_t now = millis();
    if (now - lastDisplayMs >= DISPLAY_PERIOD_MS) {
        lastDisplayMs = now;
        updateDisplay();
    }
}
