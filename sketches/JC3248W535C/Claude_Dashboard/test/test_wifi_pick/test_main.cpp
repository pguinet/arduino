#include <unity.h>
#include "wifi_pick.h"

using wifi::Network;
using wifi::pickFirstVisible;

void setUp() {}
void tearDown() {}

static const Network KNOWN[] = {
    {"maison", "a"},
    {"bureau", "b"},
    {"telephone", "c"},
};

void test_first_declared_wins_over_scan_order() {
    // Scan trie par RSSI : "telephone" plus fort, mais "bureau" declare avant
    const char *visible[] = {"telephone", "voisin", "bureau"};
    TEST_ASSERT_EQUAL_INT(1, pickFirstVisible(KNOWN, 3, visible, 3));
}

void test_single_match() {
    const char *visible[] = {"voisin", "telephone"};
    TEST_ASSERT_EQUAL_INT(2, pickFirstVisible(KNOWN, 3, visible, 2));
}

void test_no_known_network_visible() {
    const char *visible[] = {"voisin", "Freebox-1234"};
    TEST_ASSERT_EQUAL_INT(-1, pickFirstVisible(KNOWN, 3, visible, 2));
}

void test_empty_scan() {
    TEST_ASSERT_EQUAL_INT(-1, pickFirstVisible(KNOWN, 3, nullptr, 0));
    const char *visible[] = {"maison"};
    TEST_ASSERT_EQUAL_INT(-1, pickFirstVisible(KNOWN, 3, visible, 0));
}

void test_exact_case_sensitive_match() {
    const char *visible[] = {"Maison", "maison-5G", "maiso"};
    TEST_ASSERT_EQUAL_INT(-1, pickFirstVisible(KNOWN, 3, visible, 3));
}

void test_empty_ssids_ignored() {
    // Reseau cache (SSID vide au scan) et entree configuree vide
    const Network known[] = {{"", "x"}, {nullptr, "y"}, {"bureau", "b"}};
    const char *visible[] = {"", nullptr, "bureau"};
    TEST_ASSERT_EQUAL_INT(2, pickFirstVisible(known, 3, visible, 3));
    const char *hiddenOnly[] = {""};
    TEST_ASSERT_EQUAL_INT(-1, pickFirstVisible(known, 3, hiddenOnly, 1));
}

void test_null_known() {
    const char *visible[] = {"maison"};
    TEST_ASSERT_EQUAL_INT(-1, pickFirstVisible(nullptr, 0, visible, 1));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_first_declared_wins_over_scan_order);
    RUN_TEST(test_single_match);
    RUN_TEST(test_no_known_network_visible);
    RUN_TEST(test_empty_scan);
    RUN_TEST(test_exact_case_sensitive_match);
    RUN_TEST(test_empty_ssids_ignored);
    RUN_TEST(test_null_known);
    return UNITY_END();
}
