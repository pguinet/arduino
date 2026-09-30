import io
import json
from pathlib import Path
from typing import Any

import pytest

from claude_dash import hook
from claude_dash.hook import apply_event


def ev(name: str, **kw: Any) -> dict[str, Any]:
    return {"hook_event_name": name, "session_id": "s1", "cwd": "/home/u/dev/arduino", **kw}


def test_session_start_is_idle_with_project() -> None:
    d: dict[str, Any] = {}
    assert apply_event(d, ev("SessionStart"), now=100, pid=42)
    assert d == {"state": "idle", "since": 100, "project": "arduino", "pid": 42, "updated": 100}


def test_prompt_then_tool_is_working_with_tool() -> None:
    d: dict[str, Any] = {}
    apply_event(d, ev("UserPromptSubmit"), now=100, pid=1)
    apply_event(d, ev("PreToolUse", tool_name="Bash"), now=105, pid=1)
    assert d["state"] == "working"
    assert d["tool"] == "Bash"
    assert d["since"] == 100  # l'état n'a pas changé : since conservé


def test_permission_prompt() -> None:
    d: dict[str, Any] = {"state": "working", "since": 100}
    apply_event(
        d,
        ev(
            "Notification",
            notification_type="permission_prompt",
            message="Claude needs your permission to use Bash",
        ),
        now=110,
        pid=1,
    )
    assert d["state"] == "permission"
    assert d["since"] == 110


def test_permission_fallback_on_message_without_type() -> None:
    d: dict[str, Any] = {"state": "working", "since": 100}
    apply_event(
        d, ev("Notification", message="Claude needs your permission to use Bash"), now=110, pid=1
    )
    assert d["state"] == "permission"


def test_idle_notification_does_not_change_state() -> None:
    d: dict[str, Any] = {"state": "idle", "since": 100}
    apply_event(
        d,
        ev(
            "Notification",
            notification_type="idle_prompt",
            message="Claude is waiting for your input",
        ),
        now=200,
        pid=1,
    )
    assert d["state"] == "idle"
    assert d["since"] == 100


def test_idle_prompt_after_permission_is_idle() -> None:
    # Esc sur une demande de permission : aucun Stop, seul idle_prompt arrive
    d: dict[str, Any] = {"state": "permission", "since": 110, "tool": "Bash"}
    apply_event(d, ev("Notification", notification_type="idle_prompt"), now=200, pid=1)
    assert d["state"] == "idle"
    assert d["since"] == 200
    assert "tool" not in d


def test_session_start_compact_keeps_state() -> None:
    d: dict[str, Any] = {"state": "working", "since": 100, "tool": "Bash"}
    apply_event(d, ev("SessionStart", source="compact"), now=150, pid=1)
    assert d["state"] == "working"
    assert d["since"] == 100


def test_session_start_compact_without_state_is_idle() -> None:
    d: dict[str, Any] = {}
    apply_event(d, ev("SessionStart", source="compact"), now=150, pid=1)
    assert d["state"] == "idle"


def test_session_start_resume_resets_to_idle() -> None:
    d: dict[str, Any] = {"state": "working", "since": 100}
    apply_event(d, ev("SessionStart", source="resume"), now=150, pid=1)
    assert d["state"] == "idle"


def test_project_dir_takes_precedence_over_cwd() -> None:
    d: dict[str, Any] = {"project": "old"}
    apply_event(d, ev("PreToolUse"), now=1, pid=1, project_dir="/home/u/dev/dashboard")
    assert d["project"] == "dashboard"


def test_project_from_cwd_only_when_unset() -> None:
    d: dict[str, Any] = {}
    apply_event(d, ev("SessionStart"), now=1, pid=1)
    apply_event(d, {**ev("PreToolUse"), "cwd": "/tmp/elsewhere"}, now=2, pid=1)
    assert d["project"] == "arduino"


def test_empty_basename_ignored() -> None:
    d: dict[str, Any] = {}
    apply_event(d, {**ev("SessionStart"), "cwd": "/"}, now=1, pid=1, project_dir="/")
    assert "project" not in d


def test_post_tool_use_after_permission_is_working() -> None:
    d: dict[str, Any] = {"state": "permission", "since": 110, "tool": "Bash"}
    apply_event(d, ev("PostToolUse", tool_name="Bash"), now=120, pid=1)
    assert d["state"] == "working"


def test_stop_is_idle_and_clears_tool() -> None:
    d: dict[str, Any] = {"state": "working", "since": 100, "tool": "Edit"}
    apply_event(d, ev("Stop"), now=130, pid=1)
    assert d["state"] == "idle"
    assert "tool" not in d


def test_session_end_requests_removal() -> None:
    assert apply_event({}, ev("SessionEnd", reason="exit"), now=1, pid=1) is False


def test_unknown_event_keeps_session() -> None:
    d: dict[str, Any] = {"state": "idle", "since": 1}
    assert apply_event(d, ev("SubagentStop"), now=5, pid=1)
    assert d["state"] == "idle"


def test_pid_not_overwritten_by_none() -> None:
    d: dict[str, Any] = {"pid": 42}
    apply_event(d, ev("Stop"), now=1, pid=None)
    assert d["pid"] == 42


def _run_main(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path, stdin: str, project_dir: str | None = None
) -> int | str | None:
    monkeypatch.setenv("CLAUDE_DASH_DIR", str(tmp_path))
    if project_dir is None:
        monkeypatch.delenv("CLAUDE_PROJECT_DIR", raising=False)
    else:
        monkeypatch.setenv("CLAUDE_PROJECT_DIR", project_dir)
    monkeypatch.setattr("sys.stdin", io.StringIO(stdin))
    with pytest.raises(SystemExit) as exc:
        hook.main()
    return exc.value.code


def test_main_session_start_creates_file(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    code = _run_main(monkeypatch, tmp_path, json.dumps(ev("SessionStart")))
    assert code == 0
    data = json.loads((tmp_path / "sessions" / "s1.json").read_text())
    assert data["state"] == "idle"
    assert data["project"] == "arduino"


def test_main_uses_claude_project_dir(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    _run_main(monkeypatch, tmp_path, json.dumps(ev("SessionStart")), "/home/u/dev/dashboard")
    data = json.loads((tmp_path / "sessions" / "s1.json").read_text())
    assert data["project"] == "dashboard"


def test_main_session_end_removes_file(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    _run_main(monkeypatch, tmp_path, json.dumps(ev("SessionStart")))
    code = _run_main(monkeypatch, tmp_path, json.dumps(ev("SessionEnd")))
    assert code == 0
    assert not (tmp_path / "sessions" / "s1.json").exists()


def test_main_invalid_json_exits_zero(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    assert _run_main(monkeypatch, tmp_path, "{not json") == 0
