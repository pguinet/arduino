#include <unity.h>
#include <string.h>
#include "net_status.h"

using namespace net;

void setUp() {}
void tearDown() {}

static char out[256];

// Ligne n (0, 1, 2) du texte formate.
static const char *line(int n) {
    static char buf[128];
    const char *p = out;
    for (int i = 0; i < n; i++) {
        p = strchr(p, '\n');
        if (!p) return "";
        p++;
    }
    size_t len = strcspn(p, "\n");
    if (len >= sizeof buf) len = sizeof buf - 1;
    memcpy(buf, p, len);
    buf[len] = 0;
    return buf;
}

static Status connected() {
    Status s;
    s.wifi = WifiPhase::Connected;
    strcpy(s.ssid, "Deskeo-Guest");
    strcpy(s.ip, "10.0.3.42");
    s.rssi = -67;
    return s;
}

void test_three_lines() {
    Status s;
    format(s, out, sizeof out);
    TEST_ASSERT_NOT_NULL(strchr(out, '\n'));
    TEST_ASSERT_EQUAL_STRING("MQTT : attente WiFi", line(2));
}

void test_scanning() {
    Status s;
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("WiFi : scan en cours", line(0));
    TEST_ASSERT_EQUAL_STRING("Heure : attente WiFi", line(1));
}

void test_no_known_network() {
    Status s;
    s.wifi = WifiPhase::NoKnownNetwork;
    s.visibleCount = 23;
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("WiFi : aucun reseau connu (23 visibles)", line(0));
}

void test_connecting_without_failure() {
    Status s;
    s.wifi = WifiPhase::Connecting;
    strcpy(s.ssid, "Deskeo-Guest");
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("WiFi : connexion a Deskeo-Guest...", line(0));
}

void test_connecting_after_failure_shows_reason() {
    Status s;
    s.wifi = WifiPhase::Connecting;
    strcpy(s.ssid, "Deskeo-Guest");
    s.disconnectReason = 202;
    strcpy(s.disconnectReasonName, "AUTH_FAIL");
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("WiFi : connexion a Deskeo-Guest, echec AUTH_FAIL (202)", line(0));
}

void test_connected_shows_ip_and_rssi() {
    Status s = connected();
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("WiFi : Deskeo-Guest  10.0.3.42  -67 dBm", line(0));
}

void test_clock_wait_in_seconds_then_minutes() {
    Status s = connected();
    s.clockWaitS = 45;
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("Heure : attente NTP (45 s)", line(1));
    s.clockWaitS = 150;
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("Heure : attente NTP (2 min)", line(1));
    s.httpsTimeFails = 3;
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("Heure : attente NTP (2 min), HTTPS echec x3", line(1));
}

void test_clock_ok() {
    Status s = connected();
    s.clockOk = true;
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("Heure : OK", line(1));
    s.clockSource = ClockSource::Ntp;
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("Heure : OK (NTP)", line(1));
    s.clockSource = ClockSource::Https;
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("Heure : OK (HTTPS, NTP filtre)", line(1));
}

void test_mqtt_waits_for_clock() {
    Status s = connected();
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("MQTT : attente heure", line(2));
}

void test_mqtt_phases() {
    Status s = connected();
    s.clockOk = true;
    s.mqtt = MqttPhase::BadCredentials;
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("MQTT : certificats invalides", line(2));

    s.mqtt = MqttPhase::DnsFailed;
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("MQTT : echec DNS", line(2));

    // Pas encore tente (la tentative est bloquante : jamais affichee en cours)
    s.mqtt = MqttPhase::Waiting;
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("MQTT : connexion...", line(2));

    s.mqtt = MqttPhase::Connected;
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("MQTT : connecte, attente des donnees", line(2));
}

void test_mqtt_connect_failure_codes() {
    Status s = connected();
    s.clockOk = true;
    s.mqtt = MqttPhase::ConnectFailed;
    s.mqttRc = -2;
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("MQTT : echec rc=-2", line(2));
    s.tlsError = -9984;
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("MQTT : echec rc=-2, TLS -9984", line(2));
}

void test_bad_credentials_shown_even_without_wifi() {
    // Certificats faux : rien a attendre du reseau, le dire tout de suite
    Status s;
    s.mqtt = MqttPhase::BadCredentials;
    format(s, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("MQTT : certificats invalides", line(2));
}

void test_truncates_to_buffer() {
    Status s = connected();
    char small[10];
    memset(small, 'x', sizeof small);
    format(s, small, sizeof small);
    TEST_ASSERT_EQUAL_INT(9, (int)strlen(small));
    format(s, small, 0);  // ne doit rien ecrire
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_three_lines);
    RUN_TEST(test_scanning);
    RUN_TEST(test_no_known_network);
    RUN_TEST(test_connecting_without_failure);
    RUN_TEST(test_connecting_after_failure_shows_reason);
    RUN_TEST(test_connected_shows_ip_and_rssi);
    RUN_TEST(test_clock_wait_in_seconds_then_minutes);
    RUN_TEST(test_clock_ok);
    RUN_TEST(test_mqtt_waits_for_clock);
    RUN_TEST(test_mqtt_phases);
    RUN_TEST(test_mqtt_connect_failure_codes);
    RUN_TEST(test_bad_credentials_shown_even_without_wifi);
    RUN_TEST(test_truncates_to_buffer);
    return UNITY_END();
}
