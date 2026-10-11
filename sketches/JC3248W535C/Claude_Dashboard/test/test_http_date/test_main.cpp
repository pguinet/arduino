#include <unity.h>
#include "http_date.h"

using namespace httpdate;

void setUp() {}
void tearDown() {}

void test_parses_imf_fixdate() {
    time_t t = 0;
    TEST_ASSERT_TRUE(parse("Thu, 08 Oct 2026 12:34:56 GMT", &t));
    TEST_ASSERT_EQUAL_INT64(1791462896LL, (long long)t);
    TEST_ASSERT_TRUE(parse("Thu, 01 Jan 1970 00:00:00 GMT", &t));
    TEST_ASSERT_EQUAL_INT64(0, (long long)t);
}

void test_leap_day() {
    time_t t = 0;
    TEST_ASSERT_TRUE(parse("Thu, 29 Feb 2024 23:59:59 GMT", &t));
    TEST_ASSERT_EQUAL_INT64(1709251199LL, (long long)t);
    TEST_ASSERT_FALSE(parse("Sun, 29 Feb 2026 00:00:00 GMT", &t));
}

void test_rejects_malformed() {
    time_t t = 42;
    TEST_ASSERT_FALSE(parse("", &t));
    TEST_ASSERT_FALSE(parse("Thu 08 Oct 2026 12:34:56 GMT", &t));
    TEST_ASSERT_FALSE(parse("Thu, 08 Foo 2026 12:34:56 GMT", &t));
    TEST_ASSERT_FALSE(parse("Thu, 08 Oct 2026 12:34:56 CET", &t));
    TEST_ASSERT_FALSE(parse("Thu, 08 Oct 2026 25:00:00 GMT", &t));
    TEST_ASSERT_FALSE(parse("Thu, 32 Oct 2026 12:00:00 GMT", &t));
    TEST_ASSERT_FALSE(parse(nullptr, &t));
    TEST_ASSERT_EQUAL_INT64(42, (long long)t);
}

void test_header_line() {
    time_t t = 0;
    TEST_ASSERT_TRUE(parseHeaderLine("Date: Thu, 08 Oct 2026 12:34:56 GMT", &t));
    TEST_ASSERT_EQUAL_INT64(1791462896LL, (long long)t);
    TEST_ASSERT_TRUE(parseHeaderLine("date:Thu, 08 Oct 2026 12:34:56 GMT\r", &t));
    TEST_ASSERT_FALSE(parseHeaderLine("Last-Modified: Thu, 08 Oct 2026 12:34:56 GMT", &t));
    TEST_ASSERT_FALSE(parseHeaderLine("Dat", &t));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parses_imf_fixdate);
    RUN_TEST(test_leap_day);
    RUN_TEST(test_rejects_malformed);
    RUN_TEST(test_header_line);
    return UNITY_END();
}
