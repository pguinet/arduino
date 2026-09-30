import json
import logging
import threading
from pathlib import Path
from typing import Any

import pytest

from claude_dash import store
from claude_dash.agent import Outage, Publisher, main, on_connect_callback, run_once

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


def test_run_once_purges_by_filename_not_inner_id(tmp_path: Path) -> None:
    # fichier édité à la main : x.json annonce l'id de y, seul x.json doit partir
    folder = tmp_path / "sessions"
    folder.mkdir()
    (folder / "x.json").write_text(json.dumps({"id": "y", "pid": 10, "updated": NOW}))
    (folder / "y.json").write_text(json.dumps({"id": "y", "pid": 11, "updated": NOW}))
    (folder / "z.json").write_text(json.dumps({"id": "../evil", "updated": NOW - 13 * 3600}))
    pub, sent = make()
    run_once(tmp_path, "srv", pub, NOW, 0.0, pid_alive=lambda p: p == 11)
    assert not (folder / "x.json").exists()
    assert not (folder / "z.json").exists()
    assert (folder / "y.json").exists()
    assert [x["id"] for x in json.loads(sent[0])["sessions"]] == ["y"]


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


def test_run_once_persistent_error_logged_once(
    tmp_path: Path, caplog: pytest.LogCaptureFixture
) -> None:
    failing = True

    def alive(pid: int) -> bool:
        if failing:
            raise RuntimeError("boom")
        return True

    add_session(tmp_path, "s1", pid=10)
    pub, sent = make()
    outage = Outage("agent rétabli")
    with caplog.at_level(logging.DEBUG, logger="claude-dash-agent"):
        for t in range(3):
            run_once(tmp_path, "srv", pub, NOW, float(t), pid_alive=alive, outage=outage)
        errors = [r for r in caplog.records if r.levelno >= logging.ERROR]
        assert len(errors) == 1 and errors[0].exc_info  # traceback complète une fois
        assert sum(r.levelno == logging.DEBUG for r in caplog.records) == 2
        failing = False
        run_once(tmp_path, "srv", pub, NOW, 3.0, pid_alive=alive, outage=outage)
    assert [r.message for r in caplog.records if r.levelno == logging.INFO][-1] == "agent rétabli"
    assert len(sent) == 1


# --- publication forcée après (re)connexion ---------------------------------


def test_force_bypasses_throttle_and_dedup() -> None:
    pub, sent = make()
    pub.tick(snap(0), now=0.0)
    pub.force()
    assert pub.tick(snap(1), now=0.5)  # inchangé et < min_interval
    assert not pub.tick(snap(2), now=0.6)  # force consommée
    assert len(sent) == 2


def test_force_survives_failed_send() -> None:
    fail = [True]
    sent: list[str] = []

    def send(payload: str) -> None:
        if fail[0]:
            raise OSError("down")
        sent.append(payload)

    pub = Publisher(send, min_interval=2, heartbeat=60)
    fail[0] = False
    pub.tick(snap(0), now=0.0)
    pub.force()
    fail[0] = True
    assert not pub.tick(snap(1), now=0.1)
    fail[0] = False
    assert pub.tick(snap(2), now=0.2)
    assert len(sent) == 2


def _reason(name: str) -> Any:
    from paho.mqtt.packettypes import PacketTypes
    from paho.mqtt.reasoncodes import ReasonCode

    return ReasonCode(PacketTypes.CONNACK, name)


def test_on_connect_success_sets_event(caplog: pytest.LogCaptureFixture) -> None:
    event = threading.Event()
    cb = on_connect_callback(event)
    with caplog.at_level(logging.INFO, logger="claude-dash-agent"):
        cb(None, None, None, _reason("Success"), None)
    assert event.is_set()
    assert "MQTT connecté" in caplog.text


def test_on_connect_refused_logs_error(caplog: pytest.LogCaptureFixture) -> None:
    event = threading.Event()
    cb = on_connect_callback(event)
    with caplog.at_level(logging.INFO, logger="claude-dash-agent"):
        cb(None, None, None, _reason("Not authorized"), None)
    assert not event.is_set()
    assert [r.levelno for r in caplog.records] == [logging.ERROR]
    assert "MQTT refusé" in caplog.text


def test_main_exits_2_on_bad_ca_file(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, caplog: pytest.LogCaptureFixture
) -> None:
    cfg_dir = tmp_path / "claude-dash"
    cfg_dir.mkdir()
    cfg = cfg_dir / "config.toml"
    cfg.write_text('[mqtt]\nhost="127.0.0.1"\nca_certs="/nonexistent/ca.pem"\n')
    cfg.chmod(0o600)
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    with pytest.raises(SystemExit) as exc:
        main()
    assert exc.value.code == 2
    assert "ca.pem" in caplog.text
