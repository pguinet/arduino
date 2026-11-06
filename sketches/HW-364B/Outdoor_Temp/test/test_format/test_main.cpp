#include <unity.h>
#include "outdoor_logic.h"

using namespace outdoor;

void setUp() {}
void tearDown() {}

static const uint32_t MIN = 60000UL;
static const uint32_t HOUR = 60 * MIN;

void test_to_tenths_rounds_half_away_from_zero() {
    TEST_ASSERT_EQUAL_INT(82, toTenths(8.2f));
    TEST_ASSERT_EQUAL_INT(83, toTenths(8.25f));
    TEST_ASSERT_EQUAL_INT(-4, toTenths(-0.44f));
    TEST_ASSERT_EQUAL_INT(-5, toTenths(-0.46f));  // -0.45f n'est pas exact en float
    TEST_ASSERT_EQUAL_INT(0, toTenths(-0.04f));
}

void test_format_tenths() {
    char buf[8];
    formatTenths(82, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("8.2", buf);
    formatTenths(-4, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("-0.4", buf);
    formatTenths(0, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("0.0", buf);
    formatTenths(-123, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("-12.3", buf);
    formatTenths(350, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("35.0", buf);
}

void test_format_tenths_truncates_safely() {
    char buf[3];
    formatTenths(-123, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("-1", buf);
}

void test_format_age() {
    char buf[20];
    formatAge(0, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("a l'instant", buf);
    formatAge(MIN - 1, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("a l'instant", buf);
    formatAge(MIN, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("il y a 1 min", buf);
    formatAge(59 * MIN + 59000, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("il y a 59 min", buf);
    formatAge(HOUR, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("il y a 1 h 00", buf);
    formatAge(2 * HOUR + 5 * MIN, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("il y a 2 h 05", buf);
    formatAge(48 * HOUR, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("il y a 2 j", buf);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_to_tenths_rounds_half_away_from_zero);
    RUN_TEST(test_format_tenths);
    RUN_TEST(test_format_tenths_truncates_safely);
    RUN_TEST(test_format_age);
    return UNITY_END();
}
