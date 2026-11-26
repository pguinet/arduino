import os
from pathlib import Path

import pytest

from claude_dash.instance_lock import acquire, default_lock_path, holder_pid


def test_second_acquire_fails_and_reports_pid(tmp_path: Path) -> None:
    path = tmp_path / "desktop.lock"
    first = acquire(path)
    assert first is not None
    assert acquire(path) is None
    assert holder_pid(path) == os.getpid()
    first.close()
    again = acquire(path)
    assert again is not None
    again.close()


def test_acquire_creates_parent_directory(tmp_path: Path) -> None:
    lock = acquire(tmp_path / "sub" / "desktop.lock")
    assert lock is not None
    lock.close()


def test_holder_pid_unknown(tmp_path: Path) -> None:
    assert holder_pid(tmp_path / "absent.lock") is None
    garbage = tmp_path / "garbage.lock"
    garbage.write_text("not a pid\n")
    assert holder_pid(garbage) is None


def test_default_lock_path_prefers_runtime_dir(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("XDG_RUNTIME_DIR", str(tmp_path))
    assert default_lock_path() == tmp_path / "claude-dash-desktop.lock"
    monkeypatch.delenv("XDG_RUNTIME_DIR")
    assert default_lock_path() == Path.home() / ".config" / "claude-dash" / "desktop.lock"
