#include <unity.h>
#include "temp_logic.h"

using namespace temp;

void setUp() {}
void tearDown() {}

void test_starts_offline() {
    ProbeHealth h;
    TEST_ASSERT_FALSE(h.online());
}

void test_first_valid_reading_brings_online() {
    ProbeHealth h;
    TEST_ASSERT_TRUE(h.record(true));   // changement
    TEST_ASSERT_TRUE(h.online());
    TEST_ASSERT_FALSE(h.record(true));  // pas de changement
}

void test_goes_offline_after_three_failures() {
    ProbeHealth h;
    h.record(true);
    TEST_ASSERT_FALSE(h.record(false));
    TEST_ASSERT_FALSE(h.record(false));
    TEST_ASSERT_TRUE(h.online());
    TEST_ASSERT_TRUE(h.record(false));  // 3e echec : changement
    TEST_ASSERT_FALSE(h.online());
    TEST_ASSERT_FALSE(h.record(false));
}

void test_valid_reading_resets_failure_count() {
    ProbeHealth h;
    h.record(true);
    h.record(false);
    h.record(false);
    h.record(true);
    h.record(false);
    h.record(false);
    TEST_ASSERT_TRUE(h.online());
}

void test_recovers_after_offline() {
    ProbeHealth h;
    h.record(true);
    h.record(false);
    h.record(false);
    h.record(false);
    TEST_ASSERT_TRUE(h.record(true));
    TEST_ASSERT_TRUE(h.online());
}

void test_zero_max_failures_acts_as_one() {
    ProbeHealth h(0);
    h.record(true);
    TEST_ASSERT_TRUE(h.record(false));
    TEST_ASSERT_FALSE(h.online());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_starts_offline);
    RUN_TEST(test_first_valid_reading_brings_online);
    RUN_TEST(test_goes_offline_after_three_failures);
    RUN_TEST(test_valid_reading_resets_failure_count);
    RUN_TEST(test_recovers_after_offline);
    RUN_TEST(test_zero_max_failures_acts_as_one);
    return UNITY_END();
}
