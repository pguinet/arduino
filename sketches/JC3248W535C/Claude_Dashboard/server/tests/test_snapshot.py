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
