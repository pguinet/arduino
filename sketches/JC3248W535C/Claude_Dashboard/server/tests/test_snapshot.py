import json
from typing import Any

from claude_dash.snapshot import build_snapshot

NOW = 1_000_000


def s(**kw: Any) -> dict[str, Any]:
    base = {
        "id": "abcdef0123456789",
        "state": "idle",
        "since": NOW - 10,
        "updated": NOW - 5,
        "pid": 10,
        "project": "arduino",
        "model": "Opus 5.5",
    }
    base.update(kw)
    return base


def test_basic_snapshot() -> None:
    snap, dead = build_snapshot(
        "srv", [s(ctx=37, tool="Bash")], {"h5": 42, "updated": 1}, NOW, pid_alive=lambda p: True
    )
    assert dead == []
    assert snap == {
        "host": "srv",
        "ts": NOW,
        "limits": {"h5": 42},
        "sessions": [
            {
                "id": "abcdef01",
                "project": "arduino",
                "model": "Opus 5.5",
                "state": "idle",
                "since": NOW - 10,
                "ctx": 37,
                "tool": "Bash",
            }
        ],
    }


def test_dead_pid_excluded() -> None:
    snap, dead = build_snapshot(
        "srv", [s(id="a1", pid=10), s(id="b2", pid=11)], {}, NOW, pid_alive=lambda p: p == 11
    )
    assert [x["id"] for x in snap["sessions"]] == ["b2"]
    assert dead == ["a1"]
    assert "limits" not in snap


def test_session_without_pid_expires_after_12h() -> None:
    fresh = s(id="f", updated=NOW - 3600)
    old = s(id="o", updated=NOW - 13 * 3600)
    del fresh["pid"], old["pid"]
    snap, dead = build_snapshot("srv", [fresh, old], {}, NOW, pid_alive=lambda p: True)
    assert [x["id"] for x in snap["sessions"]] == ["f"]
    assert dead == ["o"]


def test_truncation_and_invalid_state() -> None:
    snap, _ = build_snapshot(
        "srv", [s(project="p" * 50, state="weird")], {}, NOW, pid_alive=lambda p: True
    )
    sess = snap["sessions"][0]
    assert len(sess["project"]) == 32
    assert sess["state"] == "idle"


def test_max_12_most_recent() -> None:
    sessions = [s(id=f"s{i:02d}", updated=NOW - i) for i in range(20)]
    snap, dead = build_snapshot("srv", sessions, {}, NOW, pid_alive=lambda p: True)
    assert len(snap["sessions"]) == 12
    assert snap["sessions"][0]["id"] == "s00"
    assert dead == []


def test_garbage_values_do_not_crash() -> None:
    bad = s(id="g", since="hier", updated=None, ctx=True, tool="")
    snap, dead = build_snapshot("srv", [bad, {"state": "idle"}], {}, NOW, pid_alive=lambda p: True)
    assert dead == []
    assert snap["sessions"] == [
        {"id": "g", "project": "arduino", "model": "Opus 5.5", "state": "idle", "since": 0}
    ]


def test_garbage_updated_without_pid_is_dead() -> None:
    bad = s(id="x", updated="n/a")
    del bad["pid"]
    snap, dead = build_snapshot("srv", [bad], {}, NOW, pid_alive=lambda p: True)
    assert snap["sessions"] == []
    assert dead == ["x"]


PUBLIC_KEYS = {"id", "project", "model", "state", "since", "ctx", "tool"}


def test_non_finite_floats_do_not_crash() -> None:
    bad = s(id="n", since=float("nan"), updated=float("inf"), ctx=float("nan"))
    del bad["pid"]
    snap, dead = build_snapshot("srv", [bad], {}, NOW, pid_alive=lambda p: True)
    assert snap["sessions"] == []
    assert dead == ["n"]
    ok = s(id="m", since=float("-inf"), updated=float("nan"))
    snap, _ = build_snapshot("srv", [ok], {}, NOW, pid_alive=lambda p: True)
    assert snap["sessions"][0]["since"] == 0


def test_out_of_range_pid_uses_no_pid_branch() -> None:
    calls: list[int] = []

    def alive(p: int) -> bool:
        calls.append(p)
        return True

    huge = s(id="h", pid=2**70, updated=NOW - 13 * 3600)
    neg = s(id="n", pid=-1, updated=NOW - 60)
    zero = s(id="z", pid=0, updated=NOW - 13 * 3600)
    snap, dead = build_snapshot("srv", [huge, neg, zero], {}, NOW, pid_alive=alive)
    assert calls == []
    assert [x["id"] for x in snap["sessions"]] == ["n"]
    assert dead == ["h", "z"]


def test_text_folded_to_ascii() -> None:
    snap, _ = build_snapshot(
        "srv",
        [s(project="Été 🎉", model="Opus\x07 5.5\n", tool="Édit\tfile")],
        {},
        NOW,
        pid_alive=lambda p: True,
    )
    sess = snap["sessions"][0]
    assert sess["project"] == "Ete"
    assert sess["model"] == "Opus 5.5"
    assert sess["tool"] == "Editfile"


def test_non_str_text_uses_default() -> None:
    snap, _ = build_snapshot(
        "srv", [s(project=["a"], model=42, tool={"x": 1})], {}, NOW, pid_alive=lambda p: True
    )
    sess = snap["sessions"][0]
    assert sess["project"] == "?"
    assert sess["model"] == ""
    assert "tool" not in sess


def test_emoji_only_project_becomes_placeholder() -> None:
    snap, _ = build_snapshot("srv", [s(project="🎉🎉")], {}, NOW, pid_alive=lambda p: True)
    assert snap["sessions"][0]["project"] == "?"


def test_worst_case_payload_fits_mqtt_buffer() -> None:
    nasty = 'é🎉\x01"\\' * 40
    sessions = [
        s(
            id=f"{i:02d}" * 20,
            project=nasty,
            model=nasty,
            tool=nasty,
            state="permission",
            since=9_999_999_999,
            updated=NOW - i,
            ctx=100,
        )
        for i in range(12)
    ]
    limits = {"h5": 100, "h5_reset": 9_999_999_999, "d7": 100, "d7_reset": 9_999_999_999}
    snap, _ = build_snapshot("h" * 100, sessions, limits, NOW, pid_alive=lambda p: True)
    payload = json.dumps(snap, separators=(",", ":")).encode()
    assert len(snap["sessions"]) == 12
    assert len(snap["host"]) == 32
    assert payload.isascii()
    assert len(payload) < 4096


def test_privacy_only_public_keys() -> None:
    sess = s(
        ctx=10,
        tool="Bash",
        cwd="/home/pascal/secret",
        transcript_path="/home/pascal/.claude/x.jsonl",
        prompt="mot de passe",
        pid=1234,
    )
    snap, _ = build_snapshot("srv", [sess], {}, NOW, pid_alive=lambda p: True)
    assert set(snap) == {"host", "ts", "sessions"}
    assert set(snap["sessions"][0]) == PUBLIC_KEYS
    assert "secret" not in json.dumps(snap)


def test_model_and_tool_truncated_at_24() -> None:
    snap, _ = build_snapshot(
        "srv", [s(model="m" * 40, tool="t" * 40)], {}, NOW, pid_alive=lambda p: True
    )
    sess = snap["sessions"][0]
    assert sess["model"] == "m" * 24
    assert sess["tool"] == "t" * 24


def test_ctx_clamped() -> None:
    snap, _ = build_snapshot(
        "srv", [s(id="a", ctx=150), s(id="b", ctx=-5)], {}, NOW, pid_alive=lambda p: True
    )
    assert sorted(x["ctx"] for x in snap["sessions"]) == [0, 100]


def test_all_limit_keys_published() -> None:
    limits = {
        "h5": 42,
        "h5_reset": 1_700_000_000,
        "d7": 7,
        "d7_reset": 1_700_500_000,
        "updated": 1,
        "other": "x",
    }
    snap, _ = build_snapshot("srv", [], limits, NOW, pid_alive=lambda p: True)
    assert snap["limits"] == {
        "h5": 42,
        "h5_reset": 1_700_000_000,
        "d7": 7,
        "d7_reset": 1_700_500_000,
    }


def test_limits_validated() -> None:
    limits = {"h5": 250, "h5_reset": "demain", "d7": -3, "d7_reset": True}
    snap, _ = build_snapshot("srv", [], limits, NOW, pid_alive=lambda p: True)
    assert snap["limits"] == {"h5": 100, "d7": 0}
    snap, _ = build_snapshot("srv", [], {"h5": 1.5, "d7": None}, NOW, pid_alive=lambda p: True)
    assert "limits" not in snap
    snap, _ = build_snapshot("srv", [], ["h5"], NOW, pid_alive=lambda p: True)
    assert "limits" not in snap


def test_non_dict_sessions_skipped() -> None:
    snap, dead = build_snapshot(
        "srv", ["x", None, 3, s(id="ok")], {}, NOW, pid_alive=lambda p: True
    )
    assert [x["id"] for x in snap["sessions"]] == ["ok"]
    assert dead == []
