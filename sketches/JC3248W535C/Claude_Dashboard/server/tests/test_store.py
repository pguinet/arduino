import json
import multiprocessing
from pathlib import Path

import pytest

from claude_dash import store


def test_update_creates_then_merges(tmp_path: Path) -> None:
    store.update_session(tmp_path, "abc-123", lambda d: d.update(state="idle"))
    store.update_session(tmp_path, "abc-123", lambda d: d.update(ctx=12))
    data = json.loads((tmp_path / "sessions" / "abc-123.json").read_text())
    assert data == {"id": "abc-123", "state": "idle", "ctx": 12}


def test_remove_deletes_file(tmp_path: Path) -> None:
    store.update_session(tmp_path, "s1", lambda d: d.update(state="idle"))
    store.remove_session(tmp_path, "s1")
    assert not (tmp_path / "sessions" / "s1.json").exists()


def test_remove_missing_is_silent(tmp_path: Path) -> None:
    store.remove_session(tmp_path, "nope")


@pytest.mark.parametrize("bad", ["../etc/passwd", "a/b", "", "x" * 200, "abc\n"])
def test_rejects_unsafe_ids(tmp_path: Path, bad: str) -> None:
    with pytest.raises(ValueError):
        store.update_session(tmp_path, bad, lambda d: None)


def test_corrupted_file_is_reset(tmp_path: Path) -> None:
    (tmp_path / "sessions").mkdir()
    (tmp_path / "sessions" / "s1.json").write_text("{not json")
    store.update_session(tmp_path, "s1", lambda d: d.update(state="idle"))
    assert store.load_sessions(tmp_path) == [{"id": "s1", "state": "idle"}]


def test_limits_merge_and_load(tmp_path: Path) -> None:
    store.write_limits(tmp_path, {"h5": 10, "updated": 1})
    store.write_limits(tmp_path, {"d7": 3, "updated": 2})
    assert store.load_limits(tmp_path) == {"h5": 10, "d7": 3, "updated": 2}


def test_load_limits_missing(tmp_path: Path) -> None:
    assert store.load_limits(tmp_path) == {}


def _increment_worker(base: str) -> None:
    for _ in range(50):
        store.update_session(Path(base), "shared", lambda d: d.update(n=d.get("n", 0) + 1))


def test_concurrent_updates_are_serialized(tmp_path: Path) -> None:
    ctx = multiprocessing.get_context("spawn")
    procs = [ctx.Process(target=_increment_worker, args=(str(tmp_path),)) for _ in range(4)]
    for p in procs:
        p.start()
    for p in procs:
        p.join(timeout=10)
        assert p.exitcode == 0
    assert store.load_sessions(tmp_path) == [{"id": "shared", "n": 200}]
