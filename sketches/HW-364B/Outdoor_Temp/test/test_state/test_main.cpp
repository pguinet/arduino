#include <unity.h>
#include "outdoor_logic.h"

using namespace outdoor;

void setUp() {}
void tearDown() {}

static const uint32_t MIN = 60000UL;
static const uint32_t STALE = 60 * MIN;
static const uint32_t WINDOW = 3000UL;

void test_no_data_at_start() {
    SensorState s(STALE, WINDOW);
    TEST_ASSERT_TRUE(s.status(0) == Status::NoData);
    TEST_ASSERT_FALSE(s.hasValue());
    s.onAvailability(false);
    TEST_ASSERT_TRUE(s.status(0) == Status::NoData);  // rien a afficher de toute facon
}

void test_retained_message_has_unknown_age() {
    SensorState s(STALE, WINDOW);
    s.onSubscribed(1000);
    s.onTemperature(76, 1500);
    TEST_ASSERT_TRUE(s.status(1500) == Status::AgeUnknown);
    TEST_ASSERT_FALSE(s.ageKnown());
    TEST_ASSERT_EQUAL_INT(76, s.tenths());
}

void test_live_message_after_window_is_fresh() {
    SensorState s(STALE, WINDOW);
    s.onSubscribed(1000);
    s.onTemperature(76, 1500);
    s.onTemperature(78, 1000 + WINDOW + 1);
    TEST_ASSERT_TRUE(s.status(5000) == Status::Fresh);
    TEST_ASSERT_TRUE(s.ageKnown());
    TEST_ASSERT_EQUAL_INT(78, s.tenths());
    TEST_ASSERT_EQUAL_UINT32(5000 - (1000 + WINDOW + 1), s.ageMs(5000));
}

void test_message_without_subscription_is_fresh() {
    SensorState s(STALE, WINDOW);
    s.onTemperature(10, 100);
    TEST_ASSERT_TRUE(s.status(100) == Status::Fresh);
}

void test_fresh_becomes_stale() {
    SensorState s(STALE, WINDOW);
    s.onSubscribed(0);
    s.onTemperature(50, 10 * MIN);
    TEST_ASSERT_TRUE(s.status(10 * MIN + STALE) == Status::Fresh);
    TEST_ASSERT_TRUE(s.status(10 * MIN + STALE + 1) == Status::Stale);
}

void test_unknown_age_also_becomes_stale() {
    SensorState s(STALE, WINDOW);
    s.onSubscribed(0);
    s.onTemperature(50, 100);
    TEST_ASSERT_TRUE(s.status(100 + STALE + 1) == Status::Stale);
}

void test_resubscription_redelivers_same_value_keeps_age() {
    // Reconnexion MQTT : le broker renvoie la meme valeur retenue.
    SensorState s(STALE, WINDOW);
    s.onSubscribed(0);
    s.onTemperature(50, 10 * MIN);  // message en direct
    s.onSubscribed(20 * MIN);
    s.onTemperature(50, 20 * MIN + 200);
    TEST_ASSERT_TRUE(s.status(20 * MIN + 200) == Status::Fresh);
    TEST_ASSERT_EQUAL_UINT32(10 * MIN + 200, s.ageMs(20 * MIN + 200));
}

void test_resubscription_with_new_value_has_unknown_age() {
    // Une mise a jour a ete manquee pendant la deconnexion.
    SensorState s(STALE, WINDOW);
    s.onSubscribed(0);
    s.onTemperature(50, 10 * MIN);
    s.onSubscribed(20 * MIN);
    s.onTemperature(47, 20 * MIN + 200);
    TEST_ASSERT_TRUE(s.status(20 * MIN + 200) == Status::AgeUnknown);
    TEST_ASSERT_EQUAL_INT(47, s.tenths());
}

void test_offline_takes_priority() {
    SensorState s(STALE, WINDOW);
    s.onTemperature(50, 0);
    s.onAvailability(false);
    TEST_ASSERT_TRUE(s.status(1000) == Status::Offline);
    TEST_ASSERT_TRUE(s.status(STALE + 10) == Status::Offline);
    s.onAvailability(true);
    TEST_ASSERT_TRUE(s.status(1000) == Status::Fresh);
}

void test_millis_wraparound() {
    SensorState s(STALE, WINDOW);
    const uint32_t t0 = 0xFFFFFFFFUL - 1000;
    s.onTemperature(50, t0);
    TEST_ASSERT_TRUE(s.status(t0 + 5 * MIN) == Status::Fresh);  // deborde
    TEST_ASSERT_EQUAL_UINT32(5 * MIN, s.ageMs(t0 + 5 * MIN));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_no_data_at_start);
    RUN_TEST(test_retained_message_has_unknown_age);
    RUN_TEST(test_live_message_after_window_is_fresh);
    RUN_TEST(test_message_without_subscription_is_fresh);
    RUN_TEST(test_fresh_becomes_stale);
    RUN_TEST(test_unknown_age_also_becomes_stale);
    RUN_TEST(test_resubscription_redelivers_same_value_keeps_age);
    RUN_TEST(test_resubscription_with_new_value_has_unknown_age);
    RUN_TEST(test_offline_takes_priority);
    RUN_TEST(test_millis_wraparound);
    return UNITY_END();
}
