import json
import logging
from pathlib import Path
from typing import Any

import pytest

from claude_dash import store
from claude_dash.agent import Publisher, run_once

NOW = 1_000_000


def make() -> tuple[Publisher, list[str]]:
    sent: list[str] = []
    return Publisher(sent.append, min_interval=2, heartbeat=60), sent


def snap(ts: int, state: str = "idle") -> dict[str, object]:
    return {"host": "h", "ts": ts, "sessions": [{"id": "a", "state": state}]}


def test_first_publish_immediate() -> None:
    pub, sent = make()
    assert pub.tick(snap(0), now=0.0)
    assert len(sent) == 1


def test_unchanged_not_republished_before_heartbeat() -> None:
    pub, sent = make()
    pub.tick(snap(0), now=0.0)
    assert not pub.tick(snap(30), now=30.0)  # seul ts a changé
    assert pub.tick(snap(60), now=60.0)  # heartbeat
    assert len(sent) == 2


def test_change_throttled_then_sent() -> None:
    pub, sent = make()
    pub.tick(snap(0), now=0.0)
    assert not pub.tick(snap(1, "working"), now=1.0)  # < 2 s
    assert pub.tick(snap(2, "working"), now=2.0)
    assert '"working"' in sent[-1]


def test_failed_publish_is_retried() -> None:
    calls: list[str] = []

    def flaky(payload: str) -> None:
        calls.append(payload)
        if len(calls) == 1:
            raise OSError("down")

    pub = Publisher(flaky, min_interval=2, heartbeat=60)
    assert not pub.tick(snap(0), now=0.0)
    assert pub.tick(snap(3), now=3.0)


def test_repeated_failures_warn_once(caplog: pytest.LogCaptureFixture) -> None:
    ok = False

    def send(payload: str) -> None:
        if not ok:
            raise OSError("down")

    pub = Publisher(send)
    with caplog.at_level(logging.INFO):
        for t in range(5):
            pub.tick(snap(t), now=float(t))
        ok = True
        assert pub.tick(snap(5), now=5.0)
    assert caplog.text.count("publication échouée") == 1
    assert "publication rétablie" in caplog.text


def test_payload_is_compact_json() -> None:
    pub, sent = make()
    pub.tick(snap(5), now=0.0)
    assert " " not in sent[0]
    assert json.loads(sent[0]) == snap(5)


# --- run_once : une itération de la boucle principale -----------------------


def add_session(base: Path, sid: str, **fields: Any) -> None:
    data = {"state": "idle", "since": NOW - 10, "updated": NOW - 5, "project": "p"}
    data.update(fields)
    store.update_session(base, sid, lambda d: d.update(data))


def test_run_once_publishes_snapshot(tmp_path: Path) -> None:
    add_session(tmp_path, "s1", pid=10, state="working")
    pub, sent = make()
    run_once(tmp_path, "srv", pub, NOW, 0.0, pid_alive=lambda p: True)
    payload = json.loads(sent[0])
    assert payload["host"] == "srv"
    assert payload["ts"] == NOW
    assert [(x["id"], x["state"]) for x in payload["sessions"]] == [("s1", "working")]


def test_run_once_purges_dead_sessions(tmp_path: Path) -> None:
    add_session(tmp_path, "dead", pid=10)
    add_session(tmp_path, "alive", pid=11)
    pub, sent = make()
    run_once(tmp_path, "srv", pub, NOW, 0.0, pid_alive=lambda p: p == 11)
    assert not (tmp_path / "sessions" / "dead.json").exists()
    assert (tmp_path / "sessions" / "alive.json").exists()
    assert [x["id"] for x in json.loads(sent[0])["sessions"]] == ["alive"]


def test_run_once_survives_malformed_dead_id(tmp_path: Path) -> None:
    # fichier édité à la main : id interne invalide, session expirée sans pid
    (tmp_path / "sessions").mkdir()
    (tmp_path / "sessions" / "bad.json").write_text(
        json.dumps({"id": "../evil", "updated": NOW - 13 * 3600})
    )
    add_session(tmp_path, "dead", pid=10)
    add_session(tmp_path, "ok", pid=11)
    pub, sent = make()
    run_once(tmp_path, "srv", pub, NOW, 0.0, pid_alive=lambda p: p == 11)
    assert not (tmp_path / "sessions" / "dead.json").exists()  # purge continuée
    assert [x["id"] for x in json.loads(sent[0])["sessions"]] == ["ok"]


def test_run_once_survives_build_error(tmp_path: Path, caplog: pytest.LogCaptureFixture) -> None:
    add_session(tmp_path, "s1", pid=10)

    def boom(pid: int) -> bool:
        raise RuntimeError("boom")

    pub, sent = make()
    with caplog.at_level(logging.ERROR):
        run_once(tmp_path, "srv", pub, NOW, 0.0, pid_alive=boom)
    assert sent == []
    assert "boom" in caplog.text


def test_run_once_survives_send_error(tmp_path: Path) -> None:
    add_session(tmp_path, "s1", pid=10)

    def down(payload: str) -> None:
        raise OSError("down")

    run_once(tmp_path, "srv", Publisher(down), NOW, 0.0, pid_alive=lambda p: True)


def test_run_once_survives_tick_error(tmp_path: Path) -> None:
    class Broken:
        def tick(self, snapshot: dict[str, Any], now: float) -> bool:
            raise ValueError("tick")

    run_once(tmp_path, "srv", Broken(), NOW, 0.0, pid_alive=lambda p: True)


def test_run_once_on_missing_base(tmp_path: Path) -> None:
    pub, sent = make()
    run_once(tmp_path / "absent", "srv", pub, NOW, 0.0, pid_alive=lambda p: True)
    assert json.loads(sent[0])["sessions"] == []
