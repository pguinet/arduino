import os
from pathlib import Path

import pytest

from claude_dash import desktop
from claude_dash.desktop import main
from claude_dash.instance_lock import acquire


def write_valid_config(tmp_path: Path) -> Path:
    p = tmp_path / "desktop.toml"
    p.write_text('[mqtt]\nhost="127.0.0.1"\nport=1\ntls=false\n')
    p.chmod(0o600)
    return p


def test_main_exits_2_on_bad_config(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    with pytest.raises(SystemExit) as exc:
        main(["--config", str(tmp_path / "absent.toml")])
    assert exc.value.code == 2
    assert "absent.toml" in capsys.readouterr().err


def test_main_exits_1_when_already_running(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    config = write_valid_config(tmp_path)
    lock = tmp_path / "desktop.lock"
    monkeypatch.setattr(desktop, "default_lock_path", lambda: lock)
    held = acquire(lock)
    assert held is not None
    try:
        with pytest.raises(SystemExit) as exc:
            main(["--config", str(config)])
    finally:
        held.close()
    assert exc.value.code == 1
    assert f"déjà lancé (PID {os.getpid()})" in capsys.readouterr().err
