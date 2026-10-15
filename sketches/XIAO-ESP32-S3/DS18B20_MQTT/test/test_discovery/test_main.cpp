#include <unity.h>
#include <string.h>
#include <ArduinoJson.h>
#include "ha_discovery.h"

using namespace temp;

static const uint8_t kMac[6] = {0xdc, 0xb4, 0xd9, 0x3b, 0xb0, 0x04};

void setUp() {}
void tearDown() {}

void test_device_id_from_mac() {
    char id[32];
    makeDeviceId(kMac, id, sizeof id);
    TEST_ASSERT_EQUAL_STRING("xiao_ds18b20_3bb004", id);
}

void test_topics() {
    Topics t;
    makeTopics("xiao_ds18b20_3bb004", t);
    TEST_ASSERT_EQUAL_STRING("xiao_ds18b20/xiao_ds18b20_3bb004/temperature", t.state);
    TEST_ASSERT_EQUAL_STRING("xiao_ds18b20/xiao_ds18b20_3bb004/status", t.status);
    TEST_ASSERT_EQUAL_STRING("xiao_ds18b20/xiao_ds18b20_3bb004/probe", t.probe);
    TEST_ASSERT_EQUAL_STRING(
        "homeassistant/sensor/xiao_ds18b20_3bb004/temperature/config", t.config);
}

void test_discovery_payload() {
    Topics t;
    makeTopics("xiao_ds18b20_3bb004", t);
    char buf[768];
    const size_t n = buildDiscovery("xiao_ds18b20_3bb004", t, buf, sizeof buf);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL(strlen(buf), n);

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf));
    TEST_ASSERT_EQUAL_STRING("Temperature", doc["name"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("xiao_ds18b20_3bb004_temperature", doc["unique_id"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING(t.state, doc["state_topic"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("temperature", doc["device_class"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("measurement", doc["state_class"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("°C", doc["unit_of_measurement"].as<const char *>());
    TEST_ASSERT_EQUAL_INT(1, doc["suggested_display_precision"].as<int>());
    TEST_ASSERT_EQUAL_STRING("all", doc["availability_mode"].as<const char *>());
    TEST_ASSERT_EQUAL(2, doc["availability"].size());
    TEST_ASSERT_EQUAL_STRING(t.status, doc["availability"][0]["topic"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING(t.probe, doc["availability"][1]["topic"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("xiao_ds18b20_3bb004", doc["device"]["identifiers"][0].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("Seeed Studio", doc["device"]["manufacturer"].as<const char *>());
}

void test_discovery_too_small_buffer_returns_zero() {
    Topics t;
    makeTopics("xiao_ds18b20_3bb004", t);
    char buf[64];
    TEST_ASSERT_EQUAL(0, buildDiscovery("xiao_ds18b20_3bb004", t, buf, sizeof buf));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_device_id_from_mac);
    RUN_TEST(test_topics);
    RUN_TEST(test_discovery_payload);
    RUN_TEST(test_discovery_too_small_buffer_returns_zero);
    return UNITY_END();
}
