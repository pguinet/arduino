#include <string.h>
#include <unity.h>
#include "outdoor_logic.h"

using namespace outdoor;

void setUp() {}
void tearDown() {}

static bool parseT(const char *json, float &t) { return parseTemperature(json, strlen(json), t); }
static bool parseA(const char *json, bool &on) { return parseAvailability(json, strlen(json), on); }

void test_real_z2m_payload() {
    // Message reel (bloc "update" raccourci mais de taille comparable).
    const char *json =
        "{\"battery\":100,\"linkquality\":96,\"temperature\":8.2,"
        "\"temperature_calibration\":0,\"temperature_units\":\"celsius\","
        "\"update\":{\"installed_version\":4097,\"latest_version\":4098,"
        "\"state\":\"available\",\"release_notes\":\"Lorem ipsum dolor sit amet, "
        "consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore "
        "et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation "
        "ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure "
        "dolor in reprehenderit in voluptate velit esse cillum dolore.\"}}";
    float t = 0;
    TEST_ASSERT_TRUE(parseT(json, t));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 8.2f, t);
}

void test_negative_and_integer_values() {
    float t = 0;
    TEST_ASSERT_TRUE(parseT("{\"temperature\":-3.5}", t));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -3.5f, t);
    TEST_ASSERT_TRUE(parseT("{\"temperature\":12}", t));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f, t);
}

void test_missing_or_invalid_field_rejected() {
    float t = 42;
    TEST_ASSERT_FALSE(parseT("{\"battery\":100}", t));
    TEST_ASSERT_FALSE(parseT("{\"temperature\":null}", t));
    TEST_ASSERT_FALSE(parseT("{\"temperature\":\"8.2\"}", t));
    TEST_ASSERT_FALSE(parseT("{\"temperature\":", t));
    TEST_ASSERT_FALSE(parseT("", t));
    TEST_ASSERT_EQUAL_FLOAT(42.0f, t);  // inchange en cas d'echec
}

void test_implausible_values_rejected() {
    float t = 0;
    TEST_ASSERT_FALSE(parseT("{\"temperature\":-60.1}", t));
    TEST_ASSERT_FALSE(parseT("{\"temperature\":80.1}", t));
    TEST_ASSERT_TRUE(parseT("{\"temperature\":-60}", t));
    TEST_ASSERT_TRUE(parseT("{\"temperature\":80}", t));
}

void test_payload_without_terminator() {
    // PubSubClient fournit un tampon non termine par '\0'.
    const char buf[] = "{\"temperature\":7.6}XXXX";
    float t = 0;
    TEST_ASSERT_TRUE(parseTemperature(buf, 19, t));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.6f, t);
}

void test_availability_json() {
    bool on = false;
    TEST_ASSERT_TRUE(parseA("{\"state\":\"online\"}", on));
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_TRUE(parseA("{\"state\":\"offline\"}", on));
    TEST_ASSERT_FALSE(on);
}

void test_availability_legacy_text() {
    bool on = false;
    TEST_ASSERT_TRUE(parseA("online", on));
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_TRUE(parseA("offline", on));
    TEST_ASSERT_FALSE(on);
}

void test_availability_garbage_rejected() {
    bool on = true;
    TEST_ASSERT_FALSE(parseA("{\"state\":\"maybe\"}", on));
    TEST_ASSERT_FALSE(parseA("{}", on));
    TEST_ASSERT_FALSE(parseA("onlineX", on));
    TEST_ASSERT_FALSE(parseA("", on));
    TEST_ASSERT_TRUE(on);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_real_z2m_payload);
    RUN_TEST(test_negative_and_integer_values);
    RUN_TEST(test_missing_or_invalid_field_rejected);
    RUN_TEST(test_implausible_values_rejected);
    RUN_TEST(test_payload_without_terminator);
    RUN_TEST(test_availability_json);
    RUN_TEST(test_availability_legacy_text);
    RUN_TEST(test_availability_garbage_rejected);
    return UNITY_END();
}
