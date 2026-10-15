#include <math.h>
#include <unity.h>
#include "temp_logic.h"

using namespace temp;

void setUp() {}
void tearDown() {}

void test_disconnected_is_rejected() {
    TEST_ASSERT_FALSE(isValidReading(-127.0f, false, 0.0f));
    TEST_ASSERT_FALSE(isValidReading(-127.0f, true, 20.0f));
}

void test_power_on_85_rejected_without_close_previous() {
    TEST_ASSERT_FALSE(isValidReading(85.0f, false, 0.0f));
    TEST_ASSERT_FALSE(isValidReading(85.0f, true, 21.0f));
}

void test_real_85_accepted_when_previous_is_close() {
    TEST_ASSERT_TRUE(isValidReading(85.0f, true, 84.5f));
    TEST_ASSERT_TRUE(isValidReading(85.0f, true, 75.0f));
    TEST_ASSERT_FALSE(isValidReading(85.0f, true, 74.9f));
}

void test_range_bounds() {
    TEST_ASSERT_TRUE(isValidReading(-55.0f, false, 0.0f));
    TEST_ASSERT_TRUE(isValidReading(125.0f, false, 0.0f));
    TEST_ASSERT_FALSE(isValidReading(-55.1f, false, 0.0f));
    TEST_ASSERT_FALSE(isValidReading(125.1f, false, 0.0f));
}

void test_nan_rejected() {
    TEST_ASSERT_FALSE(isValidReading(NAN, false, 0.0f));
}

void test_ordinary_readings_accepted() {
    TEST_ASSERT_TRUE(isValidReading(21.4375f, false, 0.0f));
    TEST_ASSERT_TRUE(isValidReading(-3.0625f, true, -2.5f));
}

void test_to_tenths_rounds_half_away_from_zero() {
    TEST_ASSERT_EQUAL_INT(214, toTenths(21.4375f));
    TEST_ASSERT_EQUAL_INT(215, toTenths(21.45f));
    TEST_ASSERT_EQUAL_INT(0, toTenths(0.04f));
    TEST_ASSERT_EQUAL_INT(-31, toTenths(-3.0625f));
    TEST_ASSERT_EQUAL_INT(-5, toTenths(-0.5f));
}

void test_format_tenths() {
    char b[8];
    formatTenths(214, b, sizeof b);  TEST_ASSERT_EQUAL_STRING("21.4", b);
    formatTenths(0, b, sizeof b);    TEST_ASSERT_EQUAL_STRING("0.0", b);
    formatTenths(-5, b, sizeof b);   TEST_ASSERT_EQUAL_STRING("-0.5", b);
    formatTenths(-31, b, sizeof b);  TEST_ASSERT_EQUAL_STRING("-3.1", b);
    formatTenths(1250, b, sizeof b); TEST_ASSERT_EQUAL_STRING("125.0", b);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_disconnected_is_rejected);
    RUN_TEST(test_power_on_85_rejected_without_close_previous);
    RUN_TEST(test_real_85_accepted_when_previous_is_close);
    RUN_TEST(test_range_bounds);
    RUN_TEST(test_nan_rejected);
    RUN_TEST(test_ordinary_readings_accepted);
    RUN_TEST(test_to_tenths_rounds_half_away_from_zero);
    RUN_TEST(test_format_tenths);
    return UNITY_END();
}
