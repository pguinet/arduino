#include <unity.h>
#include "temp_logic.h"

using namespace temp;

void setUp() {}
void tearDown() {}

void test_first_value_is_published() {
    PublishPolicy p;
    TEST_ASSERT_TRUE(p.shouldPublish(214, 0));
}

void test_small_change_is_not_published() {
    PublishPolicy p;
    p.markPublished(214, 1000);
    TEST_ASSERT_FALSE(p.shouldPublish(215, 2000));
    TEST_ASSERT_FALSE(p.shouldPublish(213, 2000));
}

void test_change_at_threshold_is_published() {
    PublishPolicy p;
    p.markPublished(214, 1000);
    TEST_ASSERT_TRUE(p.shouldPublish(216, 2000));
    TEST_ASSERT_TRUE(p.shouldPublish(212, 2000));
}

void test_published_after_max_interval() {
    PublishPolicy p;
    p.markPublished(214, 1000);
    TEST_ASSERT_FALSE(p.shouldPublish(214, 1000 + 299999));
    TEST_ASSERT_TRUE(p.shouldPublish(214, 1000 + 300000));
}

void test_millis_wraparound() {
    PublishPolicy p;
    p.markPublished(214, 0xFFFFFF00u);
    TEST_ASSERT_FALSE(p.shouldPublish(214, 0x00000100u));
    TEST_ASSERT_TRUE(p.shouldPublish(214, 0xFFFFFF00u + 300000u));
}

void test_reset_forces_next_publish() {
    PublishPolicy p;
    p.markPublished(214, 1000);
    p.reset();
    TEST_ASSERT_TRUE(p.shouldPublish(214, 1001));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_first_value_is_published);
    RUN_TEST(test_small_change_is_not_published);
    RUN_TEST(test_change_at_threshold_is_published);
    RUN_TEST(test_published_after_max_interval);
    RUN_TEST(test_millis_wraparound);
    RUN_TEST(test_reset_forces_next_publish);
    return UNITY_END();
}
