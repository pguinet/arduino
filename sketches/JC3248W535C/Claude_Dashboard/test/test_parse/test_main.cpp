#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "dash_model.h"

using namespace dash;

static const char *SAMPLE =
    "{\"host\":\"srv\",\"ts\":1000,"
    "\"limits\":{\"h5\":42,\"h5_reset\":4600,\"d7\":18,\"d7_reset\":90000,\"updated\":990},"
    "\"sessions\":["
    "{\"id\":\"a1\",\"project\":\"arduino\",\"model\":\"Opus 5.5\",\"state\":\"permission\","
    "\"since\":950,\"ctx\":37,\"tool\":\"Bash\"},"
    "{\"id\":\"b2\",\"project\":\"api\",\"model\":\"Sonnet\",\"state\":\"idle\",\"since\":700}"
    "]}";

void setUp() {}
void tearDown() {}

void test_parse_full() {
    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(SAMPLE, strlen(SAMPLE), s));
    TEST_ASSERT_EQUAL_STRING("srv", s.host);
    TEST_ASSERT_EQUAL(1000, (long)s.ts);
    TEST_ASSERT_EQUAL(42, s.limits.h5);
    TEST_ASSERT_EQUAL(4600, (long)s.limits.h5Reset);
    TEST_ASSERT_EQUAL(18, s.limits.d7);
    TEST_ASSERT_EQUAL(90000, (long)s.limits.d7Reset);
    TEST_ASSERT_EQUAL(990, (long)s.limits.updated);
    TEST_ASSERT_EQUAL(2, s.count);
    TEST_ASSERT_EQUAL_STRING("a1", s.sessions[0].id);
    TEST_ASSERT_EQUAL_STRING("arduino", s.sessions[0].project);
    TEST_ASSERT_EQUAL_STRING("Opus 5.5", s.sessions[0].model);
    TEST_ASSERT_EQUAL((int)State::Permission, (int)s.sessions[0].state);
    TEST_ASSERT_EQUAL(950, (long)s.sessions[0].since);
    TEST_ASSERT_EQUAL(37, s.sessions[0].ctx);
    TEST_ASSERT_EQUAL_STRING("Bash", s.sessions[0].tool);
    TEST_ASSERT_EQUAL((int)State::Idle, (int)s.sessions[1].state);
    TEST_ASSERT_EQUAL(-1, s.sessions[1].ctx);  // absent
    TEST_ASSERT_EQUAL_STRING("", s.sessions[1].tool);
}

void test_parse_no_limits() {
    const char *j = "{\"host\":\"h\",\"ts\":1,\"sessions\":[]}";
    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(j, strlen(j), s));
    TEST_ASSERT_EQUAL(-1, s.limits.h5);
    TEST_ASSERT_EQUAL(-1, s.limits.d7);
    TEST_ASSERT_EQUAL(0, (long)s.limits.updated);
    TEST_ASSERT_EQUAL(0, s.count);
}

void test_parse_invalid_json() {
    HostSnapshot s;
    TEST_ASSERT_FALSE(parseSnapshot("{oops", 5, s));
}

void test_parse_missing_host() {
    const char *j = "{\"ts\":1,\"sessions\":[]}";
    HostSnapshot s;
    TEST_ASSERT_FALSE(parseSnapshot(j, strlen(j), s));
}

void test_parse_failure_leaves_output_untouched() {
    const char *bad[] = {"{oops", "{\"ts\":1,\"sessions\":[]}",
                         "{\"host\":\"\",\"ts\":1,\"sessions\":[]}"};
    for (const char *j : bad) {
        HostSnapshot s;
        strcpy(s.host, "keep");
        s.count = 3;
        TEST_ASSERT_FALSE_MESSAGE(parseSnapshot(j, strlen(j), s), j);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("keep", s.host, j);
        TEST_ASSERT_EQUAL_MESSAGE(3, s.count, j);
    }
}

void test_host_of_32_chars_kept_intact() {
    const char *j = "{\"host\":\"0123456789abcdef0123456789ABCDEF\",\"ts\":1,\"sessions\":[]}";
    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(j, strlen(j), s));
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789ABCDEF", s.host);
}

void test_invalid_session_entries_skipped() {
    const char *j =
        "{\"host\":\"h\",\"ts\":1,\"sessions\":[1,\"x\",null,[],{\"state\":\"idle\"},"
        "{\"id\":\"\",\"state\":\"idle\"},{\"id\":\"ok\",\"state\":\"idle\",\"since\":1}]}";
    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(j, strlen(j), s));
    TEST_ASSERT_EQUAL(1, s.count);
    TEST_ASSERT_EQUAL_STRING("ok", s.sessions[0].id);
}

void test_reparse_resets_previous_content() {
    // Le snapshot est reutilise (static dans main.cpp) : aucun residu du parse precedent.
    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(SAMPLE, strlen(SAMPLE), s));
    const char *j =
        "{\"host\":\"h2\",\"sessions\":[{\"id\":\"z\",\"state\":\"working\"}]}";
    TEST_ASSERT_TRUE(parseSnapshot(j, strlen(j), s));
    TEST_ASSERT_EQUAL_STRING("h2", s.host);
    TEST_ASSERT_EQUAL(0, (long)s.ts);
    TEST_ASSERT_EQUAL(-1, s.limits.h5);
    TEST_ASSERT_EQUAL(0, (long)s.limits.h5Reset);
    TEST_ASSERT_EQUAL(-1, s.limits.d7);
    TEST_ASSERT_EQUAL(0, (long)s.limits.d7Reset);
    TEST_ASSERT_EQUAL(1, s.count);
    const Session &z = s.sessions[0];
    TEST_ASSERT_EQUAL_STRING("z", z.id);
    TEST_ASSERT_EQUAL_STRING("", z.project);
    TEST_ASSERT_EQUAL_STRING("", z.model);
    TEST_ASSERT_EQUAL_STRING("", z.tool);
    TEST_ASSERT_EQUAL((int)State::Working, (int)z.state);
    TEST_ASSERT_EQUAL(0, (long)z.since);
    TEST_ASSERT_EQUAL(-1, z.ctx);
}

void test_parse_truncates_long_strings_and_caps_sessions() {
    static const char LONG[] = "pppppppppppppppppppppppppppppppppppppppppppppp";  // 46 > 32
    char buf[8192];
    size_t n = (size_t)snprintf(buf, sizeof buf, "{\"host\":\"h\",\"ts\":1,\"sessions\":[");
    for (int i = 0; i < 20; i++)
        n += (size_t)snprintf(buf + n, sizeof buf - n,
                              "%s{\"id\":\"s%d\",\"project\":\"%s\",\"state\":\"working\",\"since\":1}",
                              i ? "," : "", i, LONG);
    n += (size_t)snprintf(buf + n, sizeof buf - n, "]}");
    TEST_ASSERT_TRUE_MESSAGE(n < sizeof buf, "buffer de test trop petit");

    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(buf, n, s));
    TEST_ASSERT_EQUAL(MAX_SESSIONS, s.count);
    TEST_ASSERT_EQUAL_STRING("s11", s.sessions[MAX_SESSIONS - 1].id);
    TEST_ASSERT_EQUAL(sizeof(s.sessions[0].project) - 1, strlen(s.sessions[0].project));
    TEST_ASSERT_EQUAL(0, strncmp(LONG, s.sessions[0].project, sizeof(s.sessions[0].project) - 1));
}

void test_unknown_state_maps_to_idle() {
    const char *j = "{\"host\":\"h\",\"ts\":1,\"sessions\":[{\"id\":\"x\",\"state\":\"zzz\",\"since\":1}]}";
    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(j, strlen(j), s));
    TEST_ASSERT_EQUAL((int)State::Idle, (int)s.sessions[0].state);
}

void test_wrong_types_are_ignored() {
    // Valeurs de mauvais type : chaines vides / valeurs par defaut, sans planter.
    const char *j =
        "{\"host\":\"h\",\"ts\":\"x\",\"limits\":{\"h5\":\"a\",\"d7\":[1]},"
        "\"sessions\":[{\"id\":\"i\",\"project\":123,\"model\":null,\"state\":5,"
        "\"since\":\"z\",\"ctx\":{},\"tool\":true}]}";
    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(j, strlen(j), s));
    TEST_ASSERT_EQUAL(0, (long)s.ts);
    TEST_ASSERT_EQUAL(-1, s.limits.h5);
    TEST_ASSERT_EQUAL(-1, s.limits.d7);
    TEST_ASSERT_EQUAL(1, s.count);
    TEST_ASSERT_EQUAL_STRING("i", s.sessions[0].id);
    TEST_ASSERT_EQUAL_STRING("", s.sessions[0].project);
    TEST_ASSERT_EQUAL_STRING("", s.sessions[0].model);
    TEST_ASSERT_EQUAL_STRING("", s.sessions[0].tool);
    TEST_ASSERT_EQUAL((int)State::Idle, (int)s.sessions[0].state);
    TEST_ASSERT_EQUAL(0, (long)s.sessions[0].since);
    TEST_ASSERT_EQUAL(-1, s.sessions[0].ctx);
}

void test_host_not_string_or_sessions_not_array() {
    const char *j1 = "{\"host\":42,\"ts\":1,\"sessions\":[]}";
    HostSnapshot s;
    TEST_ASSERT_FALSE(parseSnapshot(j1, strlen(j1), s));
    const char *j2 = "{\"host\":\"h\",\"ts\":1,\"sessions\":{\"id\":\"x\"}}";
    TEST_ASSERT_TRUE(parseSnapshot(j2, strlen(j2), s));
    TEST_ASSERT_EQUAL(0, s.count);
}

void test_percentages_clamped() {
    const char *j =
        "{\"host\":\"h\",\"ts\":1,\"limits\":{\"h5\":250,\"d7\":-40},"
        "\"sessions\":[{\"id\":\"x\",\"state\":\"idle\",\"since\":1,\"ctx\":999}]}";
    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(j, strlen(j), s));
    TEST_ASSERT_EQUAL(100, s.limits.h5);
    TEST_ASSERT_EQUAL(-1, s.limits.d7);
    TEST_ASSERT_EQUAL(100, s.sessions[0].ctx);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_full);
    RUN_TEST(test_parse_no_limits);
    RUN_TEST(test_parse_invalid_json);
    RUN_TEST(test_parse_missing_host);
    RUN_TEST(test_parse_failure_leaves_output_untouched);
    RUN_TEST(test_host_of_32_chars_kept_intact);
    RUN_TEST(test_invalid_session_entries_skipped);
    RUN_TEST(test_reparse_resets_previous_content);
    RUN_TEST(test_parse_truncates_long_strings_and_caps_sessions);
    RUN_TEST(test_unknown_state_maps_to_idle);
    RUN_TEST(test_wrong_types_are_ignored);
    RUN_TEST(test_host_not_string_or_sessions_not_array);
    RUN_TEST(test_percentages_clamped);
    return UNITY_END();
}
