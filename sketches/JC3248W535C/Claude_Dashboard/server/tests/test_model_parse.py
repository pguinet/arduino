"""Snapshot parsing and display helpers, ported from the firmware's native tests
(test/test_parse, test/test_helpers) so both implementations stay aligned."""

import json

import pytest

from claude_dash.model import (
    State,
    color_for_percent,
    format_duration,
    parse_snapshot,
    state_color,
    state_label,
)

SAMPLE = (
    '{"host":"srv","ts":1000,'
    '"limits":{"h5":42,"h5_reset":4600,"d7":18,"d7_reset":90000,"updated":990},'
    '"sessions":['
    '{"id":"a1","project":"arduino","model":"Opus 5.5","state":"permission",'
    '"since":950,"ctx":37,"tool":"Bash"},'
    '{"id":"b2","project":"api","model":"Sonnet","state":"idle","since":700}'
    "]}"
)


def test_parse_full() -> None:
    s = parse_snapshot(SAMPLE)
    assert s is not None
    assert (s.host, s.ts) == ("srv", 1000)
    lim = s.limits
    assert (lim.h5, lim.h5_reset, lim.d7, lim.d7_reset, lim.updated) == (42, 4600, 18, 90000, 990)
    assert len(s.sessions) == 2
    a, b = s.sessions
    assert (a.id, a.project, a.model, a.state, a.since, a.ctx, a.tool) == (
        "a1",
        "arduino",
        "Opus 5.5",
        State.PERMISSION,
        950,
        37,
        "Bash",
    )
    assert b.state is State.IDLE
    assert b.ctx == -1
    assert b.tool == ""


def test_parse_accepts_bytes() -> None:
    s = parse_snapshot(SAMPLE.encode())
    assert s is not None and s.host == "srv"


def test_parse_question_state() -> None:
    s = parse_snapshot('{"host":"h","sessions":[{"id":"q","state":"question"}]}')
    assert s is not None and s.sessions[0].state is State.QUESTION


def test_parse_no_limits() -> None:
    s = parse_snapshot('{"host":"h","ts":1,"sessions":[]}')
    assert s is not None
    assert (s.limits.h5, s.limits.d7, s.limits.updated) == (-1, -1, 0)
    assert s.sessions == ()


def test_parse_invalid_json() -> None:
    assert parse_snapshot("{oops") is None


def test_parse_missing_host() -> None:
    assert parse_snapshot('{"ts":1,"sessions":[]}') is None


def test_invalid_session_entries_skipped() -> None:
    s = parse_snapshot(
        '{"host":"h","ts":1,"sessions":[1,"x",null,[],{"state":"idle"},'
        '{"id":"","state":"idle"},{"id":"ok","state":"idle","since":1}]}'
    )
    assert s is not None
    assert [x.id for x in s.sessions] == ["ok"]


def test_parse_keeps_long_strings_and_all_sessions() -> None:
    sessions = [{"id": f"s{i}", "project": "p" * 80} for i in range(20)]
    snap = parse_snapshot(json.dumps({"host": "h", "sessions": sessions}))
    assert snap is not None
    assert len(snap.sessions) == 20
    assert snap.sessions[0].project == "p" * 80


def test_unknown_state_maps_to_idle() -> None:
    s = parse_snapshot('{"host":"h","ts":1,"sessions":[{"id":"x","state":"zzz","since":1}]}')
    assert s is not None and s.sessions[0].state is State.IDLE


def test_wrong_types_are_ignored() -> None:
    s = parse_snapshot(
        '{"host":"h","ts":"x","limits":{"h5":"a","d7":[1]},'
        '"sessions":[{"id":"i","project":123,"model":null,"state":5,'
        '"since":"z","ctx":{},"tool":true}]}'
    )
    assert s is not None
    assert (s.ts, s.limits.h5, s.limits.d7) == (0, -1, -1)
    (x,) = s.sessions
    assert (x.id, x.project, x.model, x.tool, x.state, x.since, x.ctx) == (
        "i",
        "",
        "",
        "",
        State.IDLE,
        0,
        -1,
    )


def test_booleans_are_not_numbers() -> None:
    s = parse_snapshot(
        '{"host":"h","ts":true,"limits":{"h5":true},'
        '"sessions":[{"id":"i","since":true,"ctx":false}]}'
    )
    assert s is not None
    assert (s.ts, s.limits.h5, s.sessions[0].since, s.sessions[0].ctx) == (0, -1, 0, -1)


def test_negative_epochs_become_zero() -> None:
    s = parse_snapshot('{"host":"h","ts":-5,"sessions":[{"id":"i","since":-1}]}')
    assert s is not None
    assert (s.ts, s.sessions[0].since) == (0, 0)


def test_host_not_string_or_sessions_not_array() -> None:
    assert parse_snapshot('{"host":42,"ts":1,"sessions":[]}') is None
    s = parse_snapshot('{"host":"h","ts":1,"sessions":{"id":"x"}}')
    assert s is not None and s.sessions == ()


def test_percentages_clamped() -> None:
    s = parse_snapshot(
        '{"host":"h","ts":1,"limits":{"h5":250,"d7":-40},'
        '"sessions":[{"id":"x","state":"idle","since":1,"ctx":999}]}'
    )
    assert s is not None
    assert (s.limits.h5, s.limits.d7, s.sessions[0].ctx) == (100, -1, 100)


@pytest.mark.parametrize(
    "payload", [b"\xff\xfe", b"[]", b"null", b"42", b'{"host": 3}', b'{"host": ""}']
)
def test_parse_rejects_garbage(payload: bytes) -> None:
    assert parse_snapshot(payload) is None


@pytest.mark.parametrize(
    ("seconds", "text"),
    [
        (0, "0:00"),
        (42, "0:42"),
        (310, "5:10"),
        (3599, "59:59"),
        (3600, "1h00"),
        (3900, "1h05"),
        (86399, "23h59"),
        (86400, "1j"),
        (200000, "2j"),
        (-5, "0:00"),
    ],
)
def test_format_duration(seconds: int, text: str) -> None:
    assert format_duration(seconds) == text


def test_format_duration_huge() -> None:
    assert format_duration(999 * 86400) == "999j"
    assert format_duration(1000 * 86400) == "999j+"
    assert format_duration(2**63 - 1) == "999j+"
    assert format_duration(-(2**63)) == "0:00"


def test_color_thresholds() -> None:
    assert [color_for_percent(p) for p in (0, 59, 60, 84, 85, 100)] == [
        "#4caf50",
        "#4caf50",
        "#fca311",
        "#fca311",
        "#f72585",
        "#f72585",
    ]


def test_state_labels() -> None:
    assert state_label(State.PERMISSION) == "PERMISSION"
    assert state_label(State.QUESTION) == "QUESTION"
    assert state_label(State.IDLE) == "ATTENTE"
    assert state_label(State.WORKING) == "TRAVAILLE"


def test_state_colors() -> None:
    assert state_color(State.PERMISSION) == "#f72585"
    assert state_color(State.QUESTION) == "#b388ff"
    assert state_color(State.IDLE) == "#fca311"
    assert state_color(State.WORKING) == "#4cc9f0"


def test_epochs_beyond_32_bits_become_zero() -> None:
    # A reset in milliseconds would crash datetime formatting in the window.
    s = parse_snapshot(
        '{"host":"h","ts":10000000000000,"limits":{"h5":10,"h5_reset":10000000000000},'
        '"sessions":[{"id":"i","since":100000000000000000000}]}'
    )
    assert s is not None
    assert (s.ts, s.limits.h5_reset, s.sessions[0].since) == (0, 0, 0)
    ok = parse_snapshot('{"host":"h","ts":4294967295}')
    assert ok is not None and ok.ts == 4294967295


def test_lone_surrogates_replaced() -> None:
    # Valid JSON escapes that Tk cannot encode.
    s = parse_snapshot(
        '{"host":"h","sessions":[{"id":"\\udce9","project":"a\\udce9","model":"\\ud800",'
        '"tool":"\\udfff"}]}'
    )
    assert s is not None
    x = s.sessions[0]
    assert (x.id, x.project, x.model, x.tool) == ("�", "a�", "�", "�")
    assert parse_snapshot('{"host":"\\udce9"}') is not None
