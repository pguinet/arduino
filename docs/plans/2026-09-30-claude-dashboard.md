# Claude_Dashboard Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:executing-plans to implement this plan task-by-task.

**Goal:** Tableau de bord Claude Code sur JC3248W535C alimenté par un agent Python tournant sur un serveur Linux distant, via un broker MQTT cloud (TLS).

**Architecture:** Côté serveur, des hooks Claude Code et la statusline écrivent un fichier JSON par session dans `~/.claude/dashboard/` ; un démon (`claude-dash-agent`) agrège ces fichiers, purge les sessions mortes et publie un snapshot retained sur `claude-dash/<host>/state`. Côté ESP32, un modèle pur C++ (`dash_model`, testé en natif) parse les snapshots, trie les sessions et détecte les transitions ; `main.cpp` gère WiFi/MQTT-TLS, LVGL, le bip I2S et la veille.

**Tech Stack:** Python 3.11+ (stdlib + `paho-mqtt` 2.x), pytest/ruff/mypy dans Docker ; PlatformIO pioarduino (Arduino 3.0.7 / IDF 5.1), LVGL 8.4, ArduinoJson 7, PubSubClient 2.8, ESP_I2S, Unity (env `native`).

**Design de référence :** `docs/plans/2026-09-30-claude-dashboard-design.md`

**Écarts assumés par rapport au design :**
- L'agent scrute le dossier toutes les secondes (stdlib) au lieu d'inotify : pas de dépendance, comportement identique à l'échelle humaine.
- `dash_model` vit dans `lib/dash_model/` (et non `src/`) pour que PlatformIO le compile aussi dans l'env `native` sans tirer `main.cpp`.
- Le hook `PostToolUse` est ajouté : il se déclenche APRÈS l'exécution de l'outil et remet la session en `working` une fois l'outil approuvé terminé. Pendant un outil approuvé long, la carte reste sur `permission` : limitation acceptée, aucun hook ne se déclenche au moment de l'approbation.

**Conventions :**
- Chemins relatifs à la racine du repo `/home/pascal/github/arduino`.
- `PIO=~/.platformio/penv/bin/pio` (pio n'est pas dans le PATH).
- `SRV=sketches/JC3248W535C/Claude_Dashboard/server`, `FW=sketches/JC3248W535C/Claude_Dashboard`.
- Code et commentaires firmware sans accents dans les chaînes affichées (Montserrat = ASCII + `°`).
- Ne jamais accéder au port série en bash direct (cf. CLAUDE.md) : `timeout 30 $PIO device monitor`.

---

## Partie A — Agent serveur (Python)

### Task 0: Squelette du paquet Python et chaîne de qualité

**Files:**
- Create: `$SRV/pyproject.toml`
- Create: `$SRV/claude_dash/__init__.py`
- Create: `$SRV/run-checks.sh`
- Create: `$SRV/tests/test_smoke.py`
- Create: `$SRV/.gitignore`

**Step 1: `pyproject.toml`**

```toml
[build-system]
requires = ["setuptools>=68"]
build-backend = "setuptools.build_meta"

[project]
name = "claude-dash"
version = "0.1.0"
description = "Agent serveur du tableau de bord Claude Code (JC3248W535C)"
requires-python = ">=3.11"
dependencies = ["paho-mqtt>=2.0,<3"]

[project.optional-dependencies]
dev = ["pytest>=8", "ruff>=0.6", "mypy>=1.11"]

[project.scripts]
claude-dash-hook = "claude_dash.hook:main"
claude-dash-statusline = "claude_dash.statusline:main"
claude-dash-agent = "claude_dash.agent:main"

[tool.setuptools]
packages = ["claude_dash"]

[tool.ruff]
line-length = 100
target-version = "py311"

[tool.ruff.lint]
select = ["E", "F", "W", "I", "B", "UP", "SIM"]

[tool.mypy]
strict = true
python_version = "3.11"

[[tool.mypy.overrides]]
module = "paho.*"
ignore_missing_imports = true
```

**Step 2: `claude_dash/__init__.py`**

```python
"""Agent serveur du tableau de bord Claude Code."""

__version__ = "0.1.0"
```

**Step 3: `tests/test_smoke.py`**

```python
import claude_dash


def test_version() -> None:
    assert claude_dash.__version__ == "0.1.0"
```

**Step 4: `run-checks.sh`** (tests + lint dans Docker, rien d'installé sur l'hôte)

```bash
#!/usr/bin/env bash
# Lance ruff, mypy et pytest dans un conteneur Python jetable.
set -euo pipefail
cd "$(dirname "$0")"
docker run --rm -v "$PWD":/app -w /app python:3.13-slim sh -c '
  pip install -q --root-user-action=ignore -e ".[dev]" &&
  ruff check . && ruff format --check . && mypy claude_dash && pytest -q "$@"
' -- "$@"
```

`chmod +x run-checks.sh`.

**Step 5: `.gitignore`**

```
__pycache__/
*.egg-info/
.pytest_cache/
.mypy_cache/
.ruff_cache/
```

**Step 6: Lancer**

Run: `$SRV/run-checks.sh`
Expected: `1 passed`, ruff et mypy OK.

**Step 7: Commit**

```bash
git add $SRV
git commit -m "Claude_Dashboard: squelette de l'agent serveur Python"
```

---

### Task 1: Stockage des sessions (`store.py`)

Un fichier JSON par session dans `<base>/sessions/<id>.json` + `<base>/limits.json`. Toutes les écritures passent par un verrou `flock` global (hook et statusline peuvent écrire en même temps) et un `rename` atomique.

**Files:**
- Create: `$SRV/claude_dash/store.py`
- Test: `$SRV/tests/test_store.py`

**Step 1: Tests**

```python
import json
from pathlib import Path

import pytest

from claude_dash import store


def test_update_creates_then_merges(tmp_path: Path) -> None:
    store.update_session(tmp_path, "abc-123", lambda d: d.update(state="idle"))
    store.update_session(tmp_path, "abc-123", lambda d: d.update(ctx=12))
    data = json.loads((tmp_path / "sessions" / "abc-123.json").read_text())
    assert data == {"id": "abc-123", "state": "idle", "ctx": 12}


def test_update_returning_none_removes(tmp_path: Path) -> None:
    store.update_session(tmp_path, "s1", lambda d: d.update(state="idle"))
    store.remove_session(tmp_path, "s1")
    assert not (tmp_path / "sessions" / "s1.json").exists()


def test_remove_missing_is_silent(tmp_path: Path) -> None:
    store.remove_session(tmp_path, "nope")


@pytest.mark.parametrize("bad", ["../etc/passwd", "a/b", "", "x" * 200])
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
```

**Step 2: Vérifier l'échec**

Run: `$SRV/run-checks.sh tests/test_store.py`
Expected: FAIL (`cannot import name 'store'`). Note : ruff/mypy passent avant pytest ; si mypy échoue faute de module, c'est aussi un échec attendu.

**Step 3: Implémentation**

```python
"""Stockage fichier des sessions Claude Code (un JSON par session)."""

from __future__ import annotations

import contextlib
import fcntl
import json
import os
import re
import tempfile
from collections.abc import Callable, Iterator
from pathlib import Path
from typing import Any

Data = dict[str, Any]

_SAFE_ID = re.compile(r"^[A-Za-z0-9_-]{1,100}$")


def default_base() -> Path:
    return Path(os.environ.get("CLAUDE_DASH_DIR", Path.home() / ".claude" / "dashboard"))


def _check_id(session_id: str) -> None:
    if not _SAFE_ID.match(session_id):
        raise ValueError(f"session id invalide: {session_id!r}")


@contextlib.contextmanager
def _locked(base: Path) -> Iterator[None]:
    base.mkdir(parents=True, exist_ok=True)
    with open(base / ".lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(lock, fcntl.LOCK_UN)


def _read(path: Path) -> Data:
    try:
        data = json.loads(path.read_text())
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def _write_atomic(path: Path, data: Data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=".tmp-")
    try:
        with os.fdopen(fd, "w") as f:
            json.dump(data, f)
        os.replace(tmp, path)
    except BaseException:
        with contextlib.suppress(OSError):
            os.unlink(tmp)
        raise


def update_session(base: Path, session_id: str, fn: Callable[[Data], None]) -> None:
    """Lit la session, applique `fn` (mutation en place) et réécrit atomiquement."""
    _check_id(session_id)
    path = base / "sessions" / f"{session_id}.json"
    with _locked(base):
        data = _read(path)
        data["id"] = session_id
        fn(data)
        _write_atomic(path, data)


def remove_session(base: Path, session_id: str) -> None:
    _check_id(session_id)
    with _locked(base), contextlib.suppress(FileNotFoundError):
        (base / "sessions" / f"{session_id}.json").unlink()


def load_sessions(base: Path) -> list[Data]:
    folder = base / "sessions"
    if not folder.is_dir():
        return []
    return [d for p in sorted(folder.glob("*.json")) if (d := _read(p))]


def write_limits(base: Path, limits: Data) -> None:
    path = base / "limits.json"
    with _locked(base):
        data = _read(path)
        data.update(limits)
        _write_atomic(path, data)


def load_limits(base: Path) -> Data:
    return _read(base / "limits.json")
```

**Step 4: Vérifier**

Run: `$SRV/run-checks.sh`
Expected: tous les tests passent, ruff/mypy OK.

**Step 5: Commit**

```bash
git add $SRV
git commit -m "Claude_Dashboard: stockage atomique des sessions cote serveur"
```

---

### Task 2: Machine à états des hooks (`hook.py`)

**Files:**
- Create: `$SRV/claude_dash/hook.py`
- Create: `$SRV/claude_dash/procs.py`
- Test: `$SRV/tests/test_hook.py`
- Test: `$SRV/tests/test_procs.py`

Entrée des hooks (stdin, JSON) : champs communs `session_id`, `cwd`, `hook_event_name` ; `PreToolUse`/`PostToolUse` ont `tool_name` ; `SessionStart` a `source` (`startup`, `resume`, `clear`, `compact`) ; `Notification` a `message` et `notification_type` (`permission_prompt`, `idle_prompt`, …).

**Step 1: Tests de la machine à états (fonction pure)**

```python
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
```

**Step 2: Tests de `procs.find_claude_pid`** (arbre de processus injecté)

```python
import os

from claude_dash.procs import find_claude_pid, parse_stat, pid_alive, proc_info

Entry = tuple[int, str, list[str]]
CLI = "/usr/lib/node_modules/@anthropic-ai/claude-code/cli.js"


def test_returns_first_claude_ancestor() -> None:
    tree: dict[int, Entry] = {
        300: (200, "sh", ["sh", "-c", "claude-dash-hook"]),
        200: (100, "claude", ["claude"]),
        100: (1, "bash", ["bash"]),
    }
    assert find_claude_pid(300, lambda p: tree.get(p)) == 200


def test_node_process_with_claude_cmdline() -> None:
    tree: dict[int, Entry] = {
        300: (200, "sh", ["sh"]),
        200: (1, "node", ["/usr/bin/node", CLI]),
    }
    assert find_claude_pid(300, lambda p: tree.get(p)) == 200


def test_none_when_not_found() -> None:
    tree: dict[int, Entry] = {300: (1, "sh", ["sh"])}
    assert find_claude_pid(300, lambda p: tree.get(p)) is None


def test_wrapper_with_claude_in_comm_is_skipped() -> None:
    tree: dict[int, Entry] = {
        400: (300, "python3", ["python3", "/home/u/.local/bin/claude-dash-hook"]),
        300: (200, "claude-dash-hoo", ["/home/u/.local/bin/claude-dash-hook"]),
        200: (100, "claude", ["claude"]),
        100: (1, "bash", ["bash"]),
    }
    assert find_claude_pid(400, lambda p: tree.get(p)) == 200


def test_shell_mentioning_claude_code_is_not_matched() -> None:
    tree: dict[int, Entry] = {
        300: (200, "sh", ["sh"]),
        200: (1, "sh", ["sh", "-c", f"cd ~/claude-code-notes && node {CLI}"]),
    }
    assert find_claude_pid(300, lambda p: tree.get(p)) is None


def test_node_running_other_script_is_not_matched() -> None:
    tree: dict[int, Entry] = {
        300: (200, "sh", ["sh"]),
        200: (1, "node", ["node", "/srv/claude-code-proxy/index.js"]),
    }
    assert find_claude_pid(300, lambda p: tree.get(p)) is None


def test_proc_info_self_returns_parent_pid() -> None:
    info = proc_info(os.getpid())
    assert info is not None
    assert info[0] == os.getppid()
    assert info[2]  # argv non vide pour le processus pytest


def test_proc_info_missing_process() -> None:
    assert proc_info(2**22 + 1) is None  # au-dela de pid_max par defaut


def test_pid_alive_self() -> None:
    assert pid_alive(os.getpid())


def test_parse_stat_comm_with_spaces_and_parens() -> None:
    stat = "1234 ((my (weird) proc)) S 567 1234 1234 0 -1 4194304"
    assert parse_stat(stat) == (567, "(my (weird) proc)")
```

**Step 3: Vérifier l'échec**

Run: `$SRV/run-checks.sh tests/test_hook.py tests/test_procs.py`
Expected: FAIL (modules absents).

**Step 4: `procs.py`**

Détection stricte du processus Claude (un faux positif ferait purger une session vivante par l'agent) : `comm == "claude"`, ou `argv[0]` dont le basename commence par `node` avec `argv[1]` se terminant par `/@anthropic-ai/claude-code/cli.js`. Un wrapper dont le comm contient « claude » (`claude-dash-hoo`) ou un `sh -c` citant `claude-code` ne correspond pas. `ProcInfo` renvoie donc `(ppid, comm, argv)`.

```python
"""Recherche du processus Claude Code parent d'un hook."""

from __future__ import annotations

import os
from collections.abc import Callable

# pid -> (ppid, comm, argv) ; None si le processus n'existe pas
ProcInfo = Callable[[int], tuple[int, str, list[str]] | None]

_CLAUDE_CLI_SUFFIX = "/@anthropic-ai/claude-code/cli.js"


def parse_stat(stat: str) -> tuple[int, str]:
    """Extrait (ppid, comm) d'une ligne /proc/<pid>/stat.

    Le comm peut contenir espaces et parenthèses : on s'appuie sur la dernière ")".
    """
    end = stat.rindex(")")
    comm = stat[stat.index("(") + 1 : end]
    ppid = int(stat[end + 2 :].split()[1])
    return ppid, comm


def proc_info(pid: int) -> tuple[int, str, list[str]] | None:
    try:
        with open(f"/proc/{pid}/stat") as f:
            stat = f.read()
        with open(f"/proc/{pid}/cmdline", "rb") as f:
            raw = f.read()
    except OSError:
        return None
    # arguments séparés par \0, terminés par \0 ; on garde les arguments vides
    argv = [a.decode(errors="replace") for a in raw.rstrip(b"\0").split(b"\0")] if raw else []
    ppid, comm = parse_stat(stat)
    return ppid, comm, argv


def is_claude(comm: str, argv: list[str]) -> bool:
    """Vrai pour le binaire natif `claude` ou `node .../@anthropic-ai/claude-code/cli.js`."""
    if comm == "claude":
        return True
    return (
        len(argv) >= 2
        and os.path.basename(argv[0]).startswith("node")
        and argv[1].endswith(_CLAUDE_CLI_SUFFIX)
    )


def find_claude_pid(start: int, info: ProcInfo = proc_info, max_depth: int = 10) -> int | None:
    """Remonte les ancêtres de `start` jusqu'au processus Claude Code."""
    pid = start
    for _ in range(max_depth):
        entry = info(pid)
        if entry is None:
            return None
        ppid, comm, argv = entry
        if pid != start and is_claude(comm, argv):
            return pid
        if ppid <= 1:
            return None
        pid = ppid
    return None


def pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True
```

**Step 5: `hook.py`**

```python
"""Hook Claude Code : met à jour l'état de la session dans le store."""

from __future__ import annotations

import json
import os
import sys
import time
from pathlib import PurePosixPath
from typing import Any

from claude_dash import store
from claude_dash.procs import find_claude_pid

WORKING = "working"
IDLE = "idle"
PERMISSION = "permission"


def _set_state(data: dict[str, Any], state: str, now: int) -> None:
    if data.get("state") != state:
        data["state"] = state
        data["since"] = now


def _is_permission(event: dict[str, Any]) -> bool:
    ntype = event.get("notification_type")
    if ntype is not None:
        return bool(ntype == "permission_prompt")
    return "permission" in str(event.get("message", "")).lower()


def _basename(path: str | None) -> str:
    return PurePosixPath(path).name if path else ""


def apply_event(
    data: dict[str, Any],
    event: dict[str, Any],
    now: int,
    pid: int | None,
    project_dir: str | None = None,
) -> bool:
    """Applique un événement de hook. Retourne False si la session doit être supprimée.

    `project_dir` (CLAUDE_PROJECT_DIR) fixe le projet ; à défaut, le cwd n'est utilisé
    qu'au premier événement, pour que le libellé ne suive pas les `cd` de Claude.
    """
    name = event.get("hook_event_name")
    if name == "SessionEnd":
        return False

    project = _basename(project_dir)
    if not project and "project" not in data:
        project = _basename(event.get("cwd"))
    if project:
        data["project"] = project
    if pid is not None:
        data["pid"] = pid

    if name == "SessionStart":
        # après un /compact la session continue : on garde l'état courant
        if event.get("source") != "compact" or "state" not in data:
            _set_state(data, IDLE, now)
    elif name == "UserPromptSubmit":
        _set_state(data, WORKING, now)
        data.pop("tool", None)
    elif name in ("PreToolUse", "PostToolUse"):
        _set_state(data, WORKING, now)
        if tool := event.get("tool_name"):
            data["tool"] = str(tool)
    elif name == "Notification" and event.get("notification_type") == "idle_prompt":
        # seul signal après un Esc sur une demande de permission (pas de Stop)
        _set_state(data, IDLE, now)
        data.pop("tool", None)
    elif name == "Notification" and _is_permission(event):
        _set_state(data, PERMISSION, now)
    elif name == "Stop":
        _set_state(data, IDLE, now)
        data.pop("tool", None)

    data["updated"] = now
    return True


def main() -> None:
    """Point d'entrée : ne doit jamais faire échouer Claude Code."""
    try:
        event = json.load(sys.stdin)
        session_id = str(event["session_id"])
        base = store.default_base()
        if event.get("hook_event_name") == "SessionEnd":
            store.remove_session(base, session_id)
        else:
            pid = find_claude_pid(os.getpid())
            now = int(time.time())
            project_dir = os.environ.get("CLAUDE_PROJECT_DIR")

            def mutate(data: dict[str, Any]) -> None:
                apply_event(data, event, now, pid, project_dir)

            store.update_session(base, session_id, mutate)
    except Exception as exc:  # un hook ne doit jamais bloquer Claude
        print(f"claude-dash-hook: {exc}", file=sys.stderr)
    sys.exit(0)
```

Note sur `find_claude_pid(os.getpid())` : le hook tourne sous un `sh` lancé par Claude Code, lui-même lancé par le processus `claude`. On part de notre propre PID et on remonte.

Choix de la machine à états :
- `Notification` de type `idle_prompt` → `idle` (et `tool` effacé) : c'est le seul signal reçu après un Esc sur une demande de permission (aucun `Stop` n'est émis), sinon la carte resterait bloquée sur `permission`/`working`.
- Projet : `CLAUDE_PROJECT_DIR` (lu dans `main()`, passé en `project_dir`) est prioritaire ; à défaut, le `cwd` n'est utilisé que si `project` n'est pas encore connu, pour que le libellé ne suive pas les `cd` de Claude. Un basename vide (`/`) est ignoré.
- `SessionStart` avec `source == "compact"` ne remet pas l'état à `idle` (la session continue), sauf si aucun état n'existe encore.

**Step 6: Vérifier**

Run: `$SRV/run-checks.sh`
Expected: tout passe.

**Step 7: Commit**

```bash
git add $SRV
git commit -m "Claude_Dashboard: machine a etats des hooks Claude Code"
```

---

### Task 3: Collecte statusline (`statusline.py`)

**Files:**
- Create: `$SRV/claude_dash/statusline.py`
- Test: `$SRV/tests/test_statusline.py`

Entrée : le JSON que Claude Code passe à la statusline. Champs utilisés : `session_id`, `model.display_name`, `workspace.project_dir|current_dir` (ou `cwd`), `context_window.used_percentage`, `rate_limits.five_hour|seven_day.{used_percentage,resets_at}`. `resets_at` peut être un epoch (int/float) ou une date ISO 8601 : on normalise en epoch.

Projet : même règle que `hook.py` — `workspace.project_dir` prioritaire, sinon `current_dir`/`cwd` seulement si `project` n'est pas encore connu (basename vide ignoré). Le hook fait autorité sur le PID : la statusline ne comble qu'un PID manquant, et sans PID trouvé elle ne crée pas de nouvelle session (seuls les quotas sont écrits ; helper `store.session_exists`). Une date ISO sans fuseau est lue en UTC. Tests supplémentaires dans `test_statusline.py` : priorité de `project_dir`, non-écrasement d'un projet existant, repli `cwd`, `main()` silencieux avec code retour 0 (`find_claude_pid` monkeypatché : PID trouvé, ou absent avec/sans session existante).

**Step 1: Tests**

```python
from typing import Any

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


def test_extract_limits() -> None:
    assert extract_limits(SAMPLE, now=5) == {
        "h5": 42, "h5_reset": 1790003600, "d7": 18, "d7_reset": 1790838000, "updated": 5,
    }


def test_extract_limits_absent() -> None:
    assert extract_limits({"session_id": "s1"}, now=5) is None


def test_update_session_fields_new_session_defaults_idle() -> None:
    d: dict[str, Any] = {}
    update_from_statusline(d, SAMPLE, now=7, pid=9)
    assert d == {"model": "Opus 5.5", "ctx": 37, "project": "arduino",
                 "state": "idle", "since": 7, "pid": 9, "updated": 7}


def test_update_keeps_existing_state() -> None:
    d: dict[str, Any] = {"state": "working", "since": 3, "pid": 4}
    update_from_statusline(d, SAMPLE, now=7, pid=None)
    assert d["state"] == "working" and d["since"] == 3 and d["pid"] == 4
```

(`1790838000` = 2026-10-01T07:00:00Z, vérifié avec `date -d @1790838000 -u`.)

**Step 2: Vérifier l'échec** — `$SRV/run-checks.sh tests/test_statusline.py` → FAIL.

**Step 3: Implémentation**

```python
"""Collecte des infos de la statusline Claude Code (quotas, contexte, modèle)."""

from __future__ import annotations

import json
import os
import sys
import time
from datetime import UTC, datetime
from pathlib import PurePosixPath
from typing import Any

from claude_dash import store
from claude_dash.procs import find_claude_pid


def parse_epoch(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int | float):
        return int(value)
    if isinstance(value, str):
        try:
            dt = datetime.fromisoformat(value)
        except ValueError:
            return None
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=UTC)
        return int(dt.timestamp())
    return None


def _pct(value: Any) -> int | None:
    if isinstance(value, int | float) and not isinstance(value, bool):
        return max(0, min(100, round(value)))
    return None


def _basename(path: Any) -> str:
    return PurePosixPath(str(path)).name if path else ""


def extract_limits(payload: dict[str, Any], now: int) -> dict[str, Any] | None:
    rl = payload.get("rate_limits")
    if not isinstance(rl, dict):
        return None
    out: dict[str, Any] = {}
    for src, key in (("five_hour", "h5"), ("seven_day", "d7")):
        window = rl.get(src)
        if not isinstance(window, dict):
            continue
        if (pct := _pct(window.get("used_percentage"))) is not None:
            out[key] = pct
        if (reset := parse_epoch(window.get("resets_at"))) is not None:
            out[f"{key}_reset"] = reset
    if not out:
        return None
    out["updated"] = now
    return out


def update_from_statusline(
    data: dict[str, Any], payload: dict[str, Any], now: int, pid: int | None
) -> None:
    model = payload.get("model")
    if isinstance(model, dict) and model.get("display_name"):
        data["model"] = str(model["display_name"])
    ctx = payload.get("context_window")
    if isinstance(ctx, dict) and (pct := _pct(ctx.get("used_percentage"))) is not None:
        data["ctx"] = pct
    ws = payload.get("workspace")
    ws = ws if isinstance(ws, dict) else {}
    project = _basename(ws.get("project_dir"))
    if not project and "project" not in data:
        project = _basename(ws.get("current_dir") or payload.get("cwd"))
    if project:
        data["project"] = project
    if "state" not in data:
        data["state"] = "idle"
        data["since"] = now
    # le hook fait autorité sur le PID ; la statusline ne comble qu'un PID manquant
    if pid is not None and "pid" not in data:
        data["pid"] = pid
    data["updated"] = now


def main() -> None:
    """Lit le JSON de la statusline sur stdin. Silencieux, code retour toujours 0."""
    try:
        payload = json.load(sys.stdin)
        base = store.default_base()
        now = int(time.time())
        if limits := extract_limits(payload, now):
            store.write_limits(base, limits)
        session_id = str(payload["session_id"])
        pid = find_claude_pid(os.getpid())

        def mutate(data: dict[str, Any]) -> None:
            update_from_statusline(data, payload, now, pid)

        # sans PID, pas de nouvelle session (évite les fantômes, ex. après SessionEnd)
        if pid is not None or store.session_exists(base, session_id):
            store.update_session(base, session_id, mutate)
    except Exception as exc:  # la statusline ne doit jamais échouer bruyamment
        print(f"claude-dash-statusline: {exc}", file=sys.stderr)
    sys.exit(0)
```

**Step 4: Vérifier** — `$SRV/run-checks.sh` → tout passe.

**Step 5: Commit**

```bash
git add $SRV
git commit -m "Claude_Dashboard: collecte quotas et contexte depuis la statusline"
```

---

### Task 4: Construction du snapshot (`snapshot.py`)

**Files:**
- Create: `$SRV/claude_dash/snapshot.py`
- Test: `$SRV/tests/test_snapshot.py`

Règles :
- session avec `pid` mort → exclue et listée dans `dead` (l'agent la purgera) ;
- session sans `pid` : gardée si `updated` < 12 h, sinon morte ;
- champs publiés uniquement : `id` (8 premiers caractères), `project` (32 max), `model` (24), `state`, `since`, `ctx`, `tool` (24) ; **jamais** `pid`, chemins ou autres ;
- au plus 12 sessions (les plus récemment mises à jour) ;
- `limits` publiées seulement si présentes.

**Step 1: Tests**

```python
from typing import Any

from claude_dash.snapshot import build_snapshot

NOW = 1_000_000


def s(**kw: Any) -> dict[str, Any]:
    base = {"id": "abcdef0123456789", "state": "idle", "since": NOW - 10,
            "updated": NOW - 5, "pid": 10, "project": "arduino", "model": "Opus 5.5"}
    base.update(kw)
    return base


def test_basic_snapshot() -> None:
    snap, dead = build_snapshot("srv", [s(ctx=37, tool="Bash")], {"h5": 42, "updated": 1},
                                NOW, pid_alive=lambda p: True)
    assert dead == []
    assert snap == {
        "host": "srv", "ts": NOW,
        "limits": {"h5": 42},
        "sessions": [{"id": "abcdef01", "project": "arduino", "model": "Opus 5.5",
                      "state": "idle", "since": NOW - 10, "ctx": 37, "tool": "Bash"}],
    }


def test_dead_pid_excluded() -> None:
    snap, dead = build_snapshot("srv", [s(id="a1", pid=10), s(id="b2", pid=11)], {},
                                NOW, pid_alive=lambda p: p == 11)
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
    snap, _ = build_snapshot("srv", [s(project="p" * 50, state="weird")], {}, NOW,
                             pid_alive=lambda p: True)
    sess = snap["sessions"][0]
    assert len(sess["project"]) == 32
    assert sess["state"] == "idle"


def test_max_12_most_recent() -> None:
    sessions = [s(id=f"s{i:02d}", updated=NOW - i) for i in range(20)]
    snap, dead = build_snapshot("srv", sessions, {}, NOW, pid_alive=lambda p: True)
    assert len(snap["sessions"]) == 12
    assert snap["sessions"][0]["id"] == "s00"
    assert dead == []
```

**Step 2: Vérifier l'échec** — FAIL attendu.

**Step 3: Implémentation**

```python
"""Construit le snapshot publié sur MQTT à partir du store."""

from __future__ import annotations

from collections.abc import Callable
from typing import Any

STATES = ("working", "idle", "permission")
MAX_SESSIONS = 12
NO_PID_TTL = 12 * 3600
LIMIT_KEYS = ("h5", "h5_reset", "d7", "d7_reset")


def _is_dead(sess: dict[str, Any], now: int, pid_alive: Callable[[int], bool]) -> bool:
    pid = sess.get("pid")
    if isinstance(pid, int):
        return not pid_alive(pid)
    return now - int(sess.get("updated", 0)) > NO_PID_TTL


def _public(sess: dict[str, Any]) -> dict[str, Any]:
    state = sess.get("state")
    out: dict[str, Any] = {
        "id": str(sess["id"])[:8],
        "project": str(sess.get("project", "?"))[:32],
        "model": str(sess.get("model", ""))[:24],
        "state": state if state in STATES else "idle",
        "since": int(sess.get("since", 0)),
    }
    if isinstance(sess.get("ctx"), int):
        out["ctx"] = sess["ctx"]
    if sess.get("tool"):
        out["tool"] = str(sess["tool"])[:24]
    return out


def build_snapshot(
    host: str,
    sessions: list[dict[str, Any]],
    limits: dict[str, Any],
    now: int,
    pid_alive: Callable[[int], bool],
) -> tuple[dict[str, Any], list[str]]:
    """Retourne (snapshot publiable, ids des sessions mortes à purger)."""
    alive, dead = [], []
    for sess in sessions:
        if "id" not in sess:
            continue
        (dead if _is_dead(sess, now, pid_alive) else alive).append(sess)
    alive.sort(key=lambda x: int(x.get("updated", 0)), reverse=True)

    snap: dict[str, Any] = {"host": host, "ts": now}
    pub_limits = {k: limits[k] for k in LIMIT_KEYS if k in limits}
    if pub_limits:
        snap["limits"] = pub_limits
    snap["sessions"] = [_public(x) for x in alive[:MAX_SESSIONS]]
    return snap, [str(x["id"]) for x in dead]
```

> **Écart implémenté (revue) :** valeurs bornées — `_as_int` renvoie 0 pour bool, non-numérique, NaN/inf ; un `pid` n'est pris en compte que s'il est int (pas bool) et dans `1..4_194_304` (sinon règle des 12 h ; évite `kill(-1, 0)`) ; `ctx`, `h5`, `d7` bornés à 0..100 ; `limits` : entiers uniquement, `dict` vérifié ; éléments non-dict ignorés. Texte via `_text` (NFKD → ASCII imprimable, puis troncature ; non-str → défaut, `"?"` pour `project`), `host` limité à 32 : pas d'échappement `\uXXXX`, payload max mesuré ≈ 2,3 Ko pour 12 sessions (≈ 3,3 Ko si tout le texte est en `"`/`\`), sous le buffer MQTT 4 Ko de l'ESP32. Tests en plus : valeurs corrompues/non finies, pid hors bornes, ASCII, taille du payload, confidentialité, troncature 24, les 4 clés `limits`.

**Step 4: Vérifier** — tout passe.

**Step 5: Commit**

```bash
git add $SRV
git commit -m "Claude_Dashboard: construction du snapshot publie"
```

---

### Task 5: Agent MQTT (`config.py`, `agent.py`)

**Files:**
- Create: `$SRV/claude_dash/config.py`
- Create: `$SRV/claude_dash/agent.py`
- Test: `$SRV/tests/test_config.py`
- Test: `$SRV/tests/test_agent.py`

Config `~/.config/claude-dash/config.toml` :

```toml
[mqtt]
host = "xxxx.s1.eu.hivemq.cloud"
port = 8883
username = "claude-dash-srv"
password = "..."
# ca_certs = "/etc/ssl/certs/ca-certificates.crt"   # défaut : CA système
# tls = true                                         # false pour un Mosquitto local (Task 6)
topic_prefix = "claude-dash"

[agent]
# hostname = "srv-dev"    # défaut : socket.gethostname()
```

**Step 1: Tests config**

```python
from pathlib import Path

import pytest

from claude_dash.config import ConfigError, load_config


def write(tmp_path: Path, text: str, mode: int = 0o600) -> Path:
    p = tmp_path / "config.toml"
    p.write_text(text)
    p.chmod(mode)
    return p


def test_load_defaults(tmp_path: Path) -> None:
    cfg = load_config(write(tmp_path, '[mqtt]\nhost="h"\nusername="u"\npassword="p"\n'))
    assert cfg.port == 8883
    assert cfg.topic_prefix == "claude-dash"
    assert cfg.ca_certs is None
    assert cfg.hostname  # socket.gethostname()


def test_missing_host(tmp_path: Path) -> None:
    with pytest.raises(ConfigError):
        load_config(write(tmp_path, "[mqtt]\n"))


def test_rejects_world_readable(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="600"):
        load_config(write(tmp_path, '[mqtt]\nhost="h"\n', mode=0o644))
```

**Step 2: Tests du publisher** (logique de throttling isolée, horloge injectée)

```python
from claude_dash.agent import Publisher


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
    assert pub.tick(snap(60), now=60.0)      # heartbeat
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
```

**Step 3: Vérifier l'échec** — FAIL attendu.

**Step 4: `config.py`**

```python
"""Chargement de la configuration de l'agent (TOML, permissions 600)."""

from __future__ import annotations

import os
import socket
import stat
import tomllib
from dataclasses import dataclass
from pathlib import Path


class ConfigError(Exception):
    pass


@dataclass(frozen=True)
class Config:
    host: str
    port: int
    username: str | None
    password: str | None
    ca_certs: str | None
    topic_prefix: str
    hostname: str

    @property
    def topic(self) -> str:
        return f"{self.topic_prefix}/{self.hostname}/state"


def default_path() -> Path:
    xdg = os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))
    return Path(xdg) / "claude-dash" / "config.toml"


def load_config(path: Path) -> Config:
    try:
        mode = stat.S_IMODE(path.stat().st_mode)
        raw = tomllib.loads(path.read_text())
    except (OSError, tomllib.TOMLDecodeError) as exc:
        raise ConfigError(f"{path}: {exc}") from exc
    if mode & 0o077:
        raise ConfigError(f"{path} doit être en chmod 600 (actuel {mode:o})")
    mqtt = raw.get("mqtt", {})
    agent = raw.get("agent", {})
    if not mqtt.get("host"):
        raise ConfigError(f"{path}: [mqtt] host manquant")
    return Config(
        host=str(mqtt["host"]),
        port=int(mqtt.get("port", 8883)),
        username=mqtt.get("username"),
        password=mqtt.get("password"),
        ca_certs=mqtt.get("ca_certs"),
        topic_prefix=str(mqtt.get("topic_prefix", "claude-dash")),
        hostname=str(agent.get("hostname") or socket.gethostname().split(".")[0]),
    )
```

**Step 5: `agent.py`**

```python
"""Démon : agrège les sessions et publie le snapshot sur MQTT."""

from __future__ import annotations

import json
import logging
import signal
import sys
import time
from collections.abc import Callable
from typing import Any

import paho.mqtt.client as mqtt

from claude_dash import store
from claude_dash.config import Config, ConfigError, default_path, load_config
from claude_dash.procs import pid_alive
from claude_dash.snapshot import build_snapshot

log = logging.getLogger("claude-dash-agent")


class Publisher:
    """Publie si le contenu change (au plus toutes les `min_interval` s) ou en heartbeat."""

    def __init__(
        self, send: Callable[[str], None], min_interval: float = 2, heartbeat: float = 60
    ) -> None:
        self._send = send
        self._min_interval = min_interval
        self._heartbeat = heartbeat
        self._last_body: str | None = None
        self._last_sent = float("-inf")

    def tick(self, snapshot: dict[str, Any], now: float) -> bool:
        body = json.dumps({k: v for k, v in snapshot.items() if k != "ts"}, sort_keys=True)
        elapsed = now - self._last_sent
        changed = body != self._last_body
        if not ((changed and elapsed >= self._min_interval) or elapsed >= self._heartbeat):
            return False
        try:
            self._send(json.dumps(snapshot, separators=(",", ":")))
        except Exception as exc:  # noqa: BLE001
            log.warning("publication échouée: %s", exc)
            return False
        self._last_body = body
        self._last_sent = now
        return True


def _make_client(cfg: Config) -> mqtt.Client:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"claude-dash-{cfg.hostname}")
    client.tls_set(ca_certs=cfg.ca_certs)
    if cfg.username:
        client.username_pw_set(cfg.username, cfg.password)
    client.reconnect_delay_set(min_delay=1, max_delay=60)
    client.on_connect = lambda c, u, f, rc, p: log.info("MQTT connecté (%s)", rc)
    client.on_disconnect = lambda c, u, f, rc, p: log.warning("MQTT déconnecté (%s)", rc)
    client.connect_async(cfg.host, cfg.port, keepalive=60)
    client.loop_start()
    return client


def run(cfg: Config) -> None:
    client = _make_client(cfg)
    base = store.default_base()

    def send(payload: str) -> None:
        if not client.is_connected():
            raise OSError("MQTT non connecté")
        info = client.publish(cfg.topic, payload, qos=1, retain=True)
        if info.rc != mqtt.MQTT_ERR_SUCCESS:
            raise OSError(f"publish rc={info.rc}")

    publisher = Publisher(send)
    stop = False

    def _stop(*_: object) -> None:
        nonlocal stop
        stop = True

    signal.signal(signal.SIGTERM, _stop)
    signal.signal(signal.SIGINT, _stop)
    log.info("agent démarré, topic %s", cfg.topic)
    while not stop:
        now = int(time.time())
        snap, dead = build_snapshot(
            cfg.hostname, store.load_sessions(base), store.load_limits(base), now, pid_alive
        )
        for sid in dead:
            log.info("purge session morte %s", sid)
            store.remove_session(base, sid)
        publisher.tick(snap, time.monotonic())
        time.sleep(1)
    client.loop_stop()
    client.disconnect()


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    try:
        cfg = load_config(default_path())
    except ConfigError as exc:
        log.error("%s", exc)
        sys.exit(2)
    run(cfg)
```

**Step 6: Vérifier** — `$SRV/run-checks.sh` → tout passe.

**Écarts d'implémentation** (le code du dépôt fait foi) :
- Option `tls` (défaut `true`) ajoutée dès cette tâche au lieu de la Task 6 : `Config.tls`, `if cfg.tls: client.tls_set(...)`, test `test_tls_can_be_disabled`.
- `load_config` valide les types (port entier 1..65535, `tls` booléen, chaînes) ; `topic_prefix` sans `/`, `+`, `#` ni `$` initial ; `hostname` conforme à `^[A-Za-z0-9._-]{1,32}$` (le nom court par défaut aussi, sinon `ConfigError` demandant de renseigner `[agent] hostname`). Toute erreur devient `ConfigError`.
- `store.load_sessions` : le nom de fichier fait foi pour l'id (`d | {"id": p.stem}`, noms non conformes à `_SAFE_ID` ignorés) ; un `x.json` annonçant `"id": "y"` purge `x.json`, jamais `y.json`.
- Une itération de la boucle est factorisée dans `run_once(base, host, publisher, now_epoch, now_mono, pid_alive)` qui ne lève jamais (`log.exception`) : la purge continue si un id est invalide (`ValueError`), la publication a lieu malgré tout. Tests avec un store `tmp_path` et un faux publisher.
- Classe `Outage` : une panne persistante n'est journalisée qu'une fois (puis DEBUG, puis INFO au rétablissement). Utilisée par `Publisher` (« publication rétablie ») et par `run_once` (traceback complète la première fois, « agent rétabli »).
- `on_connect` (factorisé dans `on_connect_callback`) : `log.error("MQTT refusé")` si `reason_code.is_failure`, sinon INFO et pose d'un `threading.Event` ; la boucle appelle alors `publisher.force()` (le tick suivant publie sans tenir compte du contenu ni du throttling). `on_connect_fail` journalisé en DEBUG.
- Arrêt : `client.disconnect()` puis `client.loop_stop()`. `main()` intercepte les erreurs d'initialisation du client (`OSError`, `ssl.SSLError`, `ValueError`, ex. `ca_certs` introuvable) : message clair et code de sortie 2. `run(cfg, client, connected)` reçoit le client créé par `main()`.
- `CallbackAPIVersion` et `MQTTErrorCode` importés de `paho.mqtt.enums` (paho 2.x est typé, mypy strict refuse l'attribut non réexporté).
- Suivi de `snapshot.py` (commit séparé) : `id` passé par `_text(..., 8, "?")`, horodatages (`since`, `h5_reset`, `d7_reset`) bornés à `0 <= v < 2**32`, sinon 0.

**Step 7: Commit**

```bash
git add $SRV
git commit -m "Claude_Dashboard: agent MQTT avec throttling et heartbeat"
```

---

### Task 6: Test d'intégration serveur avec Mosquitto (Docker)

But : vérifier la chaîne complète hook → store → agent → broker, sans TLS (Mosquitto local), avant d'y connecter l'ESP32, y compris les mécanismes de robustesse de la Task 5 (republication forcée après reconnexion).

**Files:**
- Create: `$SRV/dev/mosquitto.conf` (`listener 1883`, `allow_anonymous true`, sans persistance)
- Create: `$SRV/dev/docker-compose.yml` (`eclipse-mosquitto:2`, port hôte `${CLAUDE_DASH_MQTT_PORT:-18830}` → 1883)
- Create: `$SRV/dev/integration-test.sh`
- Create: `$SRV/dev/README.md`

**Step 1: `dev/integration-test.sh`** (bash, `set -euo pipefail`)

- Démarre Mosquitto via `docker compose -p claude-dash-it`, attend qu'il réponde.
- Conteneur `python:3.13-slim` détaché (`--network host`, `--user "$(id -u):$(id -g)"`, `HOME=/tmp`, `CLAUDE_DASH_DIR=/tmp/dash`, `XDG_CONFIG_HOME=/tmp/.config`), `pip install --user -e .`, config `tls = false`, `hostname = "test"` en chmod 600. Hooks via `docker exec`, agent via `docker exec -d` (journal dans `/tmp/agent.log`).
- Assertions : lecture du retained de `claude-dash/test/state` avec `mosquitto_sub -C 1 -W 2` (image `eclipse-mosquitto:2`), JSON vérifié par `python3` dans le conteneur, en boucle jusqu'à un délai.
- Scénario : (1) `SessionStart` + `permission_prompt` puis démarrage de l'agent → `s1`/`arduino`/`permission` ; (2) `Stop` → `idle` en ≤ 6 s ; (3) broker arrêté 10 s puis relancé (retained perdu) → republication en ≤ 30 s, donc avant le heartbeat de 60 s ; (4) `SessionEnd` → `sessions` vide.
- `PASS`/`FAIL` par étape, code de sortie non nul et journal de l'agent en cas d'échec ; `trap` : suppression du conteneur agent et `compose down`.

Note : sans processus `claude`, `find_claude_pid` retourne `None` et la session vit 12 h, ce qui convient ici.

**Step 2: Lancer** — `$SRV/dev/integration-test.sh` → `RÉSULTAT : PASS` (~30 s ; republication observée ~5 s après le redémarrage du broker).

**Step 3: Commit**

```bash
git add $SRV/dev docs/plans/2026-09-30-claude-dashboard.md
git commit -m "Claude_Dashboard: environnement de test Mosquitto"
```

---

### Task 7: Installation sur le serveur (`install.sh`, systemd, README)

**Files:**
- Create: `$SRV/install.sh`
- Create: `$SRV/claude-dash-agent.service`
- Create: `$SRV/config.toml.example`
- Create: `$SRV/README.md`
- Create: `$SRV/dev/test-install.sh`, `$SRV/dev/fixtures/settings.json`
- Modify: `$SRV/claude_dash/agent.py`, `$SRV/tests/test_agent.py`, `$SRV/.gitignore` (`build/`)

La statusline appelle le collecteur au premier plan (`timeout 1`, sans `&`) : lancé en arrière-plan, il est réadopté par `systemd --user` et ne retrouve presque jamais le PID de Claude.

**Step 1: `claude-dash-agent.service`**

```ini
[Unit]
Description=Claude Code dashboard agent (MQTT)

[Service]
ExecStart=%h/.local/bin/claude-dash-agent
Restart=always
RestartSec=10
RestartPreventExitStatus=2

[Install]
WantedBy=default.target
```

(`After=network-online.target` retiré : sans effet dans le gestionnaire utilisateur ;
l'agent se reconnecte seul. `RestartPreventExitStatus=2` : config invalide → pas de relance
en boucle.)

**Step 2: `config.toml.example`** — le contenu TOML de la Task 5 avec des placeholders (`votre_host`, `votre_user`, `votre_mot_de_passe`) et `tls = true`.

**Step 3: `install.sh`** (écarts par rapport à l'esquisse initiale)

Le script réel (`$SRV/install.sh`) reprend le principe (venv `~/.local/share/claude-dash/venv`,
liens dans `~/.local/bin`, config `install -m 600` jamais écrasée, unit dans
`~/.config/systemd/user`, chemins fixes : `$XDG_CONFIG_HOME` ignoré car l'agent sous systemd lit
`~/.config`) avec :

- trois modes : installation (défaut, idempotente), `--hooks-only`, `--uninstall` ;
- vérifications : `jq`, Python ≥ 3.11 et module `venv` (variable `PYTHON`) ;
  `CLAUDE_CONFIG_DIR` respecté pour `settings.json` ;
- fusion jq : entrées `claude-dash-hook` reconnues par leur commande (`(^|/)claude-dash-hook$`,
  quel que soit le chemin), retirées au niveau du hook (les hooks tiers d'un même groupe
  restent), puis une entrée sans `matcher` par événement (7 événements, `timeout: 5`) ;
- `settings.json` validé avant fusion (objet, `hooks` objet de tableaux d'objets, erreur jq
  affichée) : sinon arrêt sans rien toucher — dès le début de l'installation complète, avant le
  venv ; réécriture seulement si le contenu change, après sauvegarde
  `settings.json.bak-claude-dash-AAAAMMJJ-HHMMSS-<ns>-<pid>` (une sauvegarde unique écrasée à
  chaque passage perdait l'original) ;
- écriture atomique : cible résolue (`readlink -f`), temporaire dans son dossier,
  `chmod --reference`, `mv -f` (lien symbolique et droits conservés) ; juste avant le `mv`,
  abandon avec message si le fichier a changé depuis sa lecture (`cmp`) ;
- `--hooks-only` avertit si `~/.local/bin/claude-dash-hook` n'est pas exécutable ; rappel
  statusline : `.statusLine.command` lu avec jq, le script qu'elle lance est cherché et inspecté,
  sinon rappel générique ; venv recréé avec `--clear` si `bin/python` manque ;
- `pip install --upgrade` puis `--force-reinstall --no-deps` (même numéro de version après
  un `git pull`) ;
- systemd : si `systemctl --user show-environment` échoue (conteneur, pas de bus), l'unit est
  copiée mais pas activée, avec un message ; sinon `enable` (échec → arrêt explicite), et
  `restart` seulement si la config ne contient plus de placeholder `votre_` ;
- `--uninstall` : retire nos hooks (supprime les tableaux d'événement vidés, puis `hooks` si
  vide), `disable --now` + suppression de l'unit (et du lien `default.target.wants`, avec
  avertissement « non arrêté » sans bus utilisateur), du venv et de nos liens ; conserve config,
  store et sauvegardes ; ne crée pas `settings.json` s'il est absent ; idempotent.

`chmod +x install.sh`. Vérifier avec `shellcheck` en Docker :
`docker run --rm -v "$PWD/$SRV":/mnt koalaman/shellcheck:stable /mnt/install.sh /mnt/run-checks.sh /mnt/dev/integration-test.sh /mnt/dev/test-install.sh`
Expected : aucun avertissement (corriger sinon).

**Step 4: Tester l'installation** — `$SRV/dev/test-install.sh` (Docker `python:3.13-slim` + jq,
utilisateur non root, `HOME` fictif, dépôt monté en lecture seule) : fusion sur la fixture
synthétique `dev/fixtures/settings.json` (hooks rtk-like, hook `Stop` tiers partagé avec une
ancienne entrée claude-dash, `PreCompact`, statusLine…) : 7 entrées uniques, hooks tiers et reste
du fichier conservés, sauvegarde, second passage sans écriture ; désinstallation (et
install + uninstall = original) ; cas limites (JSON invalide, `hooks` non objet, fichier absent,
lien symbolique, droits, entrée non objet, modification concurrente simulée via
`CLAUDE_DASH_TEST_BEFORE_WRITE`, temporaires nettoyés, sauvegardes distinctes) ; rappel statusline ; installation complète sans systemd puis réinstallation (config
conservée, remise en 600) et désinstallation. `CLAUDE_DASH_EXTRA_SETTINGS=~/.claude/settings.json`
rejoue la fusion sur une copie en lecture seule d'un vrai fichier (jamais versionné).
Expected : `RÉSULTAT : PASS`.

**Step 4b: Agent** — au démarrage, avant la première connexion MQTT, les échecs de publication
(« MQTT non connecté ») sont journalisés en DEBUG pendant `startup_grace` (30 s) puis en WARNING
(une fois) : tests `test_startup_failures_*` dans `test_agent.py`.

**Step 5: `README.md`** — architecture (schéma du design), prérequis (Python ≥ 3.11, jq, systemd user), étapes d'installation, config broker et ACL recommandées (compte serveur : publish `claude-dash/<host>/#` ; compte écran : subscribe `claude-dash/#`), exemple de statusline minimale :

```bash
#!/usr/bin/env bash
input=$(cat)
printf '%s' "$input" | timeout 1 ~/.local/bin/claude-dash-statusline >/dev/null 2>&1
echo "$input" | jq -r '.model.display_name'
```

et dépannage (`journalctl`, `mosquitto_sub`). Préciser aussi que le nom d'hôte doit être unique par broker (il sert au client id `claude-dash-<host>` et au topic) : si deux serveurs partagent le même nom court, renseigner `[agent] hostname` sur l'un d'eux.

**Step 6: Commit**

```bash
git add $SRV
git commit -m "Claude_Dashboard: script d'installation serveur et service systemd"
```

---

## Partie B — Firmware JC3248W535C

### Task 8: Squelette PlatformIO

**Files:**
- Create: `$FW/platformio.ini`
- Create: `$FW/include/*` (copie de `Transit_Tracker/include/`)
- Create: `$FW/src/{esp_bsp.c,esp_lcd_axs15231b.c,esp_lcd_touch.c,lv_port.c}` (copie de `Transit_Tracker/src/`)
- Create: `$FW/src/credentials.h` → symlink absolu vers `sketches/common/credentials.h`
- Create: `$FW/src/main.cpp` (minimal)
- Modify: `sketches/common/credentials.h.example`

**Step 1: Copier les drivers**

```bash
T=sketches/JC3248W535C/Transit_Tracker
mkdir -p $FW/src $FW/include $FW/lib $FW/test
cp $T/include/*.h $FW/include/
cp $T/src/esp_bsp.c $T/src/esp_lcd_axs15231b.c $T/src/esp_lcd_touch.c $T/src/lv_port.c $FW/src/
ln -s /home/pascal/github/arduino/sketches/common/credentials.h $FW/src/credentials.h
```

Note : le symlink `credentials.h` est couvert par `**/credentials.h` du `.gitignore` : il reste local (non versionne), comme dans les autres sketches.

**Step 2: `platformio.ini`**

```ini
; Claude_Dashboard - JC3248W535C
; Tableau de bord Claude Code (sessions distantes via MQTT/TLS)

[platformio]
default_envs = esp32s3

[env:esp32s3]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/51.03.07/platform-espressif32.zip
board = esp32-s3-devkitc-1
framework = arduino
upload_port = /dev/ttyACM0
monitor_port = /dev/ttyACM0
monitor_speed = 115200

board_build.mcu = esp32s3
board_build.f_cpu = 240000000L
board_build.arduino.memory_type = qio_opi
board_build.flash_mode = qio
board_build.psram_type = opi
board_upload.flash_size = 16MB

build_flags =
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DBOARD_HAS_PSRAM
    -DLV_CONF_INCLUDE_SIMPLE
    -DLV_LVGL_H_INCLUDE_SIMPLE
    -DMQTT_MAX_PACKET_SIZE=4096
    -I${PROJECT_DIR}/include

lib_deps =
    lvgl/lvgl@^8.3.11
    bblanchon/ArduinoJson@^7.0.0
    knolleary/PubSubClient@^2.8

check_tool = cppcheck
check_flags = cppcheck: --enable=warning,style,performance --inline-suppr
check_src_filters = +<src/main.cpp> +<lib/dash_model/>

[env:native]
platform = native
build_flags = -std=c++17 -Wall -Wextra
lib_deps =
    bblanchon/ArduinoJson@^7.0.0
```

**Step 3: `src/main.cpp` minimal** (en-tête standard du projet + écran noir « Claude Dashboard ») pour valider la chaîne de build :

```cpp
/*
 * Claude_Dashboard - JC3248W535C
 *
 * Tableau de bord Claude Code : quotas du forfait, etat des sessions
 * (travaille / attente / permission) et contexte, recus d'un serveur
 * distant via MQTT/TLS. Bip et reveil de l'ecran quand une session
 * attend une reponse.
 *
 * Board: JC3248W535C (ESP32-S3 + LCD tactile 3.5")
 * FQBN: PlatformIO esp32-s3-devkitc-1
 *
 * @dependencies LVGL 8.3.x, ArduinoJson, PubSubClient
 */

#include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"

void setup()
{
    Serial.begin(115200);

    // Paysage (480x320) : rotation 270 comme Transit_Tracker
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = EXAMPLE_LCD_QSPI_H_RES * EXAMPLE_LCD_QSPI_V_RES,
        .rotate = LV_DISP_ROT_270,
    };
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    bsp_display_lock(0);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x0f1419), 0);
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Claude Dashboard");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    bsp_display_unlock();
}

void loop()
{
    delay(1000);
}
```

**Step 4: Placeholders `credentials.h.example`** (ajouter à la fin) :

```c
// MQTT cloud du tableau de bord Claude Code (Claude_Dashboard) - TLS
#define DASH_MQTT_SERVER "xxxx.s1.eu.hivemq.cloud"
#define DASH_MQTT_PORT 8883
#define DASH_MQTT_USER "votre_user_ecran"
#define DASH_MQTT_PASS "votre_password"
// Certificat racine du broker (PEM). Ex. ISRG Root X1 pour HiveMQ Cloud.
#define DASH_MQTT_CA_CERT \
    "-----BEGIN CERTIFICATE-----\n" \
    "...\n" \
    "-----END CERTIFICATE-----\n"
```

Demander à l'utilisateur de renseigner son `credentials.h` réel (ne jamais l'écrire soi-même avec de vraies valeurs).

**Step 5: Compiler**

Run: `cd $FW && $PIO run -e esp32s3`
Expected: `SUCCESS`.

**Step 6: Upload et vérification visuelle** : `$PIO run -e esp32s3 -t upload` → texte « Claude Dashboard » centré en paysage.

**Step 7: Commit**

```bash
git add $FW sketches/common/credentials.h.example
git commit -m "Claude_Dashboard: squelette PlatformIO JC3248 (drivers ecran)"
```

---

### Task 9: Modèle pur `dash_model` — parsing (TDD natif)

**Files:**
- Create: `$FW/lib/dash_model/dash_model.h`
- Create: `$FW/lib/dash_model/dash_model.cpp`
- Test: `$FW/test/test_parse/test_main.cpp`

**Step 1: Tests Unity**

(Écart par rapport à la première version du plan : tests défensifs ajoutés — types JSON erronés, host non-chaîne, `sessions` non-tableau, pourcentages bornés, sortie intacte en cas d'échec (JSON invalide, host absent ou vide), host de 32 caractères conservé, entrées de session non-objet ou sans `id` ignorées, re-parse sans résidu ; buffer du test de troncature agrandi à 8 Ko avec vérification qu'il n'est pas tronqué.)

```cpp
#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "dash_model.h"

using namespace dash;

static const char *SAMPLE =
    "{\"host\":\"srv\",\"ts\":1000,"
    "\"limits\":{\"h5\":42,\"h5_reset\":4600,\"d7\":18,\"d7_reset\":90000},"
    "\"sessions\":["
    "{\"id\":\"a1\",\"project\":\"arduino\",\"model\":\"Opus 5.5\",\"state\":\"permission\","
    "\"since\":950,\"ctx\":37,\"tool\":\"Bash\"},"
    "{\"id\":\"b2\",\"project\":\"api\",\"model\":\"Sonnet\",\"state\":\"idle\",\"since\":700}"
    "]}";

void setUp() {}
void tearDown() {}

void test_parse_full() {
    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(SAMPLE, strlen(SAMPLE), s));
    TEST_ASSERT_EQUAL_STRING("srv", s.host);
    TEST_ASSERT_EQUAL(1000, (long)s.ts);
    TEST_ASSERT_EQUAL(42, s.limits.h5);
    TEST_ASSERT_EQUAL(4600, (long)s.limits.h5Reset);
    TEST_ASSERT_EQUAL(18, s.limits.d7);
    TEST_ASSERT_EQUAL(90000, (long)s.limits.d7Reset);
    TEST_ASSERT_EQUAL(2, s.count);
    TEST_ASSERT_EQUAL_STRING("a1", s.sessions[0].id);
    TEST_ASSERT_EQUAL_STRING("arduino", s.sessions[0].project);
    TEST_ASSERT_EQUAL_STRING("Opus 5.5", s.sessions[0].model);
    TEST_ASSERT_EQUAL((int)State::Permission, (int)s.sessions[0].state);
    TEST_ASSERT_EQUAL(950, (long)s.sessions[0].since);
    TEST_ASSERT_EQUAL(37, s.sessions[0].ctx);
    TEST_ASSERT_EQUAL_STRING("Bash", s.sessions[0].tool);
    TEST_ASSERT_EQUAL((int)State::Idle, (int)s.sessions[1].state);
    TEST_ASSERT_EQUAL(-1, s.sessions[1].ctx);  // absent
    TEST_ASSERT_EQUAL_STRING("", s.sessions[1].tool);
}

void test_parse_no_limits() {
    const char *j = "{\"host\":\"h\",\"ts\":1,\"sessions\":[]}";
    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(j, strlen(j), s));
    TEST_ASSERT_EQUAL(-1, s.limits.h5);
    TEST_ASSERT_EQUAL(-1, s.limits.d7);
    TEST_ASSERT_EQUAL(0, s.count);
}

void test_parse_invalid_json() {
    HostSnapshot s;
    TEST_ASSERT_FALSE(parseSnapshot("{oops", 5, s));
}

void test_parse_missing_host() {
    const char *j = "{\"ts\":1,\"sessions\":[]}";
    HostSnapshot s;
    TEST_ASSERT_FALSE(parseSnapshot(j, strlen(j), s));
}

void test_parse_failure_leaves_output_untouched() {
    const char *bad[] = {"{oops", "{\"ts\":1,\"sessions\":[]}",
                         "{\"host\":\"\",\"ts\":1,\"sessions\":[]}"};
    for (const char *j : bad) {
        HostSnapshot s;
        strcpy(s.host, "keep");
        s.count = 3;
        TEST_ASSERT_FALSE_MESSAGE(parseSnapshot(j, strlen(j), s), j);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("keep", s.host, j);
        TEST_ASSERT_EQUAL_MESSAGE(3, s.count, j);
    }
}

void test_host_of_32_chars_kept_intact() {
    const char *j = "{\"host\":\"0123456789abcdef0123456789ABCDEF\",\"ts\":1,\"sessions\":[]}";
    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(j, strlen(j), s));
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789ABCDEF", s.host);
}

void test_invalid_session_entries_skipped() {
    const char *j =
        "{\"host\":\"h\",\"ts\":1,\"sessions\":[1,\"x\",null,[],{\"state\":\"idle\"},"
        "{\"id\":\"\",\"state\":\"idle\"},{\"id\":\"ok\",\"state\":\"idle\",\"since\":1}]}";
    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(j, strlen(j), s));
    TEST_ASSERT_EQUAL(1, s.count);
    TEST_ASSERT_EQUAL_STRING("ok", s.sessions[0].id);
}

void test_reparse_resets_previous_content() {
    // Le snapshot est reutilise (static dans main.cpp) : aucun residu du parse precedent.
    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(SAMPLE, strlen(SAMPLE), s));
    const char *j =
        "{\"host\":\"h2\",\"sessions\":[{\"id\":\"z\",\"state\":\"working\"}]}";
    TEST_ASSERT_TRUE(parseSnapshot(j, strlen(j), s));
    TEST_ASSERT_EQUAL_STRING("h2", s.host);
    TEST_ASSERT_EQUAL(0, (long)s.ts);
    TEST_ASSERT_EQUAL(-1, s.limits.h5);
    TEST_ASSERT_EQUAL(0, (long)s.limits.h5Reset);
    TEST_ASSERT_EQUAL(-1, s.limits.d7);
    TEST_ASSERT_EQUAL(0, (long)s.limits.d7Reset);
    TEST_ASSERT_EQUAL(1, s.count);
    const Session &z = s.sessions[0];
    TEST_ASSERT_EQUAL_STRING("z", z.id);
    TEST_ASSERT_EQUAL_STRING("", z.project);
    TEST_ASSERT_EQUAL_STRING("", z.model);
    TEST_ASSERT_EQUAL_STRING("", z.tool);
    TEST_ASSERT_EQUAL((int)State::Working, (int)z.state);
    TEST_ASSERT_EQUAL(0, (long)z.since);
    TEST_ASSERT_EQUAL(-1, z.ctx);
}

void test_parse_truncates_long_strings_and_caps_sessions() {
    static const char LONG[] = "pppppppppppppppppppppppppppppppppppppppppppppp";  // 46 > 32
    char buf[8192];
    size_t n = (size_t)snprintf(buf, sizeof buf, "{\"host\":\"h\",\"ts\":1,\"sessions\":[");
    for (int i = 0; i < 20; i++)
        n += (size_t)snprintf(buf + n, sizeof buf - n,
                              "%s{\"id\":\"s%d\",\"project\":\"%s\",\"state\":\"working\",\"since\":1}",
                              i ? "," : "", i, LONG);
    n += (size_t)snprintf(buf + n, sizeof buf - n, "]}");
    TEST_ASSERT_TRUE_MESSAGE(n < sizeof buf, "buffer de test trop petit");

    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(buf, n, s));
    TEST_ASSERT_EQUAL(MAX_SESSIONS, s.count);
    TEST_ASSERT_EQUAL_STRING("s11", s.sessions[MAX_SESSIONS - 1].id);
    TEST_ASSERT_EQUAL(sizeof(s.sessions[0].project) - 1, strlen(s.sessions[0].project));
    TEST_ASSERT_EQUAL(0, strncmp(LONG, s.sessions[0].project, sizeof(s.sessions[0].project) - 1));
}

void test_unknown_state_maps_to_idle() {
    const char *j = "{\"host\":\"h\",\"ts\":1,\"sessions\":[{\"id\":\"x\",\"state\":\"zzz\",\"since\":1}]}";
    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(j, strlen(j), s));
    TEST_ASSERT_EQUAL((int)State::Idle, (int)s.sessions[0].state);
}

void test_wrong_types_are_ignored() {
    // Valeurs de mauvais type : chaines vides / valeurs par defaut, sans planter.
    const char *j =
        "{\"host\":\"h\",\"ts\":\"x\",\"limits\":{\"h5\":\"a\",\"d7\":[1]},"
        "\"sessions\":[{\"id\":\"i\",\"project\":123,\"model\":null,\"state\":5,"
        "\"since\":\"z\",\"ctx\":{},\"tool\":true}]}";
    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(j, strlen(j), s));
    TEST_ASSERT_EQUAL(0, (long)s.ts);
    TEST_ASSERT_EQUAL(-1, s.limits.h5);
    TEST_ASSERT_EQUAL(-1, s.limits.d7);
    TEST_ASSERT_EQUAL(1, s.count);
    TEST_ASSERT_EQUAL_STRING("i", s.sessions[0].id);
    TEST_ASSERT_EQUAL_STRING("", s.sessions[0].project);
    TEST_ASSERT_EQUAL_STRING("", s.sessions[0].model);
    TEST_ASSERT_EQUAL_STRING("", s.sessions[0].tool);
    TEST_ASSERT_EQUAL((int)State::Idle, (int)s.sessions[0].state);
    TEST_ASSERT_EQUAL(0, (long)s.sessions[0].since);
    TEST_ASSERT_EQUAL(-1, s.sessions[0].ctx);
}

void test_host_not_string_or_sessions_not_array() {
    const char *j1 = "{\"host\":42,\"ts\":1,\"sessions\":[]}";
    HostSnapshot s;
    TEST_ASSERT_FALSE(parseSnapshot(j1, strlen(j1), s));
    const char *j2 = "{\"host\":\"h\",\"ts\":1,\"sessions\":{\"id\":\"x\"}}";
    TEST_ASSERT_TRUE(parseSnapshot(j2, strlen(j2), s));
    TEST_ASSERT_EQUAL(0, s.count);
}

void test_percentages_clamped() {
    const char *j =
        "{\"host\":\"h\",\"ts\":1,\"limits\":{\"h5\":250,\"d7\":-40},"
        "\"sessions\":[{\"id\":\"x\",\"state\":\"idle\",\"since\":1,\"ctx\":999}]}";
    HostSnapshot s;
    TEST_ASSERT_TRUE(parseSnapshot(j, strlen(j), s));
    TEST_ASSERT_EQUAL(100, s.limits.h5);
    TEST_ASSERT_EQUAL(-1, s.limits.d7);
    TEST_ASSERT_EQUAL(100, s.sessions[0].ctx);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_full);
    RUN_TEST(test_parse_no_limits);
    RUN_TEST(test_parse_invalid_json);
    RUN_TEST(test_parse_missing_host);
    RUN_TEST(test_parse_failure_leaves_output_untouched);
    RUN_TEST(test_host_of_32_chars_kept_intact);
    RUN_TEST(test_invalid_session_entries_skipped);
    RUN_TEST(test_reparse_resets_previous_content);
    RUN_TEST(test_parse_truncates_long_strings_and_caps_sessions);
    RUN_TEST(test_unknown_state_maps_to_idle);
    RUN_TEST(test_wrong_types_are_ignored);
    RUN_TEST(test_host_not_string_or_sessions_not_array);
    RUN_TEST(test_percentages_clamped);
    return UNITY_END();
}
```

**Step 2: Vérifier l'échec**

Run: `cd $FW && $PIO test -e native -f test_parse`
Expected: FAIL (compilation : `dash_model.h` introuvable).

**Step 3: `dash_model.h`** (toute l'API du modèle, les fonctions des Tasks 10-11 sont déclarées ici dès maintenant)

```cpp
// Modele pur du tableau de bord (sans Arduino ni LVGL) : testable en natif.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace dash {

constexpr int MAX_SESSIONS = 12;
constexpr int MAX_HOSTS = 4;
constexpr int MAX_ROWS = MAX_SESSIONS * MAX_HOSTS;
constexpr int64_t STALE_AFTER_S = 180;
// Hote muet depuis plus longtemps : son slot peut etre repris par un nouvel hote.
constexpr int64_t EVICT_AFTER_S = 6 * 3600;

enum class State : uint8_t { Working, Idle, Permission };
enum class Alert : uint8_t { None, Idle, Permission };

struct Limits {
    int h5 = -1;  // pourcentage 0..100, -1 si inconnu
    int64_t h5Reset = 0;
    int d7 = -1;
    int64_t d7Reset = 0;
};

struct Session {
    char id[16] = "";
    char project[33] = "";
    char model[25] = "";
    char tool[25] = "";
    State state = State::Idle;
    int64_t since = 0;
    int ctx = -1;  // pourcentage 0..100, -1 si inconnu
};

struct HostSnapshot {
    char host[33] = "";  // HOST_MAX = 32 cote serveur
    int64_t ts = 0;
    Limits limits;
    Session sessions[MAX_SESSIONS];
    int count = 0;
};

// Ligne affichable : une session et le nom de son hote
struct Row {
    const Session *session;
    const char *host;
};

// Parse un snapshot JSON. En cas d'echec (JSON invalide, host absent),
// retourne false et laisse `out` intact. Les sessions qui ne sont pas des
// objets ou sans "id" sont ignorees. Seules sessions[0..count) sont valides.
bool parseSnapshot(const char *json, size_t len, HostSnapshot &out);

class Dashboard {
public:
    // Integre un snapshot recu a `now` (epoch local). Retourne l'alerte a jouer
    // (aucune si le snapshot precedent de cet hote etait deja perime).
    // Nouvel hote avec MAX_HOSTS deja connus : reprend le slot de l'hote le plus
    // vieux si son age depasse EVICT_AFTER_S, sinon il est ignore.
    Alert apply(const HostSnapshot &snap, int64_t now);
    // Oublie un hote (payload retained vide : topic efface). Retourne true s'il
    // etait connu. Les hotes suivants sont decales (ordre conserve).
    bool removeHost(const char *host);
    // Lignes triees par urgence (permission > idle > working) puis anciennete
    // (since croissant ; since = 0, inconnu, passe en tete de son groupe).
    // Les pointeurs des Row ne sont valides que jusqu'au prochain apply() ou
    // removeHost() : ne pas les conserver.
    int rows(Row *out, int max) const;
    // Quotas du snapshot le plus recent.
    const Limits *limits() const;
    int hostCount() const { return count_; }
    const char *hostName(int i) const { return hosts_[i].snap.host; }
    // Age maximal parmi les hotes (0 si aucun) : now - snap.ts, ou now - reception
    // si ts inconnu ; borne a 0.
    int64_t staleSeconds(int64_t now) const;
    bool anyWaiting() const;

private:
    struct HostEntry {
        HostSnapshot snap;
        int64_t receivedAt = 0;
    };
    HostEntry hosts_[MAX_HOSTS];
    int count_ = 0;
};

// Helpers d'affichage
void formatDuration(int64_t seconds, char *out, size_t size);  // "0:42", "5:10", "1h05", "2j"
uint32_t colorForPercent(int pct);                             // vert / orange / rouge
const char *stateLabel(State s);                                // "TRAVAILLE", "ATTENTE", "PERMISSION"

// Veille : ecran allume si une session attend, ou activite recente (ms).
bool screenShouldBeOn(bool anyWaiting, uint32_t nowMs, uint32_t lastActivityMs,
                      uint32_t timeoutMs = 10UL * 60UL * 1000UL);

}  // namespace dash
```

**Step 4: `dash_model.cpp` — parsing** (le reste en stubs `return {}`/`return 0` pour compiler). Remise à zéro en place plutôt que `out = HostSnapshot()` : le temporaire portait la frame à ~1.7 Ko sur xtensa, sur la pile de 8 Ko de la loop task (callback MQTT).

```cpp
#include "dash_model.h"

#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>

namespace dash {

// Copie tronquee ; nullptr (absent ou pas une chaine) -> ""
static void copyStr(char *dst, size_t size, const char *src) {
    if (!src) src = "";
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

static State parseState(const char *s) {
    if (s && strcmp(s, "working") == 0) return State::Working;
    if (s && strcmp(s, "permission") == 0) return State::Permission;
    return State::Idle;
}

// Pourcentage 0..100 ; absent, pas un entier ou negatif -> -1
static int parsePercent(JsonVariantConst v) {
    if (!v.is<int>()) return -1;
    int p = v.as<int>();
    if (p < 0) return -1;
    return p > 100 ? 100 : p;
}

// Epoch en secondes ; absent, pas un entier ou negatif -> 0
static int64_t parseEpoch(JsonVariantConst v) {
    int64_t t = v | (int64_t)0;
    return t < 0 ? 0 : t;
}

bool parseSnapshot(const char *json, size_t len, HostSnapshot &out) {
    JsonDocument doc;
    if (deserializeJson(doc, json, len)) return false;
    const char *host = doc["host"];
    if (!host || !*host) return false;

    // Remise a zero en place (pas de HostSnapshot temporaire de ~1.3 Ko sur
    // la pile du callback MQTT) : count borne les lectures et chaque slot
    // utilise est entierement reecrit.
    copyStr(out.host, sizeof out.host, host);
    out.ts = parseEpoch(doc["ts"]);
    out.limits = Limits();
    out.count = 0;

    JsonObjectConst lim = doc["limits"];
    if (!lim.isNull()) {
        out.limits.h5 = parsePercent(lim["h5"]);
        out.limits.h5Reset = parseEpoch(lim["h5_reset"]);
        out.limits.d7 = parsePercent(lim["d7"]);
        out.limits.d7Reset = parseEpoch(lim["d7_reset"]);
    }

    for (JsonVariantConst v : doc["sessions"].as<JsonArrayConst>()) {
        if (out.count >= MAX_SESSIONS) break;
        JsonObjectConst js = v.as<JsonObjectConst>();
        const char *id = js["id"];
        if (!id || !*id) continue;  // pas un objet, ou id absent / vide
        Session &s = out.sessions[out.count++];
        copyStr(s.id, sizeof s.id, id);
        copyStr(s.project, sizeof s.project, js["project"]);
        copyStr(s.model, sizeof s.model, js["model"]);
        copyStr(s.tool, sizeof s.tool, js["tool"]);
        s.state = parseState(js["state"]);
        s.since = parseEpoch(js["since"]);
        s.ctx = parsePercent(js["ctx"]);
    }
    return true;
}

// ... stubs des Tasks 10-11 ...

}  // namespace dash
```

**Step 5: Vérifier** — `$PIO test -e native -f test_parse` → 13 tests PASS.

**Step 6: Commit**

```bash
git add $FW/lib $FW/test
git commit -m "Claude_Dashboard: parsing du snapshot MQTT (modele natif teste)"
```

---

### Task 10: `dash_model` — agrégation, tri, transitions, staleness

**Files:**
- Modify: `$FW/lib/dash_model/dash_model.cpp`
- Test: `$FW/test/test_dashboard/test_main.cpp`

Règles des alertes :
- alerte uniquement si la session était **déjà connue** avec un autre état (pas de bip au démarrage sur le message retained, ni pour une nouvelle session) ;
- passage vers `Permission` → `Alert::Permission` ; passage vers `Idle` depuis `Working` ou `Permission` → `Alert::Idle` ;
- plusieurs transitions dans un même snapshot : `Permission` l'emporte ;
- aucune alerte si le snapshot précédent de cet hôte était déjà périmé (âge > `STALE_AFTER_S` à l'arrivée du nouveau) : pas de salve de bips au retour du serveur, l'utilisateur ne suivait plus ces états.
- une session absente du snapshot précédent (nouvelle, ou disparue puis revenue) ne déclenche pas d'alerte.

Hôtes morts : `removeHost(host)` oublie un hôte et compacte le tableau (appelé par `main.cpp` sur un payload retained vide, topic effacé). Si un nouvel hôte arrive alors que `MAX_HOSTS` sont connus, il reprend le slot de l'hôte le plus vieux si son âge dépasse `EVICT_AFTER_S` (6 h), sinon il est ignoré. Les pointeurs renvoyés par `rows()` ne sont valides que jusqu'au prochain `apply()`/`removeHost()`. Dans le tri, `since = 0` (inconnu) passe en tête de son groupe d'urgence.

Staleness : l'âge d'un hôte se mesure depuis le `ts` du snapshot (heure du serveur) et non depuis sa réception. Au démarrage de l'écran, le broker livre le message **retained** d'un agent peut-être mort depuis longtemps ; jugé à la réception, il paraîtrait frais pendant 3 min. Règle : `age = now - (snap.ts > 0 ? snap.ts : receivedAt)`, borné à 0 (horloge décalée ou non synchronisée) ; `staleSeconds` retourne le max sur les hôtes (0 si aucun). `receivedAt` reste le repli quand `ts` est absent.

(Écart par rapport à la première version du plan : staleness sur `ts`, pas d'alerte après un snapshot périmé, tri stable avec le comparateur défini hors de la boucle ; tests ajoutés pour ces règles, la borne `age == STALE_AFTER_S`, la stabilité du tri et le plafond `max` de `rows()` ; suite à la revue : `removeHost`, éviction des hôtes muets depuis 6 h, `break` sur l'hôte trouvé, tests permission→idle, 5e hôte ignoré, session disparue puis revenue, `limits()` ignorant un hôte plus récent sans quotas.)

**Step 1: Tests** (helper `mk(host, {id,state,since}...)` construisant un `HostSnapshot` à la main)

```cpp
#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "dash_model.h"

using namespace dash;

static HostSnapshot mk(const char *host, int n, const char *const ids[], const State states[],
                       const int64_t since[]) {
    HostSnapshot s;
    strcpy(s.host, host);
    s.count = n;
    for (int i = 0; i < n; i++) {
        strcpy(s.sessions[i].id, ids[i]);
        s.sessions[i].state = states[i];
        s.sessions[i].since = since[i];
    }
    return s;
}

void setUp() {}
void tearDown() {}

void test_no_alert_on_first_snapshot() {
    Dashboard d;
    const char *ids[] = {"a"};
    State st[] = {State::Permission};
    int64_t since[] = {10};
    TEST_ASSERT_EQUAL((int)Alert::None, (int)d.apply(mk("h", 1, ids, st, since), 100));
}

void test_alert_on_transition_to_permission() {
    Dashboard d;
    const char *ids[] = {"a"};
    State w[] = {State::Working}, p[] = {State::Permission};
    int64_t since[] = {10};
    d.apply(mk("h", 1, ids, w, since), 100);
    TEST_ASSERT_EQUAL((int)Alert::Permission, (int)d.apply(mk("h", 1, ids, p, since), 102));
    // meme etat re-publie : pas de nouveau bip
    TEST_ASSERT_EQUAL((int)Alert::None, (int)d.apply(mk("h", 1, ids, p, since), 104));
}

void test_alert_idle_after_working() {
    Dashboard d;
    const char *ids[] = {"a"};
    State w[] = {State::Working}, i[] = {State::Idle};
    int64_t since[] = {10};
    d.apply(mk("h", 1, ids, w, since), 100);
    TEST_ASSERT_EQUAL((int)Alert::Idle, (int)d.apply(mk("h", 1, ids, i, since), 102));
}

void test_new_session_no_alert() {
    Dashboard d;
    const char *ids1[] = {"a"}, *ids2[] = {"a", "b"};
    State s1[] = {State::Working}, s2[] = {State::Working, State::Idle};
    int64_t t1[] = {1}, t2[] = {1, 2};
    d.apply(mk("h", 1, ids1, s1, t1), 100);
    TEST_ASSERT_EQUAL((int)Alert::None, (int)d.apply(mk("h", 2, ids2, s2, t2), 102));
}

void test_permission_wins_over_idle() {
    Dashboard d;
    const char *ids[] = {"a", "b"};
    State before[] = {State::Working, State::Working};
    State after[] = {State::Idle, State::Permission};
    int64_t t[] = {1, 1};
    d.apply(mk("h", 2, ids, before, t), 100);
    TEST_ASSERT_EQUAL((int)Alert::Permission, (int)d.apply(mk("h", 2, ids, after, t), 102));
}

void test_no_alert_when_previous_snapshot_was_stale() {
    // Retour du serveur apres une longue coupure : pas de salve de bips.
    Dashboard d;
    const char *ids[] = {"a"};
    State w[] = {State::Working}, p[] = {State::Permission}, i[] = {State::Idle};
    int64_t t[] = {1};
    HostSnapshot s = mk("h", 1, ids, w, t);
    s.ts = 100;
    d.apply(s, 100);
    s = mk("h", 1, ids, p, t);
    s.ts = 100 + STALE_AFTER_S + 1;
    TEST_ASSERT_EQUAL((int)Alert::None, (int)d.apply(s, 100 + STALE_AFTER_S + 1));
    // le snapshot suivant est frais : les transitions alertent a nouveau
    s = mk("h", 1, ids, i, t);
    s.ts = 100 + STALE_AFTER_S + 3;
    TEST_ASSERT_EQUAL((int)Alert::Idle, (int)d.apply(s, 100 + STALE_AFTER_S + 3));
}

void test_alert_when_previous_snapshot_at_stale_limit() {
    // age == STALE_AFTER_S : pas encore perime
    Dashboard d;
    const char *ids[] = {"a"};
    State w[] = {State::Working}, p[] = {State::Permission};
    int64_t t[] = {1};
    HostSnapshot s = mk("h", 1, ids, w, t);
    s.ts = 100;
    d.apply(s, 100);
    s = mk("h", 1, ids, p, t);
    s.ts = 100 + STALE_AFTER_S;
    TEST_ASSERT_EQUAL((int)Alert::Permission, (int)d.apply(s, 100 + STALE_AFTER_S));
}

void test_rows_sorted_by_urgency_then_age() {
    Dashboard d;
    const char *ids[] = {"w", "i_new", "p", "i_old"};
    State st[] = {State::Working, State::Idle, State::Permission, State::Idle};
    int64_t since[] = {50, 90, 80, 20};
    d.apply(mk("h", 4, ids, st, since), 100);
    Row rows[MAX_ROWS];
    int n = d.rows(rows, MAX_ROWS);
    TEST_ASSERT_EQUAL(4, n);
    TEST_ASSERT_EQUAL_STRING("p", rows[0].session->id);
    TEST_ASSERT_EQUAL_STRING("i_old", rows[1].session->id);
    TEST_ASSERT_EQUAL_STRING("i_new", rows[2].session->id);
    TEST_ASSERT_EQUAL_STRING("w", rows[3].session->id);
    TEST_ASSERT_EQUAL_STRING("h", rows[0].host);
}

void test_rows_sort_is_stable() {
    // meme urgence et meme anciennete : ordre d'origine conserve
    Dashboard d;
    const char *ids[] = {"x1", "w", "x2", "x3"};
    State st[] = {State::Idle, State::Working, State::Idle, State::Idle};
    int64_t since[] = {5, 1, 5, 5};
    d.apply(mk("h", 4, ids, st, since), 100);
    Row rows[MAX_ROWS];
    TEST_ASSERT_EQUAL(4, d.rows(rows, MAX_ROWS));
    TEST_ASSERT_EQUAL_STRING("x1", rows[0].session->id);
    TEST_ASSERT_EQUAL_STRING("x2", rows[1].session->id);
    TEST_ASSERT_EQUAL_STRING("x3", rows[2].session->id);
    TEST_ASSERT_EQUAL_STRING("w", rows[3].session->id);
}

void test_rows_capped_by_max() {
    Dashboard d;
    const char *ids[] = {"a", "b", "c"};
    State st[] = {State::Idle, State::Idle, State::Idle};
    int64_t since[] = {1, 2, 3};
    d.apply(mk("h", 3, ids, st, since), 100);
    Row rows[2];
    TEST_ASSERT_EQUAL(2, d.rows(rows, 2));
}

void test_multi_host_and_latest_limits() {
    Dashboard d;
    const char *ids[] = {"a"};
    State st[] = {State::Idle};
    int64_t t[] = {1};
    HostSnapshot h1 = mk("h1", 1, ids, st, t), h2 = mk("h2", 1, ids, st, t);
    h1.ts = 100; h1.limits.h5 = 10;
    h2.ts = 200; h2.limits.h5 = 20;
    d.apply(h1, 100);
    d.apply(h2, 200);
    TEST_ASSERT_EQUAL(2, d.hostCount());
    TEST_ASSERT_EQUAL(20, d.limits()->h5);
    Row rows[MAX_ROWS];
    TEST_ASSERT_EQUAL(2, d.rows(rows, MAX_ROWS));
}

void test_limits_null_when_never_received() {
    Dashboard d;
    TEST_ASSERT_NULL(d.limits());
}

void test_stale_seconds() {
    // snapshot sans ts : repli sur l'heure de reception
    Dashboard d;
    TEST_ASSERT_EQUAL(0, (long)d.staleSeconds(1000));
    HostSnapshot s;
    strcpy(s.host, "h");
    d.apply(s, 1000);
    TEST_ASSERT_EQUAL(200, (long)d.staleSeconds(1200));
}

void test_stale_seconds_uses_snapshot_ts() {
    // message retained d'un agent mort depuis longtemps, recu maintenant : perime
    Dashboard d;
    HostSnapshot s;
    strcpy(s.host, "h");
    s.ts = 100;
    d.apply(s, 1000);
    TEST_ASSERT_EQUAL(900, (long)d.staleSeconds(1000));
}

void test_stale_seconds_future_ts_clamped() {
    // horloge non synchronisee ou decalee : jamais d'age negatif
    Dashboard d;
    HostSnapshot s;
    strcpy(s.host, "h");
    s.ts = 2000;
    d.apply(s, 1000);
    TEST_ASSERT_EQUAL(0, (long)d.staleSeconds(1000));
}

void test_stale_seconds_max_over_hosts() {
    Dashboard d;
    HostSnapshot s;
    strcpy(s.host, "h1");
    s.ts = 900;
    d.apply(s, 1000);
    strcpy(s.host, "h2");
    s.ts = 500;
    d.apply(s, 1000);
    TEST_ASSERT_EQUAL(500, (long)d.staleSeconds(1000));
}

void test_any_waiting() {
    Dashboard d;
    const char *ids[] = {"a"};
    State w[] = {State::Working}, i[] = {State::Idle};
    int64_t t[] = {1};
    d.apply(mk("h", 1, ids, w, t), 1);
    TEST_ASSERT_FALSE(d.anyWaiting());
    d.apply(mk("h", 1, ids, i, t), 2);
    TEST_ASSERT_TRUE(d.anyWaiting());
}

void test_alert_idle_after_permission() {
    Dashboard d;
    const char *ids[] = {"a"};
    State p[] = {State::Permission}, i[] = {State::Idle};
    int64_t since[] = {10};
    d.apply(mk("h", 1, ids, p, since), 100);
    TEST_ASSERT_EQUAL((int)Alert::Idle, (int)d.apply(mk("h", 1, ids, i, since), 102));
}

void test_session_gone_then_back_no_alert() {
    // une session absente du snapshot precedent est traitee comme nouvelle
    Dashboard d;
    const char *ids[] = {"a"};
    State w[] = {State::Working}, p[] = {State::Permission};
    int64_t t[] = {1};
    d.apply(mk("h", 1, ids, w, t), 100);
    d.apply(mk("h", 0, ids, w, t), 102);
    TEST_ASSERT_EQUAL((int)Alert::None, (int)d.apply(mk("h", 1, ids, p, t), 104));
}

void test_limits_skip_host_without_limits() {
    Dashboard d;
    HostSnapshot s;
    strcpy(s.host, "old");
    s.ts = 100;
    s.limits.d7 = 30;
    d.apply(s, 100);
    HostSnapshot n;
    strcpy(n.host, "new");
    n.ts = 200;  // plus recent, mais sans quotas
    d.apply(n, 200);
    TEST_ASSERT_NOT_NULL(d.limits());
    TEST_ASSERT_EQUAL(30, d.limits()->d7);
}

// Remplit le tableau de bord avec MAX_HOSTS hotes "h0".."h3" de ts donnes.
static void fillHosts(Dashboard &d, const int64_t ts[MAX_HOSTS], int64_t now) {
    for (int i = 0; i < MAX_HOSTS; i++) {
        HostSnapshot s;
        snprintf(s.host, sizeof s.host, "h%d", i);
        s.ts = ts[i];
        d.apply(s, now);
    }
}

void test_extra_host_ignored_when_none_evictable() {
    static Dashboard d;  // ~5.6 Ko : hors pile
    const int64_t now = 100000;
    const int64_t ts[MAX_HOSTS] = {now, now - 60, now - EVICT_AFTER_S, now - 10};
    fillHosts(d, ts, now);
    HostSnapshot s;
    strcpy(s.host, "extra");
    s.ts = now;
    TEST_ASSERT_EQUAL((int)Alert::None, (int)d.apply(s, now));
    TEST_ASSERT_EQUAL(MAX_HOSTS, d.hostCount());
    for (int i = 0; i < MAX_HOSTS; i++) TEST_ASSERT_NOT_EQUAL(0, strcmp("extra", d.hostName(i)));
}

void test_extra_host_evicts_oldest_dead_host() {
    static Dashboard d;  // ~5.6 Ko : hors pile
    const int64_t now = 100000;
    // h1 et h2 morts depuis plus de 6 h ; h2 est le plus vieux
    const int64_t ts[MAX_HOSTS] = {now, now - EVICT_AFTER_S - 10, now - EVICT_AFTER_S - 500, now};
    fillHosts(d, ts, now);
    HostSnapshot s;
    strcpy(s.host, "extra");
    s.ts = now;
    d.apply(s, now);
    TEST_ASSERT_EQUAL(MAX_HOSTS, d.hostCount());
    TEST_ASSERT_EQUAL_STRING("h0", d.hostName(0));
    TEST_ASSERT_EQUAL_STRING("h1", d.hostName(1));
    TEST_ASSERT_EQUAL_STRING("extra", d.hostName(2));
    TEST_ASSERT_EQUAL_STRING("h3", d.hostName(3));
}

void test_remove_host_compacts() {
    static Dashboard d;  // ~5.6 Ko : hors pile
    const char *ids[] = {"a"};
    State st[] = {State::Idle};
    int64_t t[] = {1};
    d.apply(mk("h1", 1, ids, st, t), 100);
    d.apply(mk("h2", 1, ids, st, t), 100);
    d.apply(mk("h3", 1, ids, st, t), 100);
    TEST_ASSERT_TRUE(d.removeHost("h2"));
    TEST_ASSERT_EQUAL(2, d.hostCount());
    TEST_ASSERT_EQUAL_STRING("h1", d.hostName(0));
    TEST_ASSERT_EQUAL_STRING("h3", d.hostName(1));
    Row rows[MAX_ROWS];
    TEST_ASSERT_EQUAL(2, d.rows(rows, MAX_ROWS));
    TEST_ASSERT_FALSE(d.removeHost("h2"));
    TEST_ASSERT_FALSE(d.removeHost("zzz"));
    TEST_ASSERT_TRUE(d.removeHost("h1"));
    TEST_ASSERT_TRUE(d.removeHost("h3"));
    TEST_ASSERT_EQUAL(0, d.hostCount());
    TEST_ASSERT_EQUAL(0, (long)d.staleSeconds(1000));
}

void test_removed_host_comes_back_without_alert() {
    Dashboard d;
    const char *ids[] = {"a"};
    State w[] = {State::Working}, p[] = {State::Permission};
    int64_t t[] = {1};
    d.apply(mk("h", 1, ids, w, t), 100);
    TEST_ASSERT_TRUE(d.removeHost("h"));
    TEST_ASSERT_EQUAL((int)Alert::None, (int)d.apply(mk("h", 1, ids, p, t), 102));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_no_alert_on_first_snapshot);
    RUN_TEST(test_alert_on_transition_to_permission);
    RUN_TEST(test_alert_idle_after_working);
    RUN_TEST(test_new_session_no_alert);
    RUN_TEST(test_permission_wins_over_idle);
    RUN_TEST(test_no_alert_when_previous_snapshot_was_stale);
    RUN_TEST(test_alert_when_previous_snapshot_at_stale_limit);
    RUN_TEST(test_rows_sorted_by_urgency_then_age);
    RUN_TEST(test_rows_sort_is_stable);
    RUN_TEST(test_rows_capped_by_max);
    RUN_TEST(test_multi_host_and_latest_limits);
    RUN_TEST(test_limits_null_when_never_received);
    RUN_TEST(test_stale_seconds);
    RUN_TEST(test_stale_seconds_uses_snapshot_ts);
    RUN_TEST(test_stale_seconds_future_ts_clamped);
    RUN_TEST(test_stale_seconds_max_over_hosts);
    RUN_TEST(test_any_waiting);
    RUN_TEST(test_alert_idle_after_permission);
    RUN_TEST(test_session_gone_then_back_no_alert);
    RUN_TEST(test_limits_skip_host_without_limits);
    RUN_TEST(test_extra_host_ignored_when_none_evictable);
    RUN_TEST(test_extra_host_evicts_oldest_dead_host);
    RUN_TEST(test_remove_host_compacts);
    RUN_TEST(test_removed_host_comes_back_without_alert);
    return UNITY_END();
}
```

**Step 2: Vérifier l'échec** — `$PIO test -e native -f test_dashboard` → FAIL (stubs).

**Step 3: Implémentation** (dans `dash_model.cpp`, avant la fin du namespace)

```cpp
static int urgency(State s) {
    switch (s) {
        case State::Permission: return 0;
        case State::Idle: return 1;
        default: return 2;
    }
}

static const Session *findSession(const HostSnapshot &snap, const char *id) {
    for (int i = 0; i < snap.count; i++)
        if (strcmp(snap.sessions[i].id, id) == 0) return &snap.sessions[i];
    return nullptr;
}

// Age d'un snapshot : depuis son ts (heure du serveur) s'il est connu, sinon
// depuis sa reception. Un message retained d'un agent mort depuis longtemps
// est ainsi perime des sa reception. Borne a 0 (horloge decalee ou non
// synchronisee).
static int64_t snapshotAge(const HostSnapshot &snap, int64_t receivedAt, int64_t now) {
    int64_t age = now - (snap.ts > 0 ? snap.ts : receivedAt);
    return age < 0 ? 0 : age;
}

Alert Dashboard::apply(const HostSnapshot &snap, int64_t now) {
    int idx = -1;
    for (int i = 0; i < count_; i++)
        if (strcmp(hosts_[i].snap.host, snap.host) == 0) {
            idx = i;
            break;
        }

    Alert alert = Alert::None;
    if (idx >= 0) {
        const HostSnapshot &prev = hosts_[idx].snap;
        // Snapshot precedent deja perime : ses etats n'etaient plus fiables,
        // pas de salve de bips au retour du serveur.
        bool prevStale = snapshotAge(prev, hosts_[idx].receivedAt, now) > STALE_AFTER_S;
        for (int i = 0; i < snap.count && !prevStale; i++) {
            const Session &cur = snap.sessions[i];
            const Session *old = findSession(prev, cur.id);
            if (!old || old->state == cur.state) continue;
            if (cur.state == State::Permission) alert = Alert::Permission;
            else if (cur.state == State::Idle && alert == Alert::None) alert = Alert::Idle;
        }
    } else if (count_ < MAX_HOSTS) {
        idx = count_++;
    } else {
        // Tableau plein : reprendre le slot de l'hote muet depuis le plus
        // longtemps, s'il l'est depuis plus de EVICT_AFTER_S.
        int64_t oldest = EVICT_AFTER_S;
        for (int i = 0; i < count_; i++) {
            int64_t age = snapshotAge(hosts_[i].snap, hosts_[i].receivedAt, now);
            if (age > oldest) {
                oldest = age;
                idx = i;
            }
        }
        if (idx < 0) return Alert::None;  // hote ignore
    }
    hosts_[idx].snap = snap;
    hosts_[idx].receivedAt = now;
    return alert;
}

bool Dashboard::removeHost(const char *host) {
    for (int i = 0; i < count_; i++) {
        if (strcmp(hosts_[i].snap.host, host) != 0) continue;
        for (int j = i + 1; j < count_; j++) hosts_[j - 1] = hosts_[j];
        count_--;
        return true;
    }
    return false;
}

int Dashboard::rows(Row *out, int max) const {
    int n = 0;
    for (int h = 0; h < count_; h++)
        for (int i = 0; i < hosts_[h].snap.count && n < max; i++)
            out[n++] = {&hosts_[h].snap.sessions[i], hosts_[h].snap.host};

    // Ordre strict : a egalite, l'ordre d'origine est conserve (tri stable).
    // since = 0 (inconnu) passe en tete de son groupe d'urgence.
    auto before = [](const Row &a, const Row &b) {
        int ua = urgency(a.session->state), ub = urgency(b.session->state);
        return ua != ub ? ua < ub : a.session->since < b.session->since;
    };
    // Tri par insertion (n <= MAX_ROWS = 48)
    for (int i = 1; i < n; i++) {
        Row r = out[i];
        int j = i - 1;
        while (j >= 0 && before(r, out[j])) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = r;
    }
    return n;
}

const Limits *Dashboard::limits() const {
    const HostSnapshot *best = nullptr;
    for (int i = 0; i < count_; i++) {
        const HostSnapshot &s = hosts_[i].snap;
        if (s.limits.h5 < 0 && s.limits.d7 < 0) continue;
        if (!best || s.ts > best->ts) best = &s;
    }
    return best ? &best->limits : nullptr;
}

int64_t Dashboard::staleSeconds(int64_t now) const {
    int64_t worst = 0;
    for (int i = 0; i < count_; i++) {
        int64_t age = snapshotAge(hosts_[i].snap, hosts_[i].receivedAt, now);
        if (age > worst) worst = age;
    }
    return worst;
}

bool Dashboard::anyWaiting() const {
    for (int h = 0; h < count_; h++)
        for (int i = 0; i < hosts_[h].snap.count; i++)
            if (hosts_[h].snap.sessions[i].state != State::Working) return true;
    return false;
}
```

**Step 4: Vérifier** — `$PIO test -e native` → tous les tests PASS (test_parse + test_dashboard).

**Step 5: Commit**

```bash
git add $FW/lib $FW/test
git commit -m "Claude_Dashboard: agregation multi-hote, tri et detection des transitions"
```

---

### Task 11: `dash_model` — helpers d'affichage et veille

**Files:**
- Modify: `$FW/lib/dash_model/dash_model.cpp`
- Test: `$FW/test/test_helpers/test_main.cpp`

**Step 1: Tests**

```cpp
#include <unity.h>
#include "dash_model.h"

using namespace dash;

void setUp() {}
void tearDown() {}

void test_format_duration() {
    char b[16];
    formatDuration(0, b, sizeof b);      TEST_ASSERT_EQUAL_STRING("0:00", b);
    formatDuration(42, b, sizeof b);     TEST_ASSERT_EQUAL_STRING("0:42", b);
    formatDuration(310, b, sizeof b);    TEST_ASSERT_EQUAL_STRING("5:10", b);
    formatDuration(3599, b, sizeof b);   TEST_ASSERT_EQUAL_STRING("59:59", b);
    formatDuration(3900, b, sizeof b);   TEST_ASSERT_EQUAL_STRING("1h05", b);
    formatDuration(200000, b, sizeof b); TEST_ASSERT_EQUAL_STRING("2j", b);
    formatDuration(-5, b, sizeof b);     TEST_ASSERT_EQUAL_STRING("0:00", b);
}

void test_color_thresholds() {
    TEST_ASSERT_EQUAL_HEX32(0x4caf50, colorForPercent(0));
    TEST_ASSERT_EQUAL_HEX32(0x4caf50, colorForPercent(59));
    TEST_ASSERT_EQUAL_HEX32(0xfca311, colorForPercent(60));
    TEST_ASSERT_EQUAL_HEX32(0xfca311, colorForPercent(84));
    TEST_ASSERT_EQUAL_HEX32(0xf72585, colorForPercent(85));
}

void test_state_labels() {
    TEST_ASSERT_EQUAL_STRING("PERMISSION", stateLabel(State::Permission));
    TEST_ASSERT_EQUAL_STRING("ATTENTE", stateLabel(State::Idle));
    TEST_ASSERT_EQUAL_STRING("TRAVAILLE", stateLabel(State::Working));
}

void test_screen_policy() {
    const uint32_t T = 600000;
    TEST_ASSERT_TRUE(screenShouldBeOn(true, 10 * T, 0));
    TEST_ASSERT_TRUE(screenShouldBeOn(false, T - 1, 0));
    TEST_ASSERT_FALSE(screenShouldBeOn(false, T, 0));
    // debordement de millis() (~49 j)
    TEST_ASSERT_TRUE(screenShouldBeOn(false, 100, 0xFFFFFF00u));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_format_duration);
    RUN_TEST(test_color_thresholds);
    RUN_TEST(test_state_labels);
    RUN_TEST(test_screen_policy);
    return UNITY_END();
}
```

**Step 2: Vérifier l'échec** — FAIL.

**Step 3: Implémentation**

```cpp
void formatDuration(int64_t s, char *out, size_t size) {
    if (s < 0) s = 0;
    if (s < 3600) snprintf(out, size, "%d:%02d", (int)(s / 60), (int)(s % 60));
    else if (s < 86400) snprintf(out, size, "%dh%02d", (int)(s / 3600), (int)(s % 3600 / 60));
    else if (s / 86400 <= 999) snprintf(out, size, "%dj", (int)(s / 86400));
    else snprintf(out, size, "999j+");  // since aberrant : borne aussi les casts int
}

uint32_t colorForPercent(int pct) {
    if (pct < 60) return 0x4caf50;
    if (pct < 85) return 0xfca311;
    return 0xf72585;
}

const char *stateLabel(State s) {
    switch (s) {
        case State::Permission: return "PERMISSION";
        case State::Idle: return "ATTENTE";
        default: return "TRAVAILLE";
    }
}

bool screenShouldBeOn(bool anyWaiting, uint32_t nowMs, uint32_t lastActivityMs,
                      uint32_t timeoutMs) {
    return anyWaiting || (uint32_t)(nowMs - lastActivityMs) < timeoutMs;
}
```

**Écarts à l'implémentation :** `formatDuration` borne l'affichage à `999j+`
(sinon `(int)(s / 86400)` déborde pour un `since` aberrant, ex. `INT64_MAX`) et
ne fait rien si `size == 0`. Tests ajoutés : bornes 3600/86399/86400, `999j`/`999j+`
/`INT64_MAX`/`INT64_MIN`, petit buffer (troncature snprintf), couleur à 100,
veille après débordement et timeout explicite. `colorForPercent(-1)` reste vert
(comportement du plan) : les appelants testent `< 0` (inconnu) avant.

**Step 4: Vérifier** — `$PIO test -e native` → tout PASS.

**Step 5: Commit**

```bash
git add $FW/lib $FW/test
git commit -m "Claude_Dashboard: helpers d'affichage et politique de veille"
```

---

### Task 12: Interface LVGL (`ui.cpp`)

UI séparée de `main.cpp` pour garder des fichiers lisibles. Toutes les fonctions `ui_*` supposent que l'appelant tient `bsp_display_lock`.

**Files:**
- Create: `$FW/src/ui.h`
- Create: `$FW/src/ui.cpp`
- Modify: `$FW/src/main.cpp`

**Step 1: `ui.h`**

```cpp
#pragma once
#include <time.h>
#include "dash_model.h"

void ui_create();
// Reconstruit quotas + cartes a partir du modele (appel a chaque snapshot).
void ui_render(const dash::Dashboard &d, time_t now);
// Met a jour horloge, chronos et bandeau (appel chaque seconde).
void ui_tick(const dash::Dashboard &d, time_t now, bool mqttOk);
```

**Step 2: `ui.cpp`** — structure (480×320, fond `0x0f1419`, cartes `0x1c2733`) :

```cpp
#include "ui.h"
#include <lvgl.h>
#include <stdio.h>

using namespace dash;

#define COLOR_BG      0x0f1419
#define COLOR_CARD    0x1c2733
#define COLOR_TEXT    0xffffff
#define COLOR_DIM     0x888888
#define COLOR_WORKING 0x4cc9f0
#define COLOR_IDLE    0xfca311
#define COLOR_PERM    0xf72585

static lv_obj_t *lblHost, *dotMqtt, *lblClock, *banner, *list, *lblEmpty;
static lv_obj_t *bar5h, *pct5h, *reset5h, *bar7d, *pct7d, *reset7d;

// Chronos des cartes, mis a jour chaque seconde sans reconstruire la liste
static lv_obj_t *durLabels[MAX_ROWS];
static int64_t durSince[MAX_ROWS];
static int durCount = 0;

static uint32_t stateColor(State s) {
    return s == State::Permission ? COLOR_PERM : s == State::Idle ? COLOR_IDLE : COLOR_WORKING;
}

static lv_obj_t *mkLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *mkBar(lv_obj_t *parent, int y, const char *name,
                       lv_obj_t **pct, lv_obj_t **reset) {
    lv_obj_t *l = mkLabel(parent, &lv_font_montserrat_16, COLOR_TEXT);
    lv_label_set_text(l, name);
    lv_obj_set_pos(l, 10, y);
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_set_size(b, 220, 14);
    lv_obj_set_pos(b, 45, y + 3);
    lv_bar_set_range(b, 0, 100);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2a3a4a), LV_PART_MAIN);
    *pct = mkLabel(parent, &lv_font_montserrat_16, COLOR_TEXT);
    lv_obj_set_pos(*pct, 275, y);
    *reset = mkLabel(parent, &lv_font_montserrat_14, COLOR_DIM);
    lv_obj_set_pos(*reset, 330, y + 1);
    return b;
}

void ui_create() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // En-tete (0-30)
    lv_obj_t *title = mkLabel(scr, &lv_font_montserrat_18, COLOR_TEXT);
    lv_label_set_text(title, "Claude Code");
    lv_obj_set_pos(title, 10, 6);
    lblHost = mkLabel(scr, &lv_font_montserrat_16, COLOR_DIM);
    lv_obj_set_pos(lblHost, 170, 7);
    dotMqtt = lv_obj_create(scr);
    lv_obj_set_size(dotMqtt, 12, 12);
    lv_obj_set_style_radius(dotMqtt, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dotMqtt, 0, 0);
    lv_obj_set_pos(dotMqtt, 370, 10);
    lblClock = mkLabel(scr, &lv_font_montserrat_18, COLOR_TEXT);
    lv_obj_set_pos(lblClock, 410, 6);

    // Quotas (34-80)
    bar5h = mkBar(scr, 36, "5h", &pct5h, &reset5h);
    bar7d = mkBar(scr, 60, "7j", &pct7d, &reset7d);

    // Liste des sessions (86-320), defilement vertical
    list = lv_obj_create(scr);
    lv_obj_set_pos(list, 0, 86);
    lv_obj_set_size(list, 480, 234);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 6, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    lblEmpty = mkLabel(scr, &lv_font_montserrat_18, COLOR_DIM);
    lv_label_set_text(lblEmpty, "En attente de donnees...");
    lv_obj_align(lblEmpty, LV_ALIGN_CENTER, 0, 60);

    // Bandeau d'alerte (serveur injoignable), cache par defaut
    banner = lv_label_create(scr);
    lv_obj_set_width(banner, 480);
    lv_obj_set_style_bg_color(banner, lv_color_hex(COLOR_PERM), 0);
    lv_obj_set_style_bg_opa(banner, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(banner, lv_color_white(), 0);
    lv_obj_set_style_text_align(banner, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_ver(banner, 4, 0);
    lv_obj_align(banner, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);
}

static void renderLimit(lv_obj_t *bar, lv_obj_t *pct, lv_obj_t *reset, int value,
                        int64_t resetAt, bool withDay) {
    if (value < 0) {
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_label_set_text(pct, "--");
        lv_label_set_text(reset, "");
        return;
    }
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(colorForPercent(value)), LV_PART_INDICATOR);
    lv_label_set_text_fmt(pct, "%d%%", value);
    if (resetAt > 0) {
        static const char *DAYS[] = {"dim", "lun", "mar", "mer", "jeu", "ven", "sam"};
        time_t t = (time_t)resetAt;
        struct tm tm;
        localtime_r(&t, &tm);
        if (withDay)
            lv_label_set_text_fmt(reset, "reset %s %02d:%02d", DAYS[tm.tm_wday], tm.tm_hour, tm.tm_min);
        else
            lv_label_set_text_fmt(reset, "reset %02d:%02d", tm.tm_hour, tm.tm_min);
    } else {
        lv_label_set_text(reset, "");
    }
}

static void addCard(const Row &r, bool showHost, time_t now) {
    const Session &s = *r.session;
    uint32_t color = stateColor(s.state);

    lv_obj_t *card = lv_obj_create(list);
    lv_obj_set_size(card, lv_pct(100), 62);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_border_side(card, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(card, 5, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(color), 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = mkLabel(card, &lv_font_montserrat_18, COLOR_TEXT);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 150);
    if (showHost) lv_label_set_text_fmt(name, "%s@%s", s.project, r.host);
    else lv_label_set_text(name, s.project);
    lv_obj_set_pos(name, 4, 0);

    lv_obj_t *model = mkLabel(card, &lv_font_montserrat_14, COLOR_DIM);
    lv_label_set_text(model, s.model);
    lv_obj_set_pos(model, 160, 3);

    lv_obj_t *state = mkLabel(card, &lv_font_montserrat_14, color);
    lv_label_set_text(state, stateLabel(s.state));
    lv_obj_set_pos(state, 250, 3);

    lv_obj_t *tool = mkLabel(card, &lv_font_montserrat_14, COLOR_DIM);
    lv_label_set_text(tool, s.state == State::Idle ? "" : s.tool);
    lv_obj_set_pos(tool, 345, 3);

    lv_obj_t *dur = mkLabel(card, &lv_font_montserrat_16, COLOR_TEXT);
    lv_obj_align(dur, LV_ALIGN_TOP_RIGHT, 0, 1);
    if (durCount < MAX_ROWS) {
        durLabels[durCount] = dur;
        durSince[durCount] = s.since;
        durCount++;
    }

    lv_obj_t *ctxLbl = mkLabel(card, &lv_font_montserrat_14, COLOR_DIM);
    lv_obj_set_pos(ctxLbl, 4, 28);
    if (s.ctx >= 0) {
        lv_label_set_text(ctxLbl, "ctx");
        lv_obj_t *bar = lv_bar_create(card);
        lv_obj_set_size(bar, 140, 10);
        lv_obj_set_pos(bar, 36, 32);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, s.ctx, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x2a3a4a), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(colorForPercent(s.ctx)), LV_PART_INDICATOR);
        lv_obj_t *pct = mkLabel(card, &lv_font_montserrat_14, COLOR_TEXT);
        lv_label_set_text_fmt(pct, "%d%%", s.ctx);
        lv_obj_set_pos(pct, 184, 28);
    }
    (void)now;
}

void ui_render(const Dashboard &d, time_t now) {
    const Limits *lim = d.limits();
    renderLimit(bar5h, pct5h, reset5h, lim ? lim->h5 : -1, lim ? lim->h5Reset : 0, false);
    renderLimit(bar7d, pct7d, reset7d, lim ? lim->d7 : -1, lim ? lim->d7Reset : 0, true);

    lv_label_set_text(lblHost, d.hostCount() == 1 ? d.hostName(0) : "");

    lv_coord_t scroll = lv_obj_get_scroll_y(list);
    lv_obj_clean(list);
    durCount = 0;
    Row rows[MAX_ROWS];
    int n = d.rows(rows, MAX_ROWS);
    for (int i = 0; i < n; i++) addCard(rows[i], d.hostCount() > 1, now);
    lv_obj_update_layout(list);
    lv_obj_scroll_to_y(list, scroll, LV_ANIM_OFF);

    if (d.hostCount() == 0) lv_label_set_text(lblEmpty, "En attente de donnees...");
    else lv_label_set_text(lblEmpty, "Aucune session active");
    if (n == 0) lv_obj_clear_flag(lblEmpty, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(lblEmpty, LV_OBJ_FLAG_HIDDEN);

    ui_tick(d, now, true);
}

void ui_tick(const Dashboard &d, time_t now, bool mqttOk) {
    struct tm tm;
    localtime_r(&now, &tm);
    if (now > 1704067200) lv_label_set_text_fmt(lblClock, "%02d:%02d", tm.tm_hour, tm.tm_min);

    lv_obj_set_style_bg_color(dotMqtt, lv_color_hex(mqttOk ? 0x4caf50 : COLOR_PERM), 0);

    char buf[16];
    for (int i = 0; i < durCount; i++) {
        formatDuration(now - durSince[i], buf, sizeof buf);
        lv_label_set_text(durLabels[i], buf);
    }

    int64_t stale = d.hostCount() ? d.staleSeconds(now) : 0;
    bool isStale = stale > STALE_AFTER_S;
    if (isStale) {
        lv_label_set_text_fmt(banner, "Serveur injoignable depuis %d min", (int)(stale / 60));
        lv_obj_clear_flag(banner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_opa(list, isStale ? LV_OPA_50 : LV_OPA_COVER, 0);
}
```

Note : `mqttOk` n'est vraiment connu que de `main.cpp` ; `ui_render` passe `true` puis le tick suivant (≤ 1 s) corrige.

**Step 3: Écran de démo dans `main.cpp`** — temporairement, construire un `HostSnapshot` factice (3 sessions : permission/idle/working, quotas 42/18) via `parseSnapshot` sur une chaîne littérale, `apply()` puis `ui_render()`, et appeler `ui_tick()` chaque seconde dans `loop()` (sous `bsp_display_lock`). Configurer le fuseau (`setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1); tzset();`).

**Step 4: Compiler, flasher, vérifier visuellement**

Run: `cd $FW && $PIO run -e esp32s3 -t upload`
Expected : écran conforme à la maquette du design (3 cartes triées rouge/orange/bleu, barres 5h/7j, chronos qui avancent). Demander à l'utilisateur de confirmer le rendu (photo bienvenue) et ajuster positions/tailles si des textes se chevauchent.

**Step 5: Commit**

```bash
git add $FW/src
git commit -m "Claude_Dashboard: interface LVGL (quotas, cartes de session, bandeau)"
```

---

### Task 13: WiFi, NTP et MQTT/TLS dans `main.cpp`

**Files:**
- Modify: `$FW/src/main.cpp`

**Step 1: Remplacer la démo par la vraie boucle.** Points clés :

```cpp
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <esp_task_wdt.h>
#include <time.h>
#include "credentials.h"
#include "dash_model.h"
#include "ui.h"

#define WDT_TIMEOUT_SEC   30
#define MQTT_TOPIC        "claude-dash/+/state"
#define MQTT_RETRY_MS     5000
#define WIFI_RETRY_MS     10000

static WiFiClientSecure tls;
static PubSubClient mqtt(tls);
static dash::Dashboard dashboard;
static dash::HostSnapshot incoming;       // statique : ~2 Ko, hors pile
static volatile bool pendingRender = false;
static dash::Alert pendingAlert = dash::Alert::None;

static void onMessage(char *topic, byte *payload, unsigned int len) {
    if (len == 0) {
        // Retained efface (agent desinstalle / hote retire) : oublier l'hote.
        // topic = "claude-dash/<host>/state"
        char host[33];
        if (sscanf(topic, "claude-dash/%32[^/]/state", host) == 1 && dashboard.removeHost(host))
            pendingRender = true;
        return;
    }
    if (!dash::parseSnapshot((const char *)payload, len, incoming)) {
        Serial.printf("Snapshot invalide sur %s\n", topic);
        return;
    }
    time_t now = time(nullptr);
    // Un decalage d'horloge fausse la staleness et rend l'ecran muet
    // (snapshots juges perimes -> alertes supprimees) : le signaler.
    if (incoming.ts > 0 && llabs((long long)now - (long long)incoming.ts) > 30)
        Serial.printf("Attention : horloge decalee de %lld s avec %s (NTP ?)\n",
                      (long long)now - (long long)incoming.ts, incoming.host);
    dash::Alert a = dashboard.apply(incoming, now);
    if (a == dash::Alert::Permission || pendingAlert == dash::Alert::None) pendingAlert = a;
    pendingRender = true;
}

static bool mqttConnect() {
    char clientId[40];
    snprintf(clientId, sizeof clientId, "jc3248-dash-%012llx", ESP.getEfuseMac());
    if (!mqtt.connect(clientId, DASH_MQTT_USER, DASH_MQTT_PASS)) {
        Serial.printf("MQTT echec rc=%d\n", mqtt.state());
        return false;
    }
    mqtt.subscribe(MQTT_TOPIC, 1);
    Serial.println("MQTT connecte");
    return true;
}
```

`onMessage` doit appeler `removeHost` sur un payload vide (retained effacé) et journaliser un avertissement quand `|time(nullptr) - ts| > 30 s` : un décalage d'horloge fait juger les snapshots périmés, ce qui rend l'écran muet. Nota : un message retained ancien (agent mort) déclenche aussi l'avertissement au démarrage, c'est attendu.

Dans `setup()` : watchdog (comme Transit_Tracker), écran, `ui_create()`, WiFi non bloquant au-delà de 15 s, NTP (`configTime` + TZ Paris, attendre `time() > 1704067200`, requis pour valider le certificat TLS), puis :

```cpp
tls.setCACert(DASH_MQTT_CA_CERT);
mqtt.setServer(DASH_MQTT_SERVER, DASH_MQTT_PORT);
mqtt.setBufferSize(4096);
mqtt.setKeepAlive(60);
mqtt.setSocketTimeout(10);
mqtt.setCallback(onMessage);
```

Dans `loop()` :
- `esp_task_wdt_reset()` ;
- reconnexion WiFi toutes les 10 s si perdu (comme Transit_Tracker) ;
- si WiFi OK et `!mqtt.connected()` : `mqttConnect()` au plus toutes les 5 s ; sinon `mqtt.loop()` ;
- si `pendingRender` : sous `bsp_display_lock`, `ui_render(dashboard, time(nullptr))` ; si `pendingAlert != None` → `alertTriggered(pendingAlert)` (Task 14 ; pour l'instant un `Serial.println`) puis reset ;
- toutes les secondes : `ui_tick(dashboard, time(nullptr), mqtt.connected())` sous lock.

**Step 2: Compiler** — `$PIO run -e esp32s3` → SUCCESS.

**Step 3: Test de bout en bout avec le vrai broker**

Prérequis utilisateur : broker cloud créé, deux comptes/ACL, `credentials.h` et `~/.config/claude-dash/config.toml` renseignés (demander, ne pas inventer).

1. Upload du firmware, puis `timeout 30 $PIO device monitor` → `MQTT connecte`.
2. Depuis ce poste, publier un snapshot de test sur le broker cloud avec l'agent en mode dev (ou `mosquitto_sub/pub` en Docker avec `--cafile`/`-u`/`-P` du compte serveur) :
   `docker run --rm eclipse-mosquitto:2 mosquitto_pub -h $HOST -p 8883 --capath /etc/ssl/certs -u $U -P $P -t claude-dash/test/state -r -m '{"host":"test","ts":...,"sessions":[...]}'`
   (Si l'image n'a pas `/etc/ssl/certs`, monter celui de l'hôte : `-v /etc/ssl/certs:/etc/ssl/certs:ro`.)
3. Vérifier : cartes affichées, pastille verte, heure.
4. Couper le WiFi du routeur ou changer de topic → pastille rouge / bandeau après 3 min.

Si `MQTT echec rc=-2` : vérifier heure NTP, CA, port ; tester la version TLS du broker (cf. `sketches/common/PRIM_TLS13_libs.md`).

**Step 4: Commit**

```bash
git add $FW/src
git commit -m "Claude_Dashboard: connexion WiFi, NTP et MQTT/TLS"
```

---

### Task 14: Bip I2S, réveil et veille de l'écran

**Files:**
- Create: `$FW/src/beep.h`
- Create: `$FW/src/beep.cpp`
- Modify: `$FW/src/main.cpp`

**Step 1: `beep.h` / `beep.cpp`** (NS4168 : BCK 42, LRCK 2, DOUT 41 ; ESP_I2S d'Arduino 3.0.7)

```cpp
// beep.h
#pragma once
#include <stdint.h>
void beep_begin();
void beep_tone(uint16_t freqHz, uint16_t durationMs, uint8_t volume = 40);  // volume 0-100
```

```cpp
// beep.cpp
#include "beep.h"
#include <ESP_I2S.h>

#define I2S_BCK   42
#define I2S_LRCK  2
#define I2S_DOUT  41
#define RATE      16000

static I2SClass i2s;
static bool ready = false;

void beep_begin() {
    i2s.setPins(I2S_BCK, I2S_LRCK, I2S_DOUT);
    ready = i2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    if (!ready) Serial.println("I2S: echec init");
}

void beep_tone(uint16_t freqHz, uint16_t durationMs, uint8_t volume) {
    if (!ready || freqHz == 0) return;
    const int16_t amp = (int16_t)(32767L * volume / 100);
    const uint32_t half = RATE / (2 * freqHz);
    const uint32_t total = (uint32_t)RATE * durationMs / 1000;
    int16_t buf[256];
    uint32_t n = 0;
    while (n < total) {
        size_t chunk = 0;
        for (; chunk < 256 && n < total; chunk++, n++) {
            // carre adouci : enveloppe lineaire sur les 5 premieres/dernieres ms
            uint32_t edge = RATE / 200;
            int32_t env = n < edge ? n * 1000 / edge
                        : (total - n) < edge ? (total - n) * 1000 / edge : 1000;
            int16_t v = ((n / half) & 1) ? amp : -amp;
            buf[chunk] = (int16_t)((int32_t)v * env / 1000);
        }
        i2s.write((uint8_t *)buf, chunk * sizeof(int16_t));
    }
    int16_t silence[64] = {0};
    i2s.write((uint8_t *)silence, sizeof silence);  // evite un "clic" residuel
}
```

**Step 2: Brancher dans `main.cpp`**

```cpp
static uint32_t lastActivityMs = 0;
static bool screenOn = true;

static void alertTriggered(dash::Alert a) {
    lastActivityMs = millis();
    if (a == dash::Alert::Permission) {
        beep_tone(1760, 90); delay(40); beep_tone(1760, 90);   // deux notes aigues
    } else if (a == dash::Alert::Idle) {
        beep_tone(660, 150);                                    // une note grave
    }
}
```

- `beep_begin()` dans `setup()` ;
- toucher : ajouter un callback LVGL sur l'écran (`lv_obj_add_event_cb(lv_scr_act(), onTouch, LV_EVENT_PRESSED, NULL)` + flag `LV_OBJ_FLAG_CLICKABLE`) qui met `lastActivityMs = millis()`. Vérifier dans `lv_port.c` que les pressions arrivent bien quand le rétroéclairage est éteint (le tactile reste alimenté) ; sinon utiliser `lv_disp_get_inactive_time()` à la place du callback ;
- chaque seconde dans `loop()` :

```cpp
bool on = dash::screenShouldBeOn(dashboard.anyWaiting(), millis(), lastActivityMs);
if (on != screenOn) {
    screenOn = on;
    if (on) bsp_display_backlight_on(); else bsp_display_backlight_off();
}
```

- Le premier toucher qui rallume l'écran ne doit pas déclencher d'action (il n'y a pas de bouton, donc sans conséquence ici).

**Step 3: Compiler + upload**, puis scénario manuel avec `mosquitto_pub` (Task 13) :
1. publier `working`, puis `permission` → deux bips aigus, carte rouge en tête ;
2. publier `idle` → un bip grave ;
3. republier le même message → aucun bip ;
4. tout en `working`, attendre 10 min sans toucher → écran éteint ; toucher → rallumé ; publier `permission` écran éteint → rallumé + bip.

Ajuster le volume si trop fort/faible (paramètre `volume`).

**Step 4: Commit**

```bash
git add $FW/src
git commit -m "Claude_Dashboard: bip I2S sur attente et veille de l'ecran"
```

---

### Task 15: Qualité firmware, déploiement réel et documentation

**Files:**
- Modify: `CLAUDE.md` (section JC3248W535C, liste des sketches)
- Modify: `libraries.txt` (PubSubClient déjà présent : ajouter un commentaire « aussi Claude_Dashboard » ; rien à installer côté arduino-cli, les libs sont gérées par PlatformIO)
- Create: `$FW/README.md`

**Step 1: Analyse statique**

Run: `cd $FW && $PIO check -e esp32s3 --skip-packages`
Expected : aucun défaut `high`/`medium` dans `src/main.cpp`, `src/ui.cpp`, `src/beep.cpp`, `lib/dash_model`. Corriger ou justifier (`// cppcheck-suppress`).

Run: `$PIO test -e native` → tout PASS. `$SRV/run-checks.sh` → tout PASS.

**Step 2: Déploiement sur le serveur distant** (fait par l'utilisateur, guidé) : copier `server/` (`scp -r` ou `git clone`), `./install.sh`, renseigner `config.toml`, ajouter la ligne à la statusline, `loginctl enable-linger`. Lancer une session Claude Code sur le serveur et vérifier sur l'écran : apparition de la session, passage `working` pendant une réponse, `permission` sur une commande à approuver (bip), `idle` à la fin du tour, disparition à `/exit`. Tuer une session avec `kill -9` → disparition sous ~2 s.

**Step 3: `CLAUDE.md`** — ajouter dans « Sketches disponibles » de JC3248W535C :

```
- `Claude_Dashboard/` - Tableau de bord Claude Code : quotas 5h/7j, sessions (projet, modele, etat travaille/attente/permission, outil, contexte) d'un serveur distant via MQTT/TLS (broker cloud dedie, `DASH_MQTT_*` dans `credentials.h`). Bip NS4168 + reveil ecran quand une session attend. Agent serveur Python dans `server/` (hooks + statusline + service systemd --user, voir son README)
```

**Step 4: `$FW/README.md`** — schéma d'architecture, prérequis broker (TLS, 2 comptes, ACL), renseignement de `credentials.h`, build/upload, tests (`pio test -e native`, `server/run-checks.sh`), dépannage (rc MQTT, NTP, TLS 1.3 ; « aucun bip / bandeau de perte de liaison permanent → vérifier NTP sur le serveur », l'âge des snapshots étant calculé depuis leur `ts`).

**Step 5: Commit**

```bash
git add CLAUDE.md libraries.txt $FW
git commit -m "Claude_Dashboard: documentation et verification qualite"
```
