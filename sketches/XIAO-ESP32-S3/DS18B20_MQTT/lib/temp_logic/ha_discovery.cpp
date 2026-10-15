#include "ha_discovery.h"

#include <ArduinoJson.h>
#include <stdio.h>

namespace temp {

void makeDeviceId(const uint8_t mac[6], char *out, size_t size) {
    snprintf(out, size, "xiao_ds18b20_%02x%02x%02x", mac[3], mac[4], mac[5]);
}

void makeTopics(const char *deviceId, Topics &t) {
    snprintf(t.state, sizeof t.state, "xiao_ds18b20/%s/temperature", deviceId);
    snprintf(t.status, sizeof t.status, "xiao_ds18b20/%s/status", deviceId);
    snprintf(t.probe, sizeof t.probe, "xiao_ds18b20/%s/probe", deviceId);
    snprintf(t.config, sizeof t.config,
             "homeassistant/sensor/%s/temperature/config", deviceId);
}

size_t buildDiscovery(const char *deviceId, const Topics &t, char *out, size_t size) {
    char uniqueId[48];
    snprintf(uniqueId, sizeof uniqueId, "%s_temperature", deviceId);

    JsonDocument doc;
    doc["name"] = "Temperature";
    doc["unique_id"] = uniqueId;
    doc["state_topic"] = t.state;
    doc["device_class"] = "temperature";
    doc["state_class"] = "measurement";
    doc["unit_of_measurement"] = "°C";
    doc["suggested_display_precision"] = 1;
    doc["availability_mode"] = "all";
    JsonArray avail = doc["availability"].to<JsonArray>();
    avail.add<JsonObject>()["topic"] = t.status;
    avail.add<JsonObject>()["topic"] = t.probe;
    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"].add(deviceId);
    dev["name"] = deviceId;
    dev["model"] = "XIAO ESP32S3 + DS18B20";
    dev["manufacturer"] = "Seeed Studio";

    if (measureJson(doc) >= size) return 0;
    return serializeJson(doc, out, size);
}

}  // namespace temp
