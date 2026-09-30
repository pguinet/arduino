#include <unity.h>
#include <stdint.h>
#include "dash_model.h"

using namespace dash;

void setUp() {}
void tearDown() {}

void test_format_duration() {
    char b[16];
    formatDuration(0, b, sizeof b);      TEST_ASSERT_EQUAL_STRING("0:00", b);
    formatDuration(42, b, sizeof b);     TEST_ASSERT_EQUAL_STRING("0:42", b);
    formatDuration(310, b, sizeof b);    TEST_ASSERT_EQUAL_STRING("5:10", b);
    formatDuration(3599, b, sizeof b);   TEST_ASSERT_EQUAL_STRING("59:59", b);
    formatDuration(3600, b, sizeof b);   TEST_ASSERT_EQUAL_STRING("1h00", b);
    formatDuration(3900, b, sizeof b);   TEST_ASSERT_EQUAL_STRING("1h05", b);
    formatDuration(86399, b, sizeof b);  TEST_ASSERT_EQUAL_STRING("23h59", b);
    formatDuration(86400, b, sizeof b);  TEST_ASSERT_EQUAL_STRING("1j", b);
    formatDuration(200000, b, sizeof b); TEST_ASSERT_EQUAL_STRING("2j", b);
    formatDuration(-5, b, sizeof b);     TEST_ASSERT_EQUAL_STRING("0:00", b);
}

void test_format_duration_huge() {
    char b[16];
    // since aberrant (0 ou horloge folle) : pas de debordement des casts int
    formatDuration(999LL * 86400, b, sizeof b);       TEST_ASSERT_EQUAL_STRING("999j", b);
    formatDuration(1000LL * 86400, b, sizeof b);      TEST_ASSERT_EQUAL_STRING("999j+", b);
    formatDuration(INT64_MAX, b, sizeof b);           TEST_ASSERT_EQUAL_STRING("999j+", b);
    formatDuration(INT64_MIN, b, sizeof b);           TEST_ASSERT_EQUAL_STRING("0:00", b);
}

void test_format_duration_small_buffer() {
    char b[4] = "xxx";
    formatDuration(3599, b, sizeof b);  // "59:59" tronque proprement
    TEST_ASSERT_EQUAL_STRING("59:", b);
    char z[1] = {'x'};
    formatDuration(42, z, 0);  // size 0 : rien n'est ecrit
    TEST_ASSERT_EQUAL_CHAR('x', z[0]);
}

void test_color_thresholds() {
    TEST_ASSERT_EQUAL_HEX32(0x4caf50, colorForPercent(0));
    TEST_ASSERT_EQUAL_HEX32(0x4caf50, colorForPercent(59));
    TEST_ASSERT_EQUAL_HEX32(0xfca311, colorForPercent(60));
    TEST_ASSERT_EQUAL_HEX32(0xfca311, colorForPercent(84));
    TEST_ASSERT_EQUAL_HEX32(0xf72585, colorForPercent(85));
    TEST_ASSERT_EQUAL_HEX32(0xf72585, colorForPercent(100));
}

void test_state_labels() {
    TEST_ASSERT_EQUAL_STRING("PERMISSION", stateLabel(State::Permission));
    TEST_ASSERT_EQUAL_STRING("ATTENTE", stateLabel(State::Idle));
    TEST_ASSERT_EQUAL_STRING("TRAVAILLE", stateLabel(State::Working));
}

void test_screen_policy() {
    const uint32_t T = 600000;
    TEST_ASSERT_TRUE(screenShouldBeOn(true, 10 * T, 0));
    TEST_ASSERT_TRUE(screenShouldBeOn(false, T - 1, 0));
    TEST_ASSERT_FALSE(screenShouldBeOn(false, T, 0));
    // debordement de millis() (~49 j)
    TEST_ASSERT_TRUE(screenShouldBeOn(false, 100, 0xFFFFFF00u));
    TEST_ASSERT_FALSE(screenShouldBeOn(false, T, 0xFFFFFF00u));
    // timeout explicite
    TEST_ASSERT_TRUE(screenShouldBeOn(false, 999, 0, 1000));
    TEST_ASSERT_FALSE(screenShouldBeOn(false, 1000, 0, 1000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_format_duration);
    RUN_TEST(test_format_duration_huge);
    RUN_TEST(test_format_duration_small_buffer);
    RUN_TEST(test_color_thresholds);
    RUN_TEST(test_state_labels);
    RUN_TEST(test_screen_policy);
    return UNITY_END();
}
