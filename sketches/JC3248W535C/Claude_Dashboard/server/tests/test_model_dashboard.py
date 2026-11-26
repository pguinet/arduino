"""Multi-host dashboard model, ported from the firmware's native tests
(test/test_dashboard) without the 4-host cap cases."""

from dataclasses import replace

from claude_dash.model import (
    FORGET_AFTER_S,
    STALE_AFTER_S,
    Alert,
    Dashboard,
    HostSnapshot,
    Limits,
    Session,
    State,
)

W, ID, P, Q = State.WORKING, State.IDLE, State.PERMISSION, State.QUESTION


def sess(sid: str, state: State, since: int = 0, **fields: object) -> Session:
    base = Session(id=sid, project="", model="", tool="", state=state, since=since, ctx=-1)
    return replace(base, **fields)  # type: ignore[arg-type]


def snap(
    host: str, sessions: list[Session] | None = None, ts: int = 0, limits: Limits | None = None
) -> HostSnapshot:
    return HostSnapshot(host=host, ts=ts, limits=limits or Limits(), sessions=tuple(sessions or []))


def mk(host: str, ids: list[str], states: list[State], since: list[int]) -> HostSnapshot:
    return snap(host, [sess(i, s, t) for i, s, t in zip(ids, states, since, strict=True)])


def alert(d: Dashboard, s: HostSnapshot, now: int) -> Alert:
    return d.apply(s, now).alert


def ids(d: Dashboard) -> list[str]:
    return [r.session.id for r in d.rows()]


# --- Alerts ---


def test_no_alert_on_first_snapshot() -> None:
    assert alert(Dashboard(), mk("h", ["a"], [P], [10]), 100) is Alert.NONE


def test_alert_on_transition_to_permission() -> None:
    d = Dashboard()
    d.apply(mk("h", ["a"], [W], [10]), 100)
    assert alert(d, mk("h", ["a"], [P], [10]), 102) is Alert.PERMISSION
    assert alert(d, mk("h", ["a"], [P], [10]), 104) is Alert.NONE


def test_alert_idle_after_working() -> None:
    d = Dashboard()
    d.apply(mk("h", ["a"], [W], [10]), 100)
    assert alert(d, mk("h", ["a"], [ID], [10]), 102) is Alert.IDLE


def test_new_session_no_alert() -> None:
    d = Dashboard()
    d.apply(mk("h", ["a"], [W], [1]), 100)
    assert alert(d, mk("h", ["a", "b"], [W, ID], [1, 2]), 102) is Alert.NONE


def test_permission_wins_over_idle() -> None:
    d = Dashboard()
    d.apply(mk("h", ["a", "b"], [W, W], [1, 1]), 100)
    assert alert(d, mk("h", ["a", "b"], [ID, P], [1, 1]), 102) is Alert.PERMISSION


def test_alert_on_transition_to_question() -> None:
    d = Dashboard()
    d.apply(mk("h", ["a"], [W], [10]), 100)
    assert alert(d, mk("h", ["a"], [Q], [10]), 102) is Alert.QUESTION


def test_alert_priority_permission_question_idle() -> None:
    d = Dashboard()
    abc = ["a", "b", "c"]
    d.apply(mk("h", abc, [W, W, W], [1, 1, 1]), 100)
    assert alert(d, mk("h", abc, [ID, Q, W], [1, 1, 1]), 102) is Alert.QUESTION
    d.apply(mk("h", abc, [W, W, W], [1, 1, 1]), 104)
    assert alert(d, mk("h", abc, [Q, P, ID], [1, 1, 1]), 106) is Alert.PERMISSION


def test_no_alert_when_previous_snapshot_was_stale() -> None:
    d = Dashboard()
    d.apply(replace(mk("h", ["a"], [W], [1]), ts=100), 100)
    later = 100 + STALE_AFTER_S + 1
    assert alert(d, replace(mk("h", ["a"], [P], [1]), ts=later), later) is Alert.NONE
    fresh = 100 + STALE_AFTER_S + 3
    assert alert(d, replace(mk("h", ["a"], [ID], [1]), ts=fresh), fresh) is Alert.IDLE


def test_alert_when_previous_snapshot_at_stale_limit() -> None:
    d = Dashboard()
    d.apply(replace(mk("h", ["a"], [W], [1]), ts=100), 100)
    at = 100 + STALE_AFTER_S
    assert alert(d, replace(mk("h", ["a"], [P], [1]), ts=at), at) is Alert.PERMISSION


def test_alert_idle_after_permission() -> None:
    d = Dashboard()
    d.apply(mk("h", ["a"], [P], [10]), 100)
    assert alert(d, mk("h", ["a"], [ID], [10]), 102) is Alert.IDLE


def test_session_gone_then_back_no_alert() -> None:
    d = Dashboard()
    d.apply(mk("h", ["a"], [W], [1]), 100)
    d.apply(snap("h"), 102)
    assert alert(d, mk("h", ["a"], [P], [1]), 104) is Alert.NONE


def test_alerted_lists_every_session_that_changed_to_an_alert_state() -> None:
    d = Dashboard()
    d.apply(snap("h", [sess("a", W), sess("b", W), sess("c", W)]), 100)
    r = d.apply(snap("h", [sess("a", P), sess("b", ID), sess("c", W)]), 101)
    assert r.alert is Alert.PERMISSION
    assert r.alerted == frozenset({("h", "a"), ("h", "b")})


def test_alerted_ignores_transition_to_working() -> None:
    d = Dashboard()
    d.apply(snap("h", [sess("a", P)]), 100)
    r = d.apply(snap("h", [sess("a", W)]), 101)
    assert (r.alert, r.alerted) == (Alert.NONE, frozenset())


def test_alerted_empty_when_previous_snapshot_was_stale() -> None:
    d = Dashboard()
    d.apply(snap("h", [sess("a", W)], ts=100), 100)
    r = d.apply(snap("h", [sess("a", P)], ts=300), 300)
    assert r.alert is Alert.NONE and r.alerted == frozenset()


# --- Rows ---


def test_rows_sorted_by_urgency_then_age() -> None:
    d = Dashboard()
    d.apply(mk("h", ["w", "i_new", "p", "i_old"], [W, ID, P, ID], [50, 90, 80, 20]), 100)
    assert ids(d) == ["p", "i_old", "i_new", "w"]
    assert d.rows()[0].host == "h"


def test_rows_question_between_permission_and_idle() -> None:
    d = Dashboard()
    d.apply(mk("h", ["i", "q", "w", "p"], [ID, Q, W, P], [10, 90, 10, 90]), 100)
    assert ids(d) == ["p", "q", "i", "w"]


def test_rows_sort_is_stable() -> None:
    d = Dashboard()
    d.apply(mk("h", ["x1", "w", "x2", "x3"], [ID, W, ID, ID], [5, 1, 5, 5]), 100)
    assert ids(d) == ["x1", "x2", "x3", "w"]


def test_rows_unknown_since_first_in_group() -> None:
    d = Dashboard()
    d.apply(mk("h", ["late", "unknown"], [ID, ID], [50, 0]), 100)
    assert ids(d) == ["unknown", "late"]


def test_multi_host_and_latest_limits() -> None:
    d = Dashboard()
    d.apply(replace(mk("h1", ["a"], [ID], [1]), ts=100, limits=Limits(h5=10)), 100)
    d.apply(replace(mk("h2", ["a"], [ID], [1]), ts=200, limits=Limits(h5=20)), 200)
    assert d.host_names() == ["h1", "h2"]
    assert d.limits(200).h5 == 20
    assert len(d.rows()) == 2


def test_many_hosts_are_all_kept() -> None:
    d = Dashboard()
    for i in range(6):
        d.apply(snap(f"h{i}", [sess("a", ID)]), 100)
    assert d.host_names() == [f"h{i}" for i in range(6)]


# --- Staleness and counts ---


def test_limits_null_when_never_received() -> None:
    assert Dashboard().limits(100) == Limits()


def test_stale_seconds() -> None:
    d = Dashboard()
    assert d.stale_seconds(1000) == 0
    d.apply(snap("h"), 1000)
    assert d.stale_seconds(1200) == 200


def test_stale_seconds_uses_snapshot_ts() -> None:
    d = Dashboard()
    d.apply(snap("h", ts=100), 1000)
    assert d.stale_seconds(1000) == 900


def test_stale_seconds_future_ts_clamped() -> None:
    d = Dashboard()
    d.apply(snap("h", ts=2000), 1000)
    assert d.stale_seconds(1000) == 0


def test_stale_seconds_max_over_hosts() -> None:
    d = Dashboard()
    d.apply(snap("h1", ts=900), 1000)
    d.apply(snap("h2", ts=500), 1000)
    assert d.stale_seconds(1000) == 500


def test_session_count_empty() -> None:
    assert Dashboard().session_count() == 0


def test_session_count_sums_hosts_any_state() -> None:
    d = Dashboard()
    d.apply(mk("h1", ["a", "b"], [W, P], [1, 2]), 10)
    d.apply(mk("h2", ["a"], [ID], [1]), 10)
    assert d.session_count() == 3


def test_session_count_after_remove_host() -> None:
    d = Dashboard()
    d.apply(mk("h1", ["a", "b"], [W, ID], [1, 2]), 10)
    d.apply(mk("h2", ["a", "b"], [W, ID], [1, 2]), 10)
    assert d.remove_host("h1")
    assert d.session_count() == 2
    assert d.remove_host("h2")
    assert d.session_count() == 0


def test_session_count_zero_when_host_has_no_session() -> None:
    d = Dashboard()
    d.apply(snap("h"), 10)
    assert d.host_names() == ["h"]
    assert d.session_count() == 0


# --- Limits ---


def test_limits_skip_host_without_limits() -> None:
    d = Dashboard()
    d.apply(snap("old", ts=100, limits=Limits(d7=30)), 100)
    d.apply(snap("new", ts=200), 200)
    assert d.limits(200).d7 == 30


def test_limits_freshest_wins_not_latest_heartbeat() -> None:
    d = Dashboard()
    pc = snap("pc", ts=1000, limits=Limits(2, 9000, 0, 90000, 990))
    bureau = snap("bureau", ts=1010, limits=Limits(23, 8000, 55, 80000, 100))
    d.apply(pc, 1000)
    d.apply(bureau, 1010)
    assert d.limits(1010) == Limits(2, 9000, 0, 90000, 0)
    assert not d.apply(replace(pc, ts=1020), 1020).changed
    assert not d.apply(replace(bureau, ts=1030), 1030).changed
    assert d.limits(1030).h5 == 2


def test_limits_skip_expired_window() -> None:
    d = Dashboard()
    d.apply(snap("a", ts=1000, limits=Limits(23, 500, 55, 800, 990)), 1000)
    d.apply(snap("b", ts=1000, limits=Limits(40, 5000, 60, 50000, 100)), 1000)
    lim = d.limits(1000)
    assert (lim.h5, lim.d7) == (40, 60)


def test_limits_expired_without_candidate_is_unknown() -> None:
    d = Dashboard()
    a = snap("a", ts=1000, limits=Limits(23, 1500, 55, 80000, 990))
    d.apply(a, 1000)
    assert d.limits(1499).h5 == 23
    lim = d.limits(1500)
    assert (lim.h5, lim.h5_reset, lim.d7) == (-1, 0, 55)
    d.apply(replace(a, limits=Limits(23, 0, -1, 0, 990)), 2000)
    assert d.limits(99999).h5 == 23


def test_limits_windows_chosen_independently() -> None:
    d = Dashboard()
    d.apply(snap("a", ts=1000, limits=Limits(10, 5000, -1, 0, 900)), 1000)
    d.apply(snap("b", ts=1000, limits=Limits(20, 5000, 30, 50000, 100)), 1000)
    lim = d.limits(1000)
    assert (lim.h5, lim.d7, lim.d7_reset) == (10, 30, 50000)


def test_limits_updated_preferred_over_ts_of_old_agent() -> None:
    d = Dashboard()
    d.apply(snap("old", ts=1000, limits=Limits(h5=50)), 1000)
    new = snap("new", ts=1010, limits=Limits(5, 0, -1, 0, 990))
    d.apply(new, 1010)
    assert d.limits(1010).h5 == 50
    d.apply(replace(new, limits=Limits(5, 0, -1, 0, 1005)), 1020)
    assert d.limits(1020).h5 == 5


# --- Host removal ---


def test_remove_host_compacts() -> None:
    d = Dashboard()
    for h in ("h1", "h2", "h3"):
        d.apply(mk(h, ["a"], [ID], [1]), 100)
    assert d.remove_host("h2")
    assert d.host_names() == ["h1", "h3"]
    assert len(d.rows()) == 2
    assert not d.remove_host("h2")
    assert not d.remove_host("zzz")
    assert d.remove_host("h1") and d.remove_host("h3")
    assert d.host_names() == []
    assert d.stale_seconds(1000) == 0


def test_removed_host_comes_back_without_alert() -> None:
    d = Dashboard()
    d.apply(mk("h", ["a"], [W], [1]), 100)
    assert d.remove_host("h")
    assert alert(d, mk("h", ["a"], [P], [1]), 102) is Alert.NONE


# --- Change detection ---


def one(host: str, state: State, ts: int) -> HostSnapshot:
    return snap(host, [sess("a", state, 10, project="p", ctx=40)], ts=ts, limits=Limits(h5=10))


def changed_after(base: HostSnapshot, mutated: HostSnapshot) -> bool:
    d = Dashboard()
    d.apply(base, 100)
    return d.apply(mutated, 101).changed


def with_session(base: HostSnapshot, **fields: object) -> HostSnapshot:
    return replace(base, sessions=(replace(base.sessions[0], **fields),))  # type: ignore[arg-type]


def test_changed_on_new_host() -> None:
    assert Dashboard().apply(one("h", W, 100), 100).changed


def test_not_changed_when_only_ts_differs() -> None:
    d = Dashboard()
    d.apply(one("h", W, 100), 100)
    assert not d.apply(one("h", W, 120), 120).changed


def test_changed_on_session_state() -> None:
    d = Dashboard()
    d.apply(one("h", W, 100), 100)
    assert d.apply(one("h", ID, 120), 120).changed


def test_changed_on_session_fields() -> None:
    base = one("h", W, 100)
    assert not changed_after(base, base)
    assert changed_after(base, with_session(base, ctx=41))
    assert changed_after(base, with_session(base, tool="Bash"))
    assert changed_after(base, with_session(base, since=11))
    assert changed_after(base, with_session(base, model="Opus"))
    assert changed_after(base, with_session(base, project="autre"))
    assert changed_after(base, with_session(base, id="b"))


def test_changed_on_session_count() -> None:
    base = one("h", W, 100)
    assert changed_after(base, replace(base, sessions=()))


def test_changed_on_limits() -> None:
    base = one("h", W, 100)
    assert changed_after(base, replace(base, limits=Limits(h5=10, d7_reset=5000)))


def test_not_changed_when_only_limits_updated_differs() -> None:
    base = one("h", W, 100)
    assert not changed_after(base, replace(base, limits=Limits(h5=10, updated=99)))


def test_changed_when_displayed_limits_switch_host() -> None:
    d = Dashboard()
    a = replace(one("a", W, 100), limits=Limits(h5=10))
    b = replace(one("b", W, 200), limits=Limits(h5=20))
    d.apply(a, 100)
    d.apply(b, 200)
    assert d.limits(200).h5 == 20
    r = d.apply(replace(a, ts=300), 300)
    assert d.limits(300).h5 == 10
    assert r.changed


# --- Expiry ---


def test_expire_no_host() -> None:
    d = Dashboard()
    assert d.expire(100000) == 0
    assert d.host_names() == []


def test_expire_keeps_fresh_host() -> None:
    d = Dashboard()
    d.apply(snap("h", ts=100000), 100000)
    assert d.expire(100000 + 60) == 0
    assert d.host_names() == ["h"]


def test_expire_keeps_host_at_limit() -> None:
    d = Dashboard()
    d.apply(snap("h", ts=100000), 100000)
    assert d.expire(100000 + FORGET_AFTER_S) == 0


def test_expire_removes_host_past_limit() -> None:
    d = Dashboard()
    d.apply(snap("h", ts=100000), 100000)
    assert d.expire(100000 + FORGET_AFTER_S + 1) == 1
    assert d.host_names() == []
    assert d.session_count() == 0


def test_expire_uses_reception_when_ts_unknown() -> None:
    d = Dashboard()
    d.apply(snap("h"), 100000)
    assert d.expire(100000 + FORGET_AFTER_S) == 0
    assert d.expire(100000 + FORGET_AFTER_S + 1) == 1


def test_expire_multiple_hosts_keeps_order() -> None:
    d = Dashboard()
    now = 100000
    for i, ts in enumerate((now - FORGET_AFTER_S - 5, now, now - FORGET_AFTER_S - 1, now - 30)):
        d.apply(snap(f"h{i}", ts=ts), now)
    assert d.expire(now) == 2
    assert d.host_names() == ["h1", "h3"]
    assert d.expire(now) == 0


def test_expired_host_comes_back_as_new_without_alert() -> None:
    d = Dashboard()
    now = 100000
    d.apply(one("h", W, now), now)
    later = now + FORGET_AFTER_S + 1
    assert d.expire(later) == 1
    r = d.apply(one("h", P, later), later)
    assert r.alert is Alert.NONE
    assert r.changed
    assert d.host_names() == ["h"]
