#include <unity.h>
#include <stdio.h>
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
    TEST_ASSERT_EQUAL(20, d.limits(200).h5);
    Row rows[MAX_ROWS];
    TEST_ASSERT_EQUAL(2, d.rows(rows, MAX_ROWS));
}

void test_limits_null_when_never_received() {
    Dashboard d;
    Limits l = d.limits(100);
    TEST_ASSERT_EQUAL(-1, l.h5);
    TEST_ASSERT_EQUAL(-1, l.d7);
    TEST_ASSERT_EQUAL(0, (long)l.h5Reset);
    TEST_ASSERT_EQUAL(0, (long)l.d7Reset);
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
    // vieux message d'un agent muet depuis longtemps, recu maintenant : perime
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

void test_session_count_empty() {
    Dashboard d;
    TEST_ASSERT_EQUAL(0, d.sessionCount());
}

void test_session_count_sums_hosts_any_state() {
    Dashboard d;
    const char *ids[] = {"a", "b"};
    State st[] = {State::Working, State::Permission}, idle[] = {State::Idle};
    int64_t t[] = {1, 2};
    d.apply(mk("h1", 2, ids, st, t), 10);
    d.apply(mk("h2", 1, ids, idle, t), 10);
    TEST_ASSERT_EQUAL(3, d.sessionCount());
}

void test_session_count_after_remove_host() {
    Dashboard d;
    const char *ids[] = {"a", "b"};
    State st[] = {State::Working, State::Idle};
    int64_t t[] = {1, 2};
    d.apply(mk("h1", 2, ids, st, t), 10);
    d.apply(mk("h2", 1, ids, st, t), 10);
    TEST_ASSERT_TRUE(d.removeHost("h1"));
    TEST_ASSERT_EQUAL(1, d.sessionCount());
    TEST_ASSERT_TRUE(d.removeHost("h2"));
    TEST_ASSERT_EQUAL(0, d.sessionCount());
}

void test_session_count_zero_when_host_has_no_session() {
    Dashboard d;
    HostSnapshot s;
    strcpy(s.host, "h");
    d.apply(s, 10);
    TEST_ASSERT_EQUAL(1, d.hostCount());
    TEST_ASSERT_EQUAL(0, d.sessionCount());
}

void test_alert_idle_after_permission() {
    Dashboard d;
    const char *ids[] = {"a"};
    State p[] = {State::Permission}, i[] = {State::Idle};
    int64_t since[] = {10};
    d.apply(mk("h", 1, ids, p, since), 100);
    TEST_ASSERT_EQUAL((int)Alert::Idle, (int)d.apply(mk("h", 1, ids, i, since), 102));
}

void test_session_gone_then_back_no_alert() {
    // une session absente du snapshot precedent est traitee comme nouvelle
    Dashboard d;
    const char *ids[] = {"a"};
    State w[] = {State::Working}, p[] = {State::Permission};
    int64_t t[] = {1};
    d.apply(mk("h", 1, ids, w, t), 100);
    d.apply(mk("h", 0, ids, w, t), 102);
    TEST_ASSERT_EQUAL((int)Alert::None, (int)d.apply(mk("h", 1, ids, p, t), 104));
}

void test_limits_skip_host_without_limits() {
    Dashboard d;
    HostSnapshot s;
    strcpy(s.host, "old");
    s.ts = 100;
    s.limits.d7 = 30;
    d.apply(s, 100);
    HostSnapshot n;
    strcpy(n.host, "new");
    n.ts = 200;  // plus recent, mais sans quotas
    d.apply(n, 200);
    TEST_ASSERT_EQUAL(30, d.limits(200).d7);
}

// Cas reel : l'hote "bureau" republie a chaque heartbeat des quotas lus des
// heures plus tot ; ils ne doivent pas alterner avec ceux, a jour, de "pc".
void test_limits_freshest_wins_not_latest_heartbeat() {
    Dashboard d;
    HostSnapshot pc, bureau;
    strcpy(pc.host, "pc");
    pc.ts = 1000;
    pc.limits = {2, 9000, 0, 90000, 990};
    strcpy(bureau.host, "bureau");
    bureau.ts = 1010;  // heartbeat plus recent...
    bureau.limits = {23, 8000, 55, 80000, 100};  // ...mais quotas lus bien avant
    d.apply(pc, 1000);
    d.apply(bureau, 1010);  // nouvel hote : redessin normal
    Limits l = d.limits(1010);
    TEST_ASSERT_EQUAL(2, l.h5);
    TEST_ASSERT_EQUAL(9000, (long)l.h5Reset);
    TEST_ASSERT_EQUAL(0, l.d7);
    TEST_ASSERT_EQUAL(90000, (long)l.d7Reset);
    // heartbeats suivants dans les deux ordres : toujours "pc", sans redessin
    bool changed = true;
    pc.ts = 1020;
    d.apply(pc, 1020, &changed);
    TEST_ASSERT_FALSE(changed);
    changed = true;
    bureau.ts = 1030;
    d.apply(bureau, 1030, &changed);
    TEST_ASSERT_FALSE(changed);
    TEST_ASSERT_EQUAL(2, d.limits(1030).h5);
}

void test_limits_skip_expired_window() {
    Dashboard d;
    HostSnapshot a, b;
    strcpy(a.host, "a");
    a.ts = 1000;
    a.limits = {23, 500, 55, 800, 990};  // les deux resets sont passes (now = 1000)
    strcpy(b.host, "b");
    b.ts = 1000;
    b.limits = {40, 5000, 60, 50000, 100};  // moins frais mais encore valide
    d.apply(a, 1000);
    d.apply(b, 1000);
    Limits l = d.limits(1000);
    TEST_ASSERT_EQUAL(40, l.h5);
    TEST_ASSERT_EQUAL(60, l.d7);
}

void test_limits_expired_without_candidate_is_unknown() {
    Dashboard d;
    HostSnapshot a;
    strcpy(a.host, "a");
    a.ts = 1000;
    a.limits = {23, 1500, 55, 80000, 990};
    d.apply(a, 1000);
    TEST_ASSERT_EQUAL(23, d.limits(1499).h5);
    Limits l = d.limits(1500);  // reset atteint
    TEST_ASSERT_EQUAL(-1, l.h5);
    TEST_ASSERT_EQUAL(0, (long)l.h5Reset);
    TEST_ASSERT_EQUAL(55, l.d7);  // fenetre 7j independante
    // reset inconnu (0) : jamais considere comme passe
    a.limits = {23, 0, -1, 0, 990};
    d.apply(a, 2000);
    TEST_ASSERT_EQUAL(23, d.limits(99999).h5);
}

void test_limits_windows_chosen_independently() {
    Dashboard d;
    HostSnapshot a, b;
    strcpy(a.host, "a");
    a.ts = 1000;
    a.limits = {10, 5000, -1, 0, 900};  // 5h seulement, le plus frais
    strcpy(b.host, "b");
    b.ts = 1000;
    b.limits = {20, 5000, 30, 50000, 100};
    d.apply(a, 1000);
    d.apply(b, 1000);
    Limits l = d.limits(1000);
    TEST_ASSERT_EQUAL(10, l.h5);
    TEST_ASSERT_EQUAL(30, l.d7);
    TEST_ASSERT_EQUAL(50000, (long)l.d7Reset);
}

void test_limits_updated_preferred_over_ts_of_old_agent() {
    // Agent ancien (sans updated) : sa fraicheur est son ts
    Dashboard d;
    HostSnapshot oldAgent, newAgent;
    strcpy(oldAgent.host, "old");
    oldAgent.ts = 1000;
    oldAgent.limits.h5 = 50;
    strcpy(newAgent.host, "new");
    newAgent.ts = 1010;
    newAgent.limits = {5, 0, -1, 0, 990};
    d.apply(oldAgent, 1000);
    d.apply(newAgent, 1010);
    TEST_ASSERT_EQUAL(50, d.limits(1010).h5);  // ts 1000 > updated 990
    newAgent.limits.updated = 1005;
    d.apply(newAgent, 1020);
    TEST_ASSERT_EQUAL(5, d.limits(1020).h5);
}

// Remplit le tableau de bord avec MAX_HOSTS hotes "h0".."h3" de ts donnes.
static void fillHosts(Dashboard &d, const int64_t ts[MAX_HOSTS], int64_t now) {
    for (int i = 0; i < MAX_HOSTS; i++) {
        HostSnapshot s;
        snprintf(s.host, sizeof s.host, "h%d", i);
        s.ts = ts[i];
        d.apply(s, now);
    }
}

void test_extra_host_ignored_when_none_evictable() {
    static Dashboard d;  // ~5.6 Ko : hors pile
    const int64_t now = 100000;
    const int64_t ts[MAX_HOSTS] = {now, now - 60, now - FORGET_AFTER_S, now - 10};
    fillHosts(d, ts, now);
    HostSnapshot s;
    strcpy(s.host, "extra");
    s.ts = now;
    TEST_ASSERT_EQUAL((int)Alert::None, (int)d.apply(s, now));
    TEST_ASSERT_EQUAL(MAX_HOSTS, d.hostCount());
    for (int i = 0; i < MAX_HOSTS; i++) TEST_ASSERT_NOT_EQUAL(0, strcmp("extra", d.hostName(i)));
}

void test_extra_host_evicts_oldest_dead_host() {
    static Dashboard d;  // ~5.6 Ko : hors pile
    const int64_t now = 100000;
    // h1 et h2 muets depuis plus de 1 h ; h2 est le plus vieux
    const int64_t ts[MAX_HOSTS] = {now, now - FORGET_AFTER_S - 10, now - FORGET_AFTER_S - 500, now};
    fillHosts(d, ts, now);
    HostSnapshot s;
    strcpy(s.host, "extra");
    s.ts = now;
    d.apply(s, now);
    TEST_ASSERT_EQUAL(MAX_HOSTS, d.hostCount());
    TEST_ASSERT_EQUAL_STRING("h0", d.hostName(0));
    TEST_ASSERT_EQUAL_STRING("h1", d.hostName(1));
    TEST_ASSERT_EQUAL_STRING("extra", d.hostName(2));
    TEST_ASSERT_EQUAL_STRING("h3", d.hostName(3));
}

void test_remove_host_compacts() {
    static Dashboard d;  // ~5.6 Ko : hors pile
    const char *ids[] = {"a"};
    State st[] = {State::Idle};
    int64_t t[] = {1};
    d.apply(mk("h1", 1, ids, st, t), 100);
    d.apply(mk("h2", 1, ids, st, t), 100);
    d.apply(mk("h3", 1, ids, st, t), 100);
    TEST_ASSERT_TRUE(d.removeHost("h2"));
    TEST_ASSERT_EQUAL(2, d.hostCount());
    TEST_ASSERT_EQUAL_STRING("h1", d.hostName(0));
    TEST_ASSERT_EQUAL_STRING("h3", d.hostName(1));
    Row rows[MAX_ROWS];
    TEST_ASSERT_EQUAL(2, d.rows(rows, MAX_ROWS));
    TEST_ASSERT_FALSE(d.removeHost("h2"));
    TEST_ASSERT_FALSE(d.removeHost("zzz"));
    TEST_ASSERT_TRUE(d.removeHost("h1"));
    TEST_ASSERT_TRUE(d.removeHost("h3"));
    TEST_ASSERT_EQUAL(0, d.hostCount());
    TEST_ASSERT_EQUAL(0, (long)d.staleSeconds(1000));
}

void test_removed_host_comes_back_without_alert() {
    Dashboard d;
    const char *ids[] = {"a"};
    State w[] = {State::Working}, p[] = {State::Permission};
    int64_t t[] = {1};
    d.apply(mk("h", 1, ids, w, t), 100);
    TEST_ASSERT_TRUE(d.removeHost("h"));
    TEST_ASSERT_EQUAL((int)Alert::None, (int)d.apply(mk("h", 1, ids, p, t), 102));
}

// --- Detection de changement (evite de reconstruire la liste a chaque heartbeat) ---

static HostSnapshot one(const char *host, State st, int64_t ts) {
    const char *ids[] = {"a"};
    State s[] = {st};
    int64_t since[] = {10};
    HostSnapshot h = mk(host, 1, ids, s, since);
    h.ts = ts;
    strcpy(h.sessions[0].project, "p");
    h.sessions[0].ctx = 40;
    h.limits.h5 = 10;
    return h;
}

void test_changed_on_new_host() {
    Dashboard d;
    bool changed = false;
    d.apply(one("h", State::Working, 100), 100, &changed);
    TEST_ASSERT_TRUE(changed);
}

void test_not_changed_when_only_ts_differs() {
    Dashboard d;
    bool changed = true;
    d.apply(one("h", State::Working, 100), 100);
    d.apply(one("h", State::Working, 120), 120, &changed);
    TEST_ASSERT_FALSE(changed);
}

void test_changed_on_session_state() {
    Dashboard d;
    bool changed = false;
    d.apply(one("h", State::Working, 100), 100);
    d.apply(one("h", State::Idle, 120), 120, &changed);
    TEST_ASSERT_TRUE(changed);
}

// Applique base, puis base modifiee par mutate : renvoie le drapeau changed.
static bool changedAfter(const HostSnapshot &base, void (*mutate)(HostSnapshot &)) {
    Dashboard d;
    d.apply(base, 100);
    HostSnapshot s = base;
    mutate(s);
    bool changed = false;
    d.apply(s, 101, &changed);
    return changed;
}

void test_changed_on_session_fields() {
    HostSnapshot base = one("h", State::Working, 100);
    TEST_ASSERT_FALSE(changedAfter(base, [](HostSnapshot &) {}));
    TEST_ASSERT_TRUE(changedAfter(base, [](HostSnapshot &s) { s.sessions[0].ctx = 41; }));
    TEST_ASSERT_TRUE(changedAfter(base, [](HostSnapshot &s) { strcpy(s.sessions[0].tool, "Bash"); }));
    TEST_ASSERT_TRUE(changedAfter(base, [](HostSnapshot &s) { s.sessions[0].since = 11; }));
    TEST_ASSERT_TRUE(changedAfter(base, [](HostSnapshot &s) { strcpy(s.sessions[0].model, "Opus"); }));
    TEST_ASSERT_TRUE(changedAfter(base, [](HostSnapshot &s) { strcpy(s.sessions[0].project, "autre"); }));
    // session remplacee par une autre au contenu identique
    TEST_ASSERT_TRUE(changedAfter(base, [](HostSnapshot &s) { strcpy(s.sessions[0].id, "b"); }));
}

void test_changed_on_session_count() {
    Dashboard d;
    HostSnapshot s = one("h", State::Working, 100);
    d.apply(s, 100);
    s.count = 0;
    bool changed = false;
    d.apply(s, 110, &changed);
    TEST_ASSERT_TRUE(changed);
}

void test_changed_on_limits() {
    Dashboard d;
    HostSnapshot s = one("h", State::Working, 100);
    d.apply(s, 100);
    s.limits.d7Reset = 5000;
    bool changed = false;
    d.apply(s, 110, &changed);
    TEST_ASSERT_TRUE(changed);
}

void test_changed_when_displayed_limits_switch_host() {
    // agents sans limits.updated : fraicheur = ts, un heartbeat de l'autre hote
    // peut changer l'affichage sans que ses propres quotas aient change
    Dashboard d;
    HostSnapshot a = one("a", State::Working, 100), b = one("b", State::Working, 200);
    a.limits.h5 = 10;
    b.limits.h5 = 20;
    d.apply(a, 100);
    d.apply(b, 200);
    TEST_ASSERT_EQUAL(20, d.limits(200).h5);
    a.ts = 300;
    bool changed = false;
    d.apply(a, 300, &changed);
    TEST_ASSERT_EQUAL(10, d.limits(300).h5);
    TEST_ASSERT_TRUE(changed);
}

void test_not_changed_when_host_ignored() {
    static Dashboard d;  // ~5.6 Ko : hors pile
    const int64_t now = 100000;
    const int64_t ts[MAX_HOSTS] = {now, now, now, now};
    fillHosts(d, ts, now);
    HostSnapshot s;
    strcpy(s.host, "extra");
    s.ts = now;
    bool changed = true;
    d.apply(s, now, &changed);
    TEST_ASSERT_FALSE(changed);
}

void test_changed_when_host_evicts_dead_one() {
    static Dashboard d;  // ~5.6 Ko : hors pile
    const int64_t now = 100000;
    const int64_t ts[MAX_HOSTS] = {now, now - FORGET_AFTER_S - 10, now, now};
    fillHosts(d, ts, now);
    HostSnapshot s;
    strcpy(s.host, "extra");
    s.ts = now;
    bool changed = false;
    d.apply(s, now, &changed);
    TEST_ASSERT_EQUAL_STRING("extra", d.hostName(1));
    TEST_ASSERT_TRUE(changed);
}

// --- Oubli des hotes muets (expire) ---

static HostSnapshot withTs(const char *host, int64_t ts) {
    HostSnapshot s;
    strcpy(s.host, host);
    s.ts = ts;
    return s;
}

void test_expire_no_host() {
    Dashboard d;
    TEST_ASSERT_EQUAL(0, d.expire(100000));
    TEST_ASSERT_EQUAL(0, d.hostCount());
}

void test_expire_keeps_fresh_host() {
    Dashboard d;
    d.apply(withTs("h", 100000), 100000);
    TEST_ASSERT_EQUAL(0, d.expire(100000 + 60));
    TEST_ASSERT_EQUAL(1, d.hostCount());
}

void test_expire_keeps_host_at_limit() {
    Dashboard d;
    d.apply(withTs("h", 100000), 100000);
    TEST_ASSERT_EQUAL(0, d.expire(100000 + FORGET_AFTER_S));
    TEST_ASSERT_EQUAL(1, d.hostCount());
}

void test_expire_removes_host_past_limit() {
    Dashboard d;
    d.apply(withTs("h", 100000), 100000);
    TEST_ASSERT_EQUAL(1, d.expire(100000 + FORGET_AFTER_S + 1));
    TEST_ASSERT_EQUAL(0, d.hostCount());
    TEST_ASSERT_EQUAL(0, d.sessionCount());
}

void test_expire_uses_reception_when_ts_unknown() {
    Dashboard d;
    d.apply(withTs("h", 0), 100000);
    TEST_ASSERT_EQUAL(0, d.expire(100000 + FORGET_AFTER_S));
    TEST_ASSERT_EQUAL(1, d.expire(100000 + FORGET_AFTER_S + 1));
}

void test_expire_multiple_hosts_keeps_order() {
    static Dashboard d;  // ~5.6 Ko : hors pile
    const int64_t now = 100000;
    const int64_t ts[MAX_HOSTS] = {now - FORGET_AFTER_S - 5, now, now - FORGET_AFTER_S - 1, now - 30};
    fillHosts(d, ts, now);
    TEST_ASSERT_EQUAL(2, d.expire(now));
    TEST_ASSERT_EQUAL(2, d.hostCount());
    TEST_ASSERT_EQUAL_STRING("h1", d.hostName(0));
    TEST_ASSERT_EQUAL_STRING("h3", d.hostName(1));
    TEST_ASSERT_EQUAL(0, d.expire(now));
}

void test_expired_host_comes_back_as_new_without_alert() {
    Dashboard d;
    const int64_t now = 100000;
    HostSnapshot w = one("h", State::Working, now);
    d.apply(w, now);
    const int64_t later = now + FORGET_AFTER_S + 1;
    TEST_ASSERT_EQUAL(1, d.expire(later));
    HostSnapshot p = one("h", State::Permission, later);
    bool changed = false;
    TEST_ASSERT_EQUAL((int)Alert::None, (int)d.apply(p, later, &changed));
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_EQUAL(1, d.hostCount());
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
    RUN_TEST(test_session_count_empty);
    RUN_TEST(test_session_count_sums_hosts_any_state);
    RUN_TEST(test_session_count_after_remove_host);
    RUN_TEST(test_session_count_zero_when_host_has_no_session);
    RUN_TEST(test_alert_idle_after_permission);
    RUN_TEST(test_session_gone_then_back_no_alert);
    RUN_TEST(test_limits_skip_host_without_limits);
    RUN_TEST(test_limits_freshest_wins_not_latest_heartbeat);
    RUN_TEST(test_limits_skip_expired_window);
    RUN_TEST(test_limits_expired_without_candidate_is_unknown);
    RUN_TEST(test_limits_windows_chosen_independently);
    RUN_TEST(test_limits_updated_preferred_over_ts_of_old_agent);
    RUN_TEST(test_extra_host_ignored_when_none_evictable);
    RUN_TEST(test_extra_host_evicts_oldest_dead_host);
    RUN_TEST(test_remove_host_compacts);
    RUN_TEST(test_removed_host_comes_back_without_alert);
    RUN_TEST(test_changed_on_new_host);
    RUN_TEST(test_not_changed_when_only_ts_differs);
    RUN_TEST(test_changed_on_session_state);
    RUN_TEST(test_changed_on_session_fields);
    RUN_TEST(test_changed_on_session_count);
    RUN_TEST(test_changed_on_limits);
    RUN_TEST(test_changed_when_displayed_limits_switch_host);
    RUN_TEST(test_not_changed_when_host_ignored);
    RUN_TEST(test_changed_when_host_evicts_dead_one);
    RUN_TEST(test_expire_no_host);
    RUN_TEST(test_expire_keeps_fresh_host);
    RUN_TEST(test_expire_keeps_host_at_limit);
    RUN_TEST(test_expire_removes_host_past_limit);
    RUN_TEST(test_expire_uses_reception_when_ts_unknown);
    RUN_TEST(test_expire_multiple_hosts_keeps_order);
    RUN_TEST(test_expired_host_comes_back_as_new_without_alert);
    return UNITY_END();
}
