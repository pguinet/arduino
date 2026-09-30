import io
import json
from pathlib import Path
from typing import Any

import pytest

from claude_dash import statusline, store
from claude_dash.statusline import extract_limits, parse_epoch, update_from_statusline

SAMPLE: dict[str, Any] = {
    "session_id": "s1",
    "model": {"display_name": "Opus 5.5"},
    "workspace": {"current_dir": "/home/u/dev/arduino"},
    "context_window": {"used_percentage": 37.4},
    "rate_limits": {
        "five_hour": {"used_percentage": 42.2, "resets_at": 1790003600},
        "seven_day": {"used_percentage": 18, "resets_at": "2026-10-01T07:00:00Z"},
    },
}


def test_parse_epoch_variants() -> None:
    assert parse_epoch(1790003600) == 1790003600
    assert parse_epoch(1790003600.9) == 1790003600
    assert parse_epoch("2026-10-01T07:00:00Z") == 1790838000
    assert parse_epoch("2026-10-01T09:00:00+02:00") == 1790838000
    assert parse_epoch("garbage") is None
    assert parse_epoch(None) is None
    assert parse_epoch(True) is None


def test_extract_limits() -> None:
    assert extract_limits(SAMPLE, now=5) == {
        "h5": 42,
        "h5_reset": 1790003600,
        "d7": 18,
        "d7_reset": 1790838000,
        "updated": 5,
    }


def test_extract_limits_absent() -> None:
    assert extract_limits({"session_id": "s1"}, now=5) is None


def test_extract_limits_clamps_percentages() -> None:
    payload = {"rate_limits": {"five_hour": {"used_percentage": 130}}}
    assert extract_limits(payload, now=5) == {"h5": 100, "updated": 5}


def test_update_session_fields_new_session_defaults_idle() -> None:
    d: dict[str, Any] = {}
    update_from_statusline(d, SAMPLE, now=7, pid=9)
    assert d == {
        "model": "Opus 5.5",
        "ctx": 37,
        "project": "arduino",
        "state": "idle",
        "since": 7,
        "pid": 9,
        "updated": 7,
    }


def test_update_keeps_existing_state() -> None:
    d: dict[str, Any] = {"state": "working", "since": 3, "pid": 4}
    update_from_statusline(d, SAMPLE, now=7, pid=None)
    assert d["state"] == "working" and d["since"] == 3 and d["pid"] == 4


def test_project_dir_takes_priority() -> None:
    payload = {
        "session_id": "s1",
        "workspace": {
            "project_dir": "/home/u/dev/arduino",
            "current_dir": "/home/u/dev/arduino/src",
        },
    }
    d: dict[str, Any] = {"project": "old"}
    update_from_statusline(d, payload, now=7, pid=None)
    assert d["project"] == "arduino"


def test_current_dir_does_not_overwrite_existing_project() -> None:
    payload = {"session_id": "s1", "workspace": {"current_dir": "/home/u/dev/arduino/src"}}
    d: dict[str, Any] = {"project": "arduino"}
    update_from_statusline(d, payload, now=7, pid=None)
    assert d["project"] == "arduino"


def test_cwd_fallback_and_empty_basename_ignored() -> None:
    d: dict[str, Any] = {}
    update_from_statusline(d, {"session_id": "s1", "cwd": "/home/u/dev/arduino"}, now=7, pid=None)
    assert d["project"] == "arduino"

    d2: dict[str, Any] = {}
    update_from_statusline(
        d2, {"session_id": "s1", "workspace": {"project_dir": "/"}}, now=7, pid=None
    )
    assert "project" not in d2


def test_main_writes_store_silently(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setenv("CLAUDE_DASH_DIR", str(tmp_path))
    monkeypatch.setattr("sys.stdin", io.StringIO(json.dumps(SAMPLE)))
    with pytest.raises(SystemExit) as exc:
        statusline.main()
    assert exc.value.code == 0
    assert capsys.readouterr().out == ""
    assert store.load_limits(tmp_path)["h5"] == 42
    [session] = store.load_sessions(tmp_path)
    assert session["id"] == "s1" and session["model"] == "Opus 5.5"


def test_main_bad_input_exits_zero(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setenv("CLAUDE_DASH_DIR", str(tmp_path))
    monkeypatch.setattr("sys.stdin", io.StringIO("not json"))
    with pytest.raises(SystemExit) as exc:
        statusline.main()
    assert exc.value.code == 0
    assert capsys.readouterr().out == ""
