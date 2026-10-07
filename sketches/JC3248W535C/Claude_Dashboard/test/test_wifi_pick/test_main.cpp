#include <unity.h>
#include "wifi_pick.h"

using wifi::Network;
using wifi::pickFirstVisible;
using wifi::pickNextVisible;

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

void test_next_skips_failed_network() {
    // "maison" visible mais refuse : on passe a "bureau"
    const char *visible[] = {"maison", "bureau", "telephone"};
    unsigned failed = 1u << 0;
    TEST_ASSERT_EQUAL_INT(1, pickNextVisible(KNOWN, 3, visible, 3, failed));
    TEST_ASSERT_EQUAL_UINT(1u << 0, failed);
}

void test_next_walks_down_the_list() {
    const char *visible[] = {"telephone", "maison", "bureau"};
    unsigned failed = 0;
    int k = pickNextVisible(KNOWN, 3, visible, 3, failed);
    TEST_ASSERT_EQUAL_INT(0, k);
    failed |= 1u << k;
    k = pickNextVisible(KNOWN, 3, visible, 3, failed);
    TEST_ASSERT_EQUAL_INT(1, k);
    failed |= 1u << k;
    k = pickNextVisible(KNOWN, 3, visible, 3, failed);
    TEST_ASSERT_EQUAL_INT(2, k);
}

void test_next_restarts_when_all_visible_failed() {
    // "telephone" absent : maison et bureau en echec => retour a maison
    const char *visible[] = {"maison", "bureau"};
    unsigned failed = (1u << 0) | (1u << 1);
    TEST_ASSERT_EQUAL_INT(0, pickNextVisible(KNOWN, 3, visible, 2, failed));
    TEST_ASSERT_EQUAL_UINT(0, failed);
}

void test_next_failed_but_invisible_is_kept() {
    // Echec sur "maison", qui n'est plus visible : on garde la marque
    const char *visible[] = {"bureau"};
    unsigned failed = 1u << 0;
    TEST_ASSERT_EQUAL_INT(1, pickNextVisible(KNOWN, 3, visible, 1, failed));
    TEST_ASSERT_EQUAL_UINT(1u << 0, failed);
}

void test_next_no_known_network_visible() {
    const char *visible[] = {"voisin"};
    unsigned failed = 1u << 2;
    TEST_ASSERT_EQUAL_INT(-1, pickNextVisible(KNOWN, 3, visible, 1, failed));
    TEST_ASSERT_EQUAL_UINT(0, failed);
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
    RUN_TEST(test_next_skips_failed_network);
    RUN_TEST(test_next_walks_down_the_list);
    RUN_TEST(test_next_restarts_when_all_visible_failed);
    RUN_TEST(test_next_failed_but_invisible_is_kept);
    RUN_TEST(test_next_no_known_network_visible);
    return UNITY_END();
}
