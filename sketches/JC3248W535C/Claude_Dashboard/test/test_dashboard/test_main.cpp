#include <unity.h>
#include <string.h>
#include "dash_model.h"

using namespace dash;

static HostSnapshot mk(const char *host, int n, const char *const ids[], const State states[],
                       const int64_t since[]) {
    HostSnapshot s;
    strcpy(s.host, host);
    s.count = n;
    for (int i = 0; i < n; i++) {
        strcpy(s.sessions[i].id, ids[i]);
        s.sessions[i].state = states[i];
        s.sessions[i].since = since[i];
    }
    return s;
}

void setUp() {}
void tearDown() {}

void test_no_alert_on_first_snapshot() {
    Dashboard d;
    const char *ids[] = {"a"};
    State st[] = {State::Permission};
    int64_t since[] = {10};
    TEST_ASSERT_EQUAL((int)Alert::None, (int)d.apply(mk("h", 1, ids, st, since), 100));
}

void test_alert_on_transition_to_permission() {
    Dashboard d;
    const char *ids[] = {"a"};
    State w[] = {State::Working}, p[] = {State::Permission};
    int64_t since[] = {10};
    d.apply(mk("h", 1, ids, w, since), 100);
    TEST_ASSERT_EQUAL((int)Alert::Permission, (int)d.apply(mk("h", 1, ids, p, since), 102));
    // meme etat re-publie : pas de nouveau bip
    TEST_ASSERT_EQUAL((int)Alert::None, (int)d.apply(mk("h", 1, ids, p, since), 104));
}

void test_alert_idle_after_working() {
    Dashboard d;
    const char *ids[] = {"a"};
    State w[] = {State::Working}, i[] = {State::Idle};
    int64_t since[] = {10};
    d.apply(mk("h", 1, ids, w, since), 100);
    TEST_ASSERT_EQUAL((int)Alert::Idle, (int)d.apply(mk("h", 1, ids, i, since), 102));
}

void test_new_session_no_alert() {
    Dashboard d;
    const char *ids1[] = {"a"}, *ids2[] = {"a", "b"};
    State s1[] = {State::Working}, s2[] = {State::Working, State::Idle};
    int64_t t1[] = {1}, t2[] = {1, 2};
    d.apply(mk("h", 1, ids1, s1, t1), 100);
    TEST_ASSERT_EQUAL((int)Alert::None, (int)d.apply(mk("h", 2, ids2, s2, t2), 102));
}

void test_permission_wins_over_idle() {
    Dashboard d;
    const char *ids[] = {"a", "b"};
    State before[] = {State::Working, State::Working};
    State after[] = {State::Idle, State::Permission};
    int64_t t[] = {1, 1};
    d.apply(mk("h", 2, ids, before, t), 100);
    TEST_ASSERT_EQUAL((int)Alert::Permission, (int)d.apply(mk("h", 2, ids, after, t), 102));
}

void test_no_alert_when_previous_snapshot_was_stale() {
    // Retour du serveur apres une longue coupure : pas de salve de bips.
    Dashboard d;
    const char *ids[] = {"a"};
    State w[] = {State::Working}, p[] = {State::Permission}, i[] = {State::Idle};
    int64_t t[] = {1};
    HostSnapshot s = mk("h", 1, ids, w, t);
    s.ts = 100;
    d.apply(s, 100);
    s = mk("h", 1, ids, p, t);
    s.ts = 100 + STALE_AFTER_S + 1;
    TEST_ASSERT_EQUAL((int)Alert::None, (int)d.apply(s, 100 + STALE_AFTER_S + 1));
    // le snapshot suivant est frais : les transitions alertent a nouveau
    s = mk("h", 1, ids, i, t);
    s.ts = 100 + STALE_AFTER_S + 3;
    TEST_ASSERT_EQUAL((int)Alert::Idle, (int)d.apply(s, 100 + STALE_AFTER_S + 3));
}

void test_alert_when_previous_snapshot_at_stale_limit() {
    // age == STALE_AFTER_S : pas encore perime
    Dashboard d;
    const char *ids[] = {"a"};
    State w[] = {State::Working}, p[] = {State::Permission};
    int64_t t[] = {1};
    HostSnapshot s = mk("h", 1, ids, w, t);
    s.ts = 100;
    d.apply(s, 100);
    s = mk("h", 1, ids, p, t);
    s.ts = 100 + STALE_AFTER_S;
    TEST_ASSERT_EQUAL((int)Alert::Permission, (int)d.apply(s, 100 + STALE_AFTER_S));
}

void test_rows_sorted_by_urgency_then_age() {
    Dashboard d;
    const char *ids[] = {"w", "i_new", "p", "i_old"};
    State st[] = {State::Working, State::Idle, State::Permission, State::Idle};
    int64_t since[] = {50, 90, 80, 20};
    d.apply(mk("h", 4, ids, st, since), 100);
    Row rows[MAX_ROWS];
    int n = d.rows(rows, MAX_ROWS);
    TEST_ASSERT_EQUAL(4, n);
    TEST_ASSERT_EQUAL_STRING("p", rows[0].session->id);
    TEST_ASSERT_EQUAL_STRING("i_old", rows[1].session->id);
    TEST_ASSERT_EQUAL_STRING("i_new", rows[2].session->id);
    TEST_ASSERT_EQUAL_STRING("w", rows[3].session->id);
    TEST_ASSERT_EQUAL_STRING("h", rows[0].host);
}

void test_rows_sort_is_stable() {
    // meme urgence et meme anciennete : ordre d'origine conserve
    Dashboard d;
    const char *ids[] = {"x1", "w", "x2", "x3"};
    State st[] = {State::Idle, State::Working, State::Idle, State::Idle};
    int64_t since[] = {5, 1, 5, 5};
    d.apply(mk("h", 4, ids, st, since), 100);
    Row rows[MAX_ROWS];
    TEST_ASSERT_EQUAL(4, d.rows(rows, MAX_ROWS));
    TEST_ASSERT_EQUAL_STRING("x1", rows[0].session->id);
    TEST_ASSERT_EQUAL_STRING("x2", rows[1].session->id);
    TEST_ASSERT_EQUAL_STRING("x3", rows[2].session->id);
    TEST_ASSERT_EQUAL_STRING("w", rows[3].session->id);
}

void test_rows_capped_by_max() {
    Dashboard d;
    const char *ids[] = {"a", "b", "c"};
    State st[] = {State::Idle, State::Idle, State::Idle};
    int64_t since[] = {1, 2, 3};
    d.apply(mk("h", 3, ids, st, since), 100);
    Row rows[2];
    TEST_ASSERT_EQUAL(2, d.rows(rows, 2));
}

void test_multi_host_and_latest_limits() {
    Dashboard d;
    const char *ids[] = {"a"};
    State st[] = {State::Idle};
    int64_t t[] = {1};
    HostSnapshot h1 = mk("h1", 1, ids, st, t), h2 = mk("h2", 1, ids, st, t);
    h1.ts = 100; h1.limits.h5 = 10;
    h2.ts = 200; h2.limits.h5 = 20;
    d.apply(h1, 100);
    d.apply(h2, 200);
    TEST_ASSERT_EQUAL(2, d.hostCount());
    TEST_ASSERT_EQUAL(20, d.limits()->h5);
    Row rows[MAX_ROWS];
    TEST_ASSERT_EQUAL(2, d.rows(rows, MAX_ROWS));
}

void test_limits_null_when_never_received() {
    Dashboard d;
    TEST_ASSERT_NULL(d.limits());
}

void test_stale_seconds() {
    // snapshot sans ts : repli sur l'heure de reception
    Dashboard d;
    TEST_ASSERT_EQUAL(0, (long)d.staleSeconds(1000));
    HostSnapshot s;
    strcpy(s.host, "h");
    d.apply(s, 1000);
    TEST_ASSERT_EQUAL(200, (long)d.staleSeconds(1200));
}

void test_stale_seconds_uses_snapshot_ts() {
    // message retained d'un agent mort depuis longtemps, recu maintenant : perime
    Dashboard d;
    HostSnapshot s;
    strcpy(s.host, "h");
    s.ts = 100;
    d.apply(s, 1000);
    TEST_ASSERT_EQUAL(900, (long)d.staleSeconds(1000));
}

void test_stale_seconds_future_ts_clamped() {
    // horloge non synchronisee ou decalee : jamais d'age negatif
    Dashboard d;
    HostSnapshot s;
    strcpy(s.host, "h");
    s.ts = 2000;
    d.apply(s, 1000);
    TEST_ASSERT_EQUAL(0, (long)d.staleSeconds(1000));
}

void test_stale_seconds_max_over_hosts() {
    Dashboard d;
    HostSnapshot s;
    strcpy(s.host, "h1");
    s.ts = 900;
    d.apply(s, 1000);
    strcpy(s.host, "h2");
    s.ts = 500;
    d.apply(s, 1000);
    TEST_ASSERT_EQUAL(500, (long)d.staleSeconds(1000));
}

void test_any_waiting() {
    Dashboard d;
    const char *ids[] = {"a"};
    State w[] = {State::Working}, i[] = {State::Idle};
    int64_t t[] = {1};
    d.apply(mk("h", 1, ids, w, t), 1);
    TEST_ASSERT_FALSE(d.anyWaiting());
    d.apply(mk("h", 1, ids, i, t), 2);
    TEST_ASSERT_TRUE(d.anyWaiting());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_no_alert_on_first_snapshot);
    RUN_TEST(test_alert_on_transition_to_permission);
    RUN_TEST(test_alert_idle_after_working);
    RUN_TEST(test_new_session_no_alert);
    RUN_TEST(test_permission_wins_over_idle);
    RUN_TEST(test_no_alert_when_previous_snapshot_was_stale);
    RUN_TEST(test_alert_when_previous_snapshot_at_stale_limit);
    RUN_TEST(test_rows_sorted_by_urgency_then_age);
    RUN_TEST(test_rows_sort_is_stable);
    RUN_TEST(test_rows_capped_by_max);
    RUN_TEST(test_multi_host_and_latest_limits);
    RUN_TEST(test_limits_null_when_never_received);
    RUN_TEST(test_stale_seconds);
    RUN_TEST(test_stale_seconds_uses_snapshot_ts);
    RUN_TEST(test_stale_seconds_future_ts_clamped);
    RUN_TEST(test_stale_seconds_max_over_hosts);
    RUN_TEST(test_any_waiting);
    return UNITY_END();
}
