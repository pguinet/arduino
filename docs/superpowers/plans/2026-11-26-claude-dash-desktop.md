# Claude Dashboard desktop (Tk over remote X) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `claude-dash-desktop` command that shows the Claude Code dashboard (all servers, quotas, session cards, alerts) in an always-on-top Tk window, run on IF-VL-SEC-P1-PRI and displayed on MobaXterm through SSH X11 forwarding.

**Architecture:** New modules in the existing `claude_dash` package (`sketches/JC3248W535C/Claude_Dashboard/server/`). A pure Python port of the firmware model (`lib/dash_model/`) feeds a pure controller/view-model layer; a paho subscriber thread posts events on a `queue.Queue`; a thin Tk layer polls the queue, renders the view-model and rings the X bell. Everything except the Tk layer is unit-tested without a display.

**Tech Stack:** Python ≥ 3.11 (3.13 on the host), Tkinter (Tk 8.6, stdlib), paho-mqtt 2.x (already a dependency), pytest, ruff, mypy `strict`.

**Spec:** `docs/superpowers/specs/2026-11-26-claude-dash-desktop-design.md` (read it first).

All paths below are relative to `sketches/JC3248W535C/Claude_Dashboard/server/` unless they start with `sketches/` or `docs/`.

## Global Constraints

- Python ≥ 3.11 (`requires-python`), no new runtime dependency, no system package to install.
- Modules other than `claude_dash/desktop_ui.py` must not import `tkinter`: `run-checks.sh` runs in `python:3.13-slim` (no Tk) and CI has no display.
- `desktop_ui.py` is imported lazily from `desktop.main()` only.
- ruff (`line-length = 100`, rules `E F W I B UP SIM`), `ruff format`, `mypy --strict claude_dash` and `pytest -q` must pass: `./run-checks.sh`.
- Code, comments, identifiers, commit messages in English. User-facing strings (window, logs, errors) and READMEs in French, as in the existing agent.
- Commits: descriptive message, no ticket prefix, end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Author `pascal.guinet@free.fr` (local git config already set).
- Thresholds copied from the firmware: `STALE_AFTER_S = 180`, `FORGET_AFTER_S = 3600`.
- No caps on hosts or sessions (the firmware's 4 × 12 limits are not ported).
- Colors (from `src/ui.cpp`): bg `#0f1419`, card `#1c2733`, track `#2a3a4a`, text `#ffffff`, dim `#888888`, working `#4cc9f0`, idle `#fca311`, permission `#f72585`, question `#b388ff`, ok `#4caf50`. Gauge: `< 60` → `#4caf50`, `< 85` → `#fca311`, else `#f72585`.
- MQTT: subscribe `<topic_prefix>/+/state`, QoS 0. Empty payload = host removed.
- Window: initial 480×320, resizable, `-topmost` always on, no option to turn it off.
- Alerts: X bell rung twice for permission and question, once for idle; cards that triggered the alert flash.

## Review Focus

1. Garbage payloads (non-UTF-8 bytes, top-level JSON array/`null`, `host` not a string): ignored, never an exception in the paho thread — tests in Task 4.
2. Same `poste` Device ID used by another client (hub kicks one off, repeated disconnects): status line shows the disconnect reason and paho keeps reconnecting (1..60 s) — test in Task 4.
3. Topic not matching `<prefix>/<host>/state` exactly, or payload `host` ≠ topic host: message dropped with a warning — tests in Task 4.
4. More sessions than fit in the window, or a window shrunk below 480×320: extra cards collapse into "+N autres sessions", no exception — test in Task 3 (`fit_cards`), manual check in Task 5.
5. Clock or data oddities (`since` = 0 or in the future, `ts` in the future, quota reset in the past): durations blank or clamped, stale window ignored, no crash — tests in Tasks 1, 2, 3.

---

### Task 1: Snapshot parsing and display helpers

**Files:**
- Create: `claude_dash/model.py`
- Test: `tests/test_model_parse.py`

**Interfaces:**
- Produces (used by every later task):
  - `class State(Enum)`: `WORKING = "working"`, `IDLE = "idle"`, `PERMISSION = "permission"`, `QUESTION = "question"`
  - `class Alert(IntEnum)`: `NONE = 0`, `IDLE = 1`, `QUESTION = 2`, `PERMISSION = 3` (order = priority)
  - `@dataclass(frozen=True) class Limits`: `h5: int = -1`, `h5_reset: int = 0`, `d7: int = -1`, `d7_reset: int = 0`, `updated: int = 0`
  - `@dataclass(frozen=True) class Session`: `id: str`, `project: str`, `model: str`, `tool: str`, `state: State`, `since: int`, `ctx: int`
  - `@dataclass(frozen=True) class HostSnapshot`: `host: str`, `ts: int`, `limits: Limits`, `sessions: tuple[Session, ...]`
  - `parse_snapshot(payload: bytes | str) -> HostSnapshot | None`
  - `format_duration(seconds: int) -> str`, `color_for_percent(pct: int) -> str`, `state_label(state: State) -> str`, `state_color(state: State) -> str`
  - constants `STALE_AFTER_S = 180`, `FORGET_AFTER_S = 3600`, the color constants of Global Constraints (`COLOR_BG`, `COLOR_CARD`, `COLOR_TRACK`, `COLOR_TEXT`, `COLOR_DIM`, `COLOR_WORKING`, `COLOR_IDLE`, `COLOR_PERMISSION`, `COLOR_QUESTION`, `COLOR_OK`)

- [ ] **Step 1: Write the failing tests**

Port, with the same JSON inputs and expected values, these tests from `sketches/JC3248W535C/Claude_Dashboard/test/test_parse/test_main.cpp` and `test/test_helpers/test_main.cpp`: `test_parse_full`, `test_parse_question_state`, `test_parse_no_limits`, `test_parse_invalid_json`, `test_parse_missing_host`, `test_invalid_session_entries_skipped`, `test_unknown_state_maps_to_idle`, `test_wrong_types_are_ignored`, `test_host_not_string_or_sessions_not_array`, `test_percentages_clamped`, `test_format_duration`, `test_format_duration_huge`, `test_color_thresholds`, `test_state_labels`. A C++ `false` return becomes `None`.

Not ported (firmware memory limits or C buffers): `test_parse_failure_leaves_output_untouched`, `test_host_of_32_chars_kept_intact`, `test_reparse_resets_previous_content`, `test_format_duration_small_buffer`, `test_screen_policy`. Replace `test_parse_truncates_long_strings_and_caps_sessions` with:

```python
def test_parse_keeps_long_strings_and_all_sessions() -> None:
    sessions = [{"id": f"s{i}", "project": "p" * 80} for i in range(20)]
    snap = parse_snapshot(json.dumps({"host": "h", "sessions": sessions}))
    assert snap is not None
    assert len(snap.sessions) == 20
    assert snap.sessions[0].project == "p" * 80
```

Add:

```python
@pytest.mark.parametrize("payload", [b"\xff\xfe", b"[]", b"null", b"42", b'{"host": 3}', b'{"host": ""}'])
def test_parse_rejects_garbage(payload: bytes) -> None:
    assert parse_snapshot(payload) is None

def test_state_colors() -> None:
    assert state_color(State.PERMISSION) == "#f72585"
    assert state_color(State.QUESTION) == "#b388ff"
    assert state_color(State.IDLE) == "#fca311"
    assert state_color(State.WORKING) == "#4cc9f0"
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pytest tests/test_model_parse.py -q`
Expected: FAIL (`ModuleNotFoundError: claude_dash.model`).

- [ ] **Step 3: Implement `claude_dash/model.py` (parsing and helpers)**

Same rules as `lib/dash_model/dash_model.cpp`: a percentage is an `int` that is not a `bool`, negative → -1, above 100 → 100; an epoch is an `int` that is not a `bool`, otherwise or negative → 0; a non-string text field → `""`; an unknown or missing `state` → `IDLE`; a session that is not a dict or has no non-empty string `id` is skipped; `sessions` not a list → no session; a top-level value that is not a dict, or no non-empty string `host` → `None`. Decode bytes as UTF-8; a decode error → `None`. `format_duration` outputs `"0:42"`, `"5:10"`, `"1h05"`, `"2j"`, `"999j+"` beyond 999 days; negative → `"0:00"`. Labels: `TRAVAILLE`, `ATTENTE`, `PERMISSION`, `QUESTION`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `pytest tests/test_model_parse.py -q`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add claude_dash/model.py tests/test_model_parse.py
git commit -m "Claude_Dashboard desktop: port snapshot parsing and display helpers to Python"
```

---

### Task 2: Multi-host dashboard model

**Files:**
- Modify: `claude_dash/model.py`
- Test: `tests/test_model_dashboard.py`

**Interfaces:**
- Consumes: Task 1 types.
- Produces:
  - `@dataclass(frozen=True) class Row`: `session: Session`, `host: str`
  - `@dataclass(frozen=True) class ApplyResult`: `alert: Alert`, `changed: bool`, `alerted: frozenset[tuple[str, str]]` (pairs `(host, session_id)`)
  - `class Dashboard` with `apply(snap: HostSnapshot, now: int) -> ApplyResult`, `remove_host(host: str) -> bool`, `expire(now: int) -> int`, `rows() -> list[Row]`, `limits(now: int) -> Limits`, `host_names() -> list[str]` (arrival order), `stale_seconds(now: int) -> int`, `session_count() -> int`

- [ ] **Step 1: Write the failing tests**

Port, with the same scenarios and values, every test of `test/test_dashboard/test_main.cpp`, except those that rely on the 4-host cap or the row buffer: `test_rows_capped_by_max`, `test_extra_host_ignored_when_none_evictable`, `test_extra_host_evicts_oldest_dead_host`, `test_not_changed_when_host_ignored`, `test_changed_when_host_evicts_dead_one`. `apply(...)` returning an `Alert` and filling `*changed` becomes `ApplyResult.alert` and `ApplyResult.changed`.

Add:

```python
def test_alerted_lists_every_session_that_changed_to_an_alert_state() -> None:
    d = Dashboard()
    d.apply(snap("h", [sess("a", "working"), sess("b", "working"), sess("c", "working")]), NOW)
    r = d.apply(snap("h", [sess("a", "permission"), sess("b", "idle"), sess("c", "working")]), NOW + 1)
    assert r.alert is Alert.PERMISSION
    assert r.alerted == frozenset({("h", "a"), ("h", "b")})

def test_alerted_empty_when_previous_snapshot_was_stale() -> None:
    d = Dashboard()
    d.apply(snap("h", [sess("a", "working")], ts=NOW), NOW)
    r = d.apply(snap("h", [sess("a", "permission")], ts=NOW + 200), NOW + 200)
    assert r.alert is Alert.NONE and r.alerted == frozenset()

def test_many_hosts_are_all_kept() -> None:
    d = Dashboard()
    for i in range(6):
        d.apply(snap(f"h{i}", [sess("a", "idle")]), NOW)
    assert d.host_names() == [f"h{i}" for i in range(6)]
```

(`snap(host, sessions, ts=0, limits=Limits())` and `sess(id, state, since=0)` are local helpers building the frozen dataclasses.)

- [ ] **Step 2: Run tests to verify they fail**

Run: `pytest tests/test_model_dashboard.py -q`
Expected: FAIL (`ImportError: Dashboard`).

- [ ] **Step 3: Implement `Dashboard` in `claude_dash/model.py`**

Same algorithm as `Dashboard` in `dash_model.cpp` minus slot eviction: snapshot age = `now - (ts if ts > 0 else received_at)`, clamped at 0; no alert if the previous snapshot of that host was older than `STALE_AFTER_S`; `changed` ignores `ts` and `limits.updated`, and is also true when the displayed limits switch; `rows()` sorts with a stable sort on (urgency, `since`), urgency permission 0 < question 1 < idle 2 < working 3; `limits()` picks each window from the host with the highest freshness (`limits.updated` if > 0, else `ts`), skipping a window whose `pct < 0` or whose reset is past (`0 < reset <= now`).

- [ ] **Step 4: Run tests to verify they pass**

Run: `pytest tests/test_model_dashboard.py tests/test_model_parse.py -q`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add claude_dash/model.py tests/test_model_dashboard.py
git commit -m "Claude_Dashboard desktop: port the multi-host dashboard model to Python"
```

---

### Task 3: Desktop config, view-model and controller

**Files:**
- Create: `claude_dash/desktop_config.py`, `claude_dash/view.py`, `desktop.toml.example`
- Test: `tests/test_desktop_config.py`, `tests/test_view.py`

**Interfaces:**
- Consumes: Task 1–2 (`Dashboard`, `ApplyResult`, `HostSnapshot`, helpers, colors); private helpers of `claude_dash/config.py` (`_opt_str`, `_opt_bool`, `_opt_path`, `_client_id`, `_client_cert`, `_transport`, `_topic_prefix`, `_reject_placeholders`, `ConfigError`, `Transport`).
- Produces:
  - `desktop_config.py`: `@dataclass(frozen=True) class DesktopConfig` with `host: str`, `port: int`, `username: str | None`, `password: str | None`, `ca_certs: str | None`, `tls: bool`, `topic_prefix: str`, `client_id: str`, `certfile: str | None`, `keyfile: str | None`, `transport: Transport`, `corner: Corner`; property `topic -> str` (`f"{topic_prefix}/+/state"`); `Corner = Literal["top-left", "top-right", "bottom-left", "bottom-right"]`; `default_path() -> Path` (`$XDG_CONFIG_HOME/claude-dash/desktop.toml`, default `~/.config`); `load_desktop_config(path: Path) -> DesktopConfig` (raises `ConfigError`).
  - `view.py`:
    - `class Phase(Enum)`: `CONNECTING`, `CONNECTED`, `DISCONNECTED`, `FAILED`, `REFUSED`; `@dataclass(frozen=True) class ConnectionStatus`: `phase: Phase`, `detail: str = ""`
    - `@dataclass(frozen=True) class Quota`: `label: str`, `pct: int`, `pct_text: str`, `color: str`, `reset_text: str`
    - `@dataclass(frozen=True) class Card`: `key: tuple[str, str]`, `title: str`, `model: str`, `state_text: str`, `state_color: str`, `tool: str`, `since: int`, `ctx: int`, `ctx_text: str`, `ctx_color: str`
    - `@dataclass(frozen=True) class Screen`: `host_label: str`, `mqtt_ok: bool`, `quotas: tuple[Quota, Quota]`, `cards: tuple[Card, ...]`, `message: str` (empty when there are cards), `banner: str` (empty when not stale), `dimmed: bool`
    - `@dataclass(frozen=True) class Reaction`: `rebuild: bool`, `bells: int`, `flash: frozenset[tuple[str, str]]`
    - `class Controller(broker: str)` (`broker` = `"host:port"` for the status text): `on_snapshot(snap: HostSnapshot, now: int) -> Reaction`, `on_removed(host: str) -> Reaction`, `on_status(status: ConnectionStatus) -> Reaction`, `expire(now: int) -> Reaction`, `screen(now: int, tz: tzinfo | None = None) -> Screen`
    - `clock_text(now: int, tz: tzinfo | None = None) -> str`, `duration_text(since: int, now: int) -> str`, `fit_cards(count: int, available: int, card_height: int, more_height: int) -> int`, `initial_geometry(corner: Corner, screen_w: int, screen_h: int, width: int = 480, height: int = 320, margin: int = 20) -> str`

- [ ] **Step 1: Write the failing config tests** (`tests/test_desktop_config.py`, same `write()` helper style as `tests/test_config.py`)

```python
def test_desktop_defaults(tmp_path: Path) -> None:
    cfg = load_desktop_config(write(tmp_path, '[mqtt]\nhost="h"\n'))
    assert (cfg.port, cfg.tls, cfg.transport, cfg.corner) == (8883, True, "tcp", "top-right")
    assert cfg.client_id == "claude-dash-desktop"
    assert cfg.topic == "claude-dash/+/state"

def test_desktop_websockets_and_corner(tmp_path: Path) -> None:
    cfg = load_desktop_config(write(tmp_path, '[mqtt]\nhost="h"\nport=443\ntransport="websockets"\n'
                                              '[window]\ncorner="bottom-left"\n'))
    assert (cfg.port, cfg.transport, cfg.corner) == (443, "websockets", "bottom-left")

def test_desktop_rejects_bad_corner(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="corner"):
        load_desktop_config(write(tmp_path, '[mqtt]\nhost="h"\n[window]\ncorner="middle"\n'))

def test_desktop_requires_chmod_600(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="600"):
        load_desktop_config(write(tmp_path, '[mqtt]\nhost="h"\n', mode=0o644))

def test_desktop_rejects_placeholder(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="client_id"):
        load_desktop_config(write(tmp_path, '[mqtt]\nhost="h"\nclient_id="votre_device_id_poste"\n'))

def test_desktop_example_file_parses_once_placeholders_filled(tmp_path: Path) -> None:
    # desktop.toml.example with every "votre_..." replaced and cert paths pointing at tmp files
    ...
```

The last test reads `desktop.toml.example`, substitutes the placeholders and cert/key paths (create dummy files, key in chmod 600), and asserts `load_desktop_config` succeeds with `transport == "websockets"` and `port == 443`.

- [ ] **Step 2: Run them to verify they fail**

Run: `pytest tests/test_desktop_config.py -q` — Expected: FAIL (module missing).

- [ ] **Step 3: Implement `desktop_config.py` and `desktop.toml.example`**

`load_desktop_config` mirrors `load_config` (chmod 600 check, TOML errors wrapped as `ConfigError(f"{path}: ...")`, placeholders rejected in `[mqtt]` and `[window]`), with no `[agent]` section; `client_id` default from `_client_id(mqtt, "desktop")`. `desktop.toml.example`: same comments style as `config.toml.example`, values for sec-p1 (`host = "iot.fr-par.scw.cloud"`, `port = 443`, `transport = "websockets"`, `ca_certs = "~/.config/claude-dash/iot-hub-ca.pem"`, `certfile = "~/.config/claude-dash/poste.crt"`, `keyfile = "~/.config/claude-dash/poste.key"`, `client_id = "votre_device_id_poste"`), plus a commented `[window] corner = "top-right"` listing the four values, and a note that the 8883 alternative is `port = 8883` without `transport`.

- [ ] **Step 4: Run config tests** — `pytest tests/test_desktop_config.py tests/test_config.py -q` — Expected: all pass.

- [ ] **Step 5: Write the failing view/controller tests** (`tests/test_view.py`, `PARIS = ZoneInfo("Europe/Paris")`)

```python
def test_waiting_screen_shows_connection_status() -> None:
    c = Controller("iot.fr-par.scw.cloud:443")
    assert c.screen(NOW, PARIS).message == "MQTT : connexion à iot.fr-par.scw.cloud:443…"
    c.on_status(ConnectionStatus(Phase.CONNECTED))
    s = c.screen(NOW, PARIS)
    assert s.mqtt_ok and s.message == "MQTT : connecté, en attente d'un serveur (20 s max)"
    c.on_status(ConnectionStatus(Phase.DISCONNECTED, "session taken over"))
    assert c.screen(NOW, PARIS).message == "MQTT : déconnecté (session taken over), reconnexion…"
    c.on_status(ConnectionStatus(Phase.FAILED, "TLS"))
    assert c.screen(NOW, PARIS).message == "MQTT : échec de connexion (TLS), nouvel essai"
    c.on_status(ConnectionStatus(Phase.REFUSED, "Not authorized"))
    assert c.screen(NOW, PARIS).message == "MQTT : refusé par le broker (Not authorized)"

def test_no_session_message_once_a_host_is_known() -> None:
    c = connected_controller()
    c.on_snapshot(snap("srv", []), NOW)
    assert c.screen(NOW, PARIS).message == "Aucune session active"

def test_single_host_label_and_titles_without_host() -> None:
    c = connected_controller()
    c.on_snapshot(snap("srv", [sess("a", "permission", project="arduino")]), NOW)
    s = c.screen(NOW, PARIS)
    assert s.host_label == "srv" and s.cards[0].title == "arduino"

def test_multi_host_titles_with_host_and_empty_label() -> None:
    ...  # two hosts → host_label == "" and title == "arduino@srv"

def test_tool_hidden_when_idle() -> None:
    ...  # idle session with tool "Bash" → Card.tool == ""

def test_quota_texts_and_colors() -> None:
    # h5=42 reset at 17:00 Paris today, d7=90 reset Monday 09:00 Paris
    q5, q7 = screen.quotas
    assert (q5.label, q5.pct_text, q5.color, q5.reset_text) == ("5h", "42%", "#4caf50", "reset 17:00")
    assert (q7.label, q7.pct_text, q7.color, q7.reset_text) == ("7j", "90%", "#f72585", "reset lun 09:00")

def test_unknown_quota_and_context() -> None:
    # no limits, ctx = -1 → pct_text "--", reset_text "", ctx_text "--"

def test_stale_banner_and_dimmed() -> None:
    c = connected_controller()
    c.on_snapshot(snap("srv", [sess("a", "idle")], ts=NOW), NOW)
    s = c.screen(NOW + 181 + 5 * 60, PARIS)
    assert s.banner == "Serveur injoignable depuis 8 min" and s.dimmed
    assert c.screen(NOW + 180, PARIS).banner == ""

def test_reaction_bells_and_flash() -> None:
    c = connected_controller()
    c.on_snapshot(snap("srv", [sess("a", "working"), sess("b", "working")]), NOW)
    r = c.on_snapshot(snap("srv", [sess("a", "question"), sess("b", "working")]), NOW + 1)
    assert (r.rebuild, r.bells, r.flash) == (True, 2, frozenset({("srv", "a")}))
    r = c.on_snapshot(snap("srv", [sess("a", "idle"), sess("b", "working")]), NOW + 2)
    assert r.bells == 1
    r = c.on_snapshot(snap("srv", [sess("a", "idle"), sess("b", "working")], ts=NOW + 3), NOW + 3)
    assert (r.rebuild, r.bells) == (False, 0)

def test_removed_and_expired_hosts_rebuild() -> None:
    ...  # on_removed known host → rebuild True; unknown → False; expire after 3601 s → rebuild True

def test_duration_and_clock_texts() -> None:
    assert duration_text(0, NOW) == ""
    assert duration_text(NOW - 42, NOW) == "0:42"
    assert duration_text(NOW + 10, NOW) == "0:00"
    at = int(datetime(2026, 11, 26, 10, 40, tzinfo=PARIS).timestamp())
    assert clock_text(at, PARIS) == "10:40"

@pytest.mark.parametrize(("count", "available", "expected"), [(3, 300, 3), (10, 300, 3), (0, 300, 0), (5, 10, 0)])
def test_fit_cards(count: int, available: int, expected: int) -> None:
    assert fit_cards(count, available, card_height=70, more_height=20) == expected

def test_initial_geometry() -> None:
    assert initial_geometry("top-right", 3840, 1930) == "480x320+3340+20"
    assert initial_geometry("bottom-left", 3840, 1930) == "480x320+20+1590"
```

- [ ] **Step 6: Run them to verify they fail** — `pytest tests/test_view.py -q` — Expected: FAIL (module missing).

- [ ] **Step 7: Implement `view.py`**

Exact strings are those in the tests above, plus reset formats `reset HH:MM` (5h) and `reset <jour> HH:MM` (7j, days `dim lun mar mer jeu ven sam`) as in `renderLimit` of `src/ui.cpp`; a reset ≤ 0 or an unknown pct gives `""`. `Reaction.bells`: 2 for `PERMISSION` and `QUESTION`, 1 for `IDLE`, 0 otherwise; `rebuild` = `ApplyResult.changed`; `on_status` returns `rebuild=True` only when the phase or detail changes. `fit_cards`: all cards if `count * card_height <= available`; otherwise the largest `n` with `n * card_height + more_height <= available` (never negative). `initial_geometry` returns a Tk geometry string `WxH+X+Y`. `tz=None` means local time.

- [ ] **Step 8: Run tests** — `pytest -q` — Expected: all pass.

- [ ] **Step 9: Commit**

```bash
git add claude_dash/desktop_config.py claude_dash/view.py desktop.toml.example tests/test_desktop_config.py tests/test_view.py
git commit -m "Claude_Dashboard desktop: config, view-model and controller"
```

---

### Task 4: MQTT subscriber and single-instance lock

**Files:**
- Create: `claude_dash/subscriber.py`, `claude_dash/instance_lock.py`
- Test: `tests/test_subscriber.py`, `tests/test_instance_lock.py`

**Interfaces:**
- Consumes: `DesktopConfig` (Task 3), `parse_snapshot`/`HostSnapshot` (Task 1), `ConnectionStatus`/`Phase` (Task 3).
- Produces:
  - `subscriber.py`: `@dataclass(frozen=True) class SnapshotEvent(snapshot: HostSnapshot)`, `RemovedEvent(host: str)`, `StatusEvent(status: ConnectionStatus)`; `Event = SnapshotEvent | RemovedEvent | StatusEvent`; `host_from_topic(topic: str, prefix: str) -> str | None`; `event_for_message(topic: str, payload: bytes, prefix: str) -> Event | None`; `make_callbacks(cfg: DesktopConfig, post: Callable[[Event], None]) -> Callbacks` where `Callbacks` is a dataclass of the four paho VERSION2 callables `on_connect`, `on_connect_fail`, `on_disconnect`, `on_message`; `start_client(cfg: DesktopConfig, post: Callable[[Event], None]) -> mqtt.Client`; `stop_client(client: mqtt.Client) -> None`.
  - `instance_lock.py`: `default_lock_path() -> Path` (`$XDG_RUNTIME_DIR/claude-dash-desktop.lock`, else `~/.config/claude-dash/desktop.lock`); `acquire(path: Path) -> IO[str] | None` (keeps the file open and `fcntl.flock(LOCK_EX | LOCK_NB)`-locked, writes the PID); `holder_pid(path: Path) -> int | None`.

- [ ] **Step 1: Write the failing tests**

```python
@pytest.mark.parametrize(("topic", "expected"), [
    ("claude-dash/srv/state", "srv"), ("claude-dash/srv/other", None), ("claude-dash/a/b/state", None),
    ("other/srv/state", None), ("claude-dash//state", None)])
def test_host_from_topic(topic: str, expected: str | None) -> None:
    assert host_from_topic(topic, "claude-dash") == expected

def test_empty_payload_removes_host() -> None:
    assert event_for_message("claude-dash/srv/state", b"", "claude-dash") == RemovedEvent("srv")

def test_valid_snapshot_event() -> None:
    e = event_for_message("claude-dash/srv/state", b'{"host":"srv","ts":1,"sessions":[]}', "claude-dash")
    assert isinstance(e, SnapshotEvent) and e.snapshot.host == "srv"

@pytest.mark.parametrize("payload", [b"\xff", b"[]", b"null", b'{"host":"other"}'])
def test_bad_payload_or_host_mismatch_dropped(payload: bytes, caplog: pytest.LogCaptureFixture) -> None:
    assert event_for_message("claude-dash/srv/state", payload, "claude-dash") is None

def test_callbacks_post_status_and_subscribe() -> None:
    posted: list[Event] = []
    cb = make_callbacks(cfg(), posted.append)
    client = FakeClient()
    cb.on_connect(client, None, None, FakeReason(failure=False, text="Success"), None)
    assert client.subscribed == [("claude-dash/+/state", 0)]
    assert posted[-1] == StatusEvent(ConnectionStatus(Phase.CONNECTED))
    cb.on_connect(client, None, None, FakeReason(failure=True, text="Not authorized"), None)
    assert posted[-1] == StatusEvent(ConnectionStatus(Phase.REFUSED, "Not authorized"))
    cb.on_disconnect(client, None, None, FakeReason(failure=True, text="Session taken over"), None)
    assert posted[-1] == StatusEvent(ConnectionStatus(Phase.DISCONNECTED, "Session taken over"))
    cb.on_connect_fail(client, None)
    assert posted[-1].status.phase is Phase.FAILED

def test_on_message_never_raises_on_garbage() -> None:
    posted: list[Event] = []
    make_callbacks(cfg(), posted.append).on_message(None, None, FakeMessage("claude-dash/srv/state", b"\xff"))
    assert posted == []

def test_second_acquire_fails_and_reports_pid(tmp_path: Path) -> None:
    path = tmp_path / "desktop.lock"
    first = acquire(path)
    assert first is not None
    assert acquire(path) is None
    assert holder_pid(path) == os.getpid()
    first.close()
    assert acquire(path) is not None
```

(`FakeReason` exposes `is_failure` and `__str__`; `FakeClient.subscribe(topic, qos)` records calls; `FakeMessage` has `topic` and `payload`.)

- [ ] **Step 2: Run them to verify they fail** — `pytest tests/test_subscriber.py tests/test_instance_lock.py -q` — Expected: FAIL.

- [ ] **Step 3: Implement `subscriber.py` and `instance_lock.py`**

`on_connect` subscribes on success (paho forgets subscriptions on reconnect, so subscribing in `on_connect` covers reconnects). `on_message` wraps `event_for_message` so that no exception escapes into the paho thread (log at ERROR). Dropped messages log a WARNING, at most one per host and reason every 10 minutes (the firmware's `HOST_MISMATCH_REPEAT_MS`). `start_client` builds the client like `agent._make_client` (VERSION2, `transport=cfg.transport`, `tls_set(ca_certs, certfile, keyfile)` when `tls`, `username_pw_set` when set, `reconnect_delay_set(1, 60)`, `connect_async(host, port, keepalive=60)`, `loop_start()`), posts `StatusEvent(ConnectionStatus(Phase.CONNECTING))` first. `stop_client` calls `disconnect()` then `loop_stop()`. `acquire` creates the parent directory, opens in `a+`, returns `None` on `BlockingIOError`, else truncates and writes `f"{os.getpid()}\n"`.

- [ ] **Step 4: Run tests** — `pytest -q` — Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add claude_dash/subscriber.py claude_dash/instance_lock.py tests/test_subscriber.py tests/test_instance_lock.py
git commit -m "Claude_Dashboard desktop: MQTT subscriber and single-instance lock"
```

---

### Task 5: Tk window, entry point and installation

**Files:**
- Create: `claude_dash/desktop_ui.py`, `claude_dash/desktop.py`
- Modify: `pyproject.toml` (`[project.scripts]`), `install.sh` (`COMMANDS`, line 22)
- Test: `tests/test_desktop_main.py`

**Interfaces:**
- Consumes: everything above.
- Produces: `desktop.main(argv: list[str] | None = None) -> None` (console script `claude-dash-desktop`); `desktop_ui.DesktopWindow(root: tk.Tk, controller: Controller, events: queue.Queue[Event], corner: Corner)` with `start() -> None`.

- [ ] **Step 1: Write the failing tests for `main()` error paths (no display needed)**

```python
def test_main_exits_2_on_bad_config(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    with pytest.raises(SystemExit) as exc:
        main(["--config", str(tmp_path / "absent.toml")])
    assert exc.value.code == 2
    assert "absent.toml" in capsys.readouterr().err

def test_main_exits_1_when_already_running(tmp_path: Path, monkeypatch: pytest.MonkeyPatch,
                                           capsys: pytest.CaptureFixture[str]) -> None:
    config = write_valid_config(tmp_path)
    lock = tmp_path / "desktop.lock"
    monkeypatch.setattr(desktop, "default_lock_path", lambda: lock)
    held = acquire(lock)
    with pytest.raises(SystemExit) as exc:
        main(["--config", str(config)])
    assert exc.value.code == 1
    assert f"déjà lancé (PID {os.getpid()})" in capsys.readouterr().err
```

(`main(argv: list[str] | None = None)` so tests can pass arguments; the lock is checked before any MQTT connection or Tk import.)

- [ ] **Step 2: Run them to verify they fail** — `pytest tests/test_desktop_main.py -q` — Expected: FAIL.

- [ ] **Step 3: Implement `desktop.py`**

Order: logging to stderr (same format as the agent) → `argparse` with `--config` (default `desktop_config.default_path()`) → `load_desktop_config` (`ConfigError` → message on stderr, exit 2) → `instance_lock.acquire(default_lock_path())` (`None` → `claude-dash-desktop déjà lancé (PID n)` on stderr, exit 1) → import `tkinter` and `desktop_ui` → `tk.Tk()` (`TclError` → `pas d'affichage X : lancer depuis une session SSH avec X11 forwarding (MobaXterm)`, exit 2) → `queue.Queue[Event]`, `start_client(cfg, events.put)`, `DesktopWindow(...).start()`, `root.mainloop()` → `stop_client` in a `finally`. SIGTERM/SIGINT close the window cleanly (`root.after` from the handler).

- [ ] **Step 4: Run tests** — `pytest tests/test_desktop_main.py -q` — Expected: pass.

- [ ] **Step 5: Implement `desktop_ui.py`**

Decisions the builder must follow:
- Title `Claude Code`, `root.geometry(initial_geometry(corner, screen_w, screen_h))`, `root.minsize(320, 200)`, `root.attributes("-topmost", True)`, background `COLOR_BG`.
- Layout as the screen (see `src/ui.cpp` and the README sketch): header (title, `host_label`, MQTT dot `COLOR_OK`/`COLOR_PERMISSION`, clock), two quota rows (label, gauge, `pct_text`, `reset_text`), the stale banner (`COLOR_PERMISSION` background) shown only when `banner` is non-empty, then the cards. Card: left border 5 px in `state_color`, background `COLOR_CARD`; line 1 title / model / state / tool / duration (right-aligned); line 2 `ctx` gauge and `ctx_text`. Gauges are `tk.Canvas` rectangles on `COLOR_TRACK` whose width follows the window width (`<Configure>`).
- Fonts in pixels (negative Tk sizes), matching the LVGL fonts: title and card titles 18 px, quota labels and durations 16 px, other texts 14 px; family `DejaVu Sans` if available (`tkinter.font.families()`), else `TkDefaultFont`.
- Cards that do not fit the window height: `fit_cards()` → the rest becomes one dim label `+N autres sessions`. Recompute on `<Configure>` (debounced ~100 ms).
- Event loop: `root.after(200, poll)` drains the queue and routes each event to the controller; full rebuild of header/quotas/cards only when a `Reaction.rebuild` is true or the window is resized. Every second: clock, card durations (`duration_text`), banner and dimming (stale cards' texts in `COLOR_DIM`) — label `configure` only when the text changes. Every 30 s: `controller.expire(now)`.
- Alerts: `root.bell()` `Reaction.bells` times, 300 ms apart; each card whose key is in `Reaction.flash` alternates its background between `COLOR_CARD` and its `state_color` every 500 ms for 4 s (tracked by key across rebuilds).

- [ ] **Step 6: Register the command**

`pyproject.toml`: `claude-dash-desktop = "claude_dash.desktop:main"`. `install.sh`: add `claude-dash-desktop` to `COMMANDS`.

- [ ] **Step 7: Run the checks** — `./run-checks.sh` — Expected: ruff, format, mypy and pytest all green.

- [ ] **Step 8: Manual check against a local broker (no hub needed)**

`dev/docker-compose.yml` already provides a Mosquitto for integration tests (see `dev/README.md`), on host port 18830 (`CLAUDE_DASH_MQTT_PORT`). Start it, write a `desktop.toml` (chmod 600) pointing at it (`host = "127.0.0.1"`, `port = 18830`, `tls = false`), run `claude-dash-desktop --config <file>` from this SSH session (DISPLAY forwarded to MobaXterm), and publish hand-written snapshots on `claude-dash/<host>/state` with `mosquitto_pub` run inside the Mosquitto container (`docker compose exec`); there are no snapshot fixtures in `dev/fixtures/`, so write the JSON inline with `"ts"` set to the current epoch. Check on MobaXterm, and report each point to the user:
1. waiting message, then cards after the first snapshot;
2. working → permission: 2 bells + card flashes; → idle: 1 bell;
3. empty payload removes the host; a second host adds `projet@hôte` titles;
4. resizing: gauges stretch, extra cards collapse into `+N autres sessions`, nothing breaks below 480×320;
5. a second `claude-dash-desktop` exits with the "déjà lancé" message;
6. stop publishing for > 180 s (or publish an old `ts`): banner and dimmed list.

- [ ] **Step 9: Commit**

```bash
git add claude_dash/desktop.py claude_dash/desktop_ui.py pyproject.toml install.sh tests/test_desktop_main.py
git commit -m "Claude_Dashboard desktop: Tk window and claude-dash-desktop command"
```

---

### Task 6: Live setup on sec-p1 and documentation

**Files:**
- Modify: `server/README.md` (new section after « Déploiement sur un serveur distant »), `sketches/JC3248W535C/Claude_Dashboard/README.md` (« Architecture » + « Fichiers »), `CLAUDE.md` at repo root (`Claude_Dashboard/` entry)

**Interfaces:** none (docs and deployment).

- [ ] **Step 1: Ask the user to create the Scaleway device `poste`** (console, IoT Hub, plan Shared): publish `reject #`, subscribe `accept claude-dash/#`; download certificate and key; note the Device ID. Files go to `~/.config/claude-dash/poste.crt` and `poste.key` (chmod 600). Wait for the user.

- [ ] **Step 2: Install and configure on sec-p1**

```bash
./install.sh
install -m 600 desktop.toml.example ~/.config/claude-dash/desktop.toml   # then fill client_id
```

- [ ] **Step 3: Live check** — the user runs `claude-dash-desktop` from a dedicated MobaXterm tab. Expected: within 20 s, the sessions of sec-p1 (and any other server) appear with the same content as the physical screen; trigger a permission prompt in a Claude session and confirm 2 bells + flash.

- [ ] **Step 4: Write the docs**

`server/README.md` section « Version poste de travail (Linux, X déporté) »: what it is, device `poste` (filters, why a dedicated device), `desktop.toml` (keys, `[window] corner`, websockets 443 on sec-p1), launch from a dedicated tab (closes with the tab, single instance), troubleshooting (repeated disconnects → same Device ID used elsewhere; « pas d'affichage X » → X11 forwarding in the MobaXterm session; no sound → X bell setting in MobaXterm). Dashboard README: one line + the arrow to the desktop in the architecture diagram, files list. `CLAUDE.md`: one sentence in the `Claude_Dashboard/` entry (command, device `poste`, launch from a dedicated MobaXterm tab).

- [ ] **Step 5: Commit**

```bash
git add sketches/JC3248W535C/Claude_Dashboard/server/README.md sketches/JC3248W535C/Claude_Dashboard/README.md CLAUDE.md
git commit -m "Claude_Dashboard desktop: document setup and usage"
```
