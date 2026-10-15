/*
 * DS18B20_MQTT - XIAO ESP32-S3
 *
 * Temperature d'une sonde DS18B20 publiee dans Home Assistant (MQTT Discovery).
 *
 * Board: Seeed Studio XIAO ESP32S3
 * FQBN: PlatformIO seeed_xiao_esp32s3
 *
 * @dependencies OneWire, DallasTemperature, PubSubClient, ArduinoJson
 */

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <esp_mac.h>

#include "credentials.h"
#include "ha_discovery.h"
#include "temp_logic.h"

#ifndef WIFI_SSID
#error "credentials.h : WIFI_SSID manquant (voir sketches/common/credentials.h.example)"
#endif
#ifndef MQTT_SERVER
#error "credentials.h : MQTT_SERVER manquant (voir sketches/common/credentials.h.example)"
#endif

static const uint8_t ONE_WIRE_PIN = D0;  // GPIO1
static const uint32_t READ_INTERVAL_MS = 10000;
static const uint32_t RETRY_INTERVAL_MS = 5000;
static const uint32_t WIFI_KICK_INTERVAL_MS = 30000;
static const uint32_t TCP_CONNECT_TIMEOUT_MS = 3000;
static const uint16_t MQTT_SOCKET_TIMEOUT_S = 5;
static const char HA_STATUS_TOPIC[] = "homeassistant/status";

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);
WiFiClient netClient;
PubSubClient mqtt(netClient);

char deviceId[32];
temp::Topics topics;
temp::PublishPolicy policy;
temp::ProbeHealth probe;

bool converting = false;
uint32_t conversionStart = 0;
uint32_t lastRead = 0;
bool hasPrevious = false;
float previous = 0.0f;
bool discoveryRequested = false;

static void publishProbeStatus() {
    mqtt.publish(topics.probe, probe.online() ? "online" : "offline", true);
}

static void publishDiscovery() {
    char payload[768];
    const size_t n = temp::buildDiscovery(deviceId, topics, payload, sizeof payload);
    if (n == 0) {
        Serial.println("Decouverte : tampon trop petit");
        return;
    }
    const bool ok = mqtt.publish(topics.config, reinterpret_cast<const uint8_t *>(payload), n, true);
    Serial.printf("Decouverte publiee (%u octets) : %s\n", (unsigned)n, ok ? "OK" : "ECHEC");
}

// Signature imposee par PubSubClient (pas de const possible).
// cppcheck-suppress constParameterCallback
static void onMqttMessage(char *topic, byte *payload, unsigned int length) {
    if (strcmp(topic, HA_STATUS_TOPIC) == 0 && length == 6 &&
        memcmp(payload, "online", 6) == 0) {
        discoveryRequested = true;  // Home Assistant a redemarre
    }
}

// La reconnexion est assuree par le core (auto-reconnect, actif par defaut).
// Il abandonne pourtant sur certaines raisons (AUTH_FAIL, renvoye par exemple
// par une box en cours de redemarrage) : relance douce toutes les 30 s.
static void handleWifi() {
    static uint32_t lastKick = 0;
    static bool wasConnected = false;
    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected != wasConnected) {
        wasConnected = connected;
        if (connected) {
            Serial.printf("WiFi connecte a %s, IP %s\n", WiFi.SSID().c_str(),
                          WiFi.localIP().toString().c_str());
        } else {
            Serial.println("WiFi perdu");
            lastKick = millis();  // laisser d'abord le core se reconnecter
        }
    }
    if (connected) return;
    if (millis() - lastKick < WIFI_KICK_INTERVAL_MS) return;
    lastKick = millis();
    Serial.printf("WiFi toujours absent (statut %d) : relance\n", (int)WiFi.status());
    WiFi.reconnect();  // non bloquant
}

static void handleMqtt() {
    static uint32_t lastAttempt = 0 - RETRY_INTERVAL_MS;  // 1re tentative immediate
    if (WiFi.status() != WL_CONNECTED) return;
    if (mqtt.connected()) {
        mqtt.loop();
        if (discoveryRequested) {
            discoveryRequested = false;
            publishDiscovery();
            policy.reset();  // republier la valeur a la prochaine lecture
        }
        return;
    }
    if (millis() - lastAttempt < RETRY_INTERVAL_MS) return;
    const bool ok = mqtt.connect(deviceId, MQTT_USER, MQTT_PASS, topics.status, 0, true, "offline");
    lastAttempt = millis();  // delai compte depuis la fin de la tentative
    if (!ok) {
        Serial.printf("MQTT : echec de connexion (etat %d)\n", mqtt.state());
        return;
    }
    Serial.println("MQTT connecte");
    mqtt.publish(topics.status, "online", true);
    publishProbeStatus();
    publishDiscovery();
    mqtt.subscribe(HA_STATUS_TOPIC);
    policy.reset();  // republier la valeur courante a la prochaine lecture
}

static void publishTemperature(int tenths) {
    char value[8];
    temp::formatTenths(tenths, value, sizeof value);
    if (mqtt.connected() && policy.shouldPublish(tenths, millis())) {
        if (mqtt.publish(topics.state, value)) {
            policy.markPublished(tenths, millis());
            Serial.printf("Publie : %s C\n", value);
        }
    }
}

static void handleSensor() {
    const uint32_t now = millis();
    if (!converting) {
        if (now - lastRead < READ_INTERVAL_MS && lastRead != 0) return;
        sensors.requestTemperatures();
        converting = true;
        conversionStart = now;
        return;
    }
    if (now - conversionStart < sensors.millisToWaitForConversion()) return;
    converting = false;
    lastRead = now;

    const float c = sensors.getTempCByIndex(0);
    const bool valid = temp::isValidReading(c, hasPrevious, previous);
    Serial.printf("Lecture : %.4f C (%s)\n", c, valid ? "valide" : "rejetee");
    if (probe.record(valid)) {
        Serial.printf("Sonde %s\n", probe.online() ? "en ligne" : "hors ligne");
        if (!probe.online()) hasPrevious = false;  // au retour, 85 sera rejete
        if (mqtt.connected()) publishProbeStatus();
    }
    if (!valid) return;
    hasPrevious = true;
    previous = c;
    publishTemperature(temp::toTenths(c));
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("DS18B20_MQTT - XIAO ESP32-S3");

    // Pas de resistance de tirage externe : pull-up interne (~45 kohm).
    pinMode(ONE_WIRE_PIN, INPUT_PULLUP);
    sensors.begin();
    sensors.setResolution(12);
    sensors.setWaitForConversion(false);
    Serial.printf("Sondes detectees : %u\n", sensors.getDeviceCount());

    WiFi.mode(WIFI_STA);
    // MAC lue dans l'eFuse : WiFi.macAddress() renvoie des zeros tant que la
    // station n'est pas demarree.
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    temp::makeDeviceId(mac, deviceId, sizeof deviceId);
    temp::makeTopics(deviceId, topics);
    Serial.printf("Identifiant : %s\n", deviceId);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  // non bloquant

    netClient.setConnectionTimeout(TCP_CONNECT_TIMEOUT_MS);
    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    mqtt.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);
    mqtt.setBufferSize(1024);  // decouverte > 256 octets par defaut
    mqtt.setCallback(onMqttMessage);
}

void loop() {
    handleWifi();
    handleMqtt();
    handleSensor();
    delay(10);
}
