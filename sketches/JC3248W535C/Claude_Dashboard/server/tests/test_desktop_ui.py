"""Tk layer: cards must be updated in place, not destroyed and rebuilt (over remote
X a rebuild blanks the whole list for a moment). Needs Tk and an X display:
skipped in CI and in run-checks.sh."""

import queue
import time
from collections.abc import Iterator
from dataclasses import replace

import pytest

# ImportError, not only ModuleNotFoundError: slim images ship tkinter without libtk.
tk = pytest.importorskip("tkinter", exc_type=ImportError)

from claude_dash.desktop_ui import DesktopWindow  # noqa: E402
from claude_dash.model import HostSnapshot, Limits, Session, State  # noqa: E402
from claude_dash.subscriber import StatusEvent  # noqa: E402
from claude_dash.view import ConnectionStatus, Controller, Phase  # noqa: E402

NOW = int(time.time())


@pytest.fixture
def window(monkeypatch: pytest.MonkeyPatch) -> Iterator[DesktopWindow]:
    try:
        root = tk.Tk()
    except tk.TclError:
        pytest.skip("no X display")
    root.withdraw()  # never shown on the user's screen
    controller = Controller("h:1")
    controller.on_status(ConnectionStatus(Phase.CONNECTED))
    win = DesktopWindow(root, controller, queue.Queue(), "top-right")
    # A withdrawn window has no real height: give the list room for 6 cards.
    monkeypatch.setattr(win, "_available_height", lambda: 6 * win._card_height())
    yield win
    root.destroy()


def sess(sid: str, state: State, since: int, tool: str = "") -> Session:
    return Session(id=sid, project=sid, model="Opus", tool=tool, state=state, since=since, ctx=40)


def show(win: DesktopWindow, sessions: list[Session]) -> None:
    snap = HostSnapshot(host="srv", ts=NOW, limits=Limits(), sessions=tuple(sessions))
    win.controller.on_snapshot(snap, NOW)
    win._render(win.controller.screen(NOW))


def test_content_change_updates_cards_in_place(window: DesktopWindow) -> None:
    a = sess("a", State.WORKING, NOW - 100, tool="Edit")
    b = sess("b", State.WORKING, NOW - 50)
    show(window, [a, b])
    before = [slot.inner for slot in window.slots]
    show(window, [replace(a, tool="Read"), b])
    assert [slot.inner for slot in window.slots] == before
    assert all(slot.inner.winfo_exists() for slot in window.slots)
    assert window.cards[("srv", "a")].tool.cget("text") == "Read"


def test_state_change_recolors_in_place(window: DesktopWindow) -> None:
    a = sess("a", State.WORKING, NOW - 100)
    show(window, [a])
    inner = window.slots[0].inner
    show(window, [replace(a, state=State.PERMISSION)])
    slot = window.slots[0]
    assert slot.inner is inner
    assert slot.state.cget("text") == "PERMISSION"
    assert slot.outer.cget("bg") == "#f72585"


def test_more_sessions_reuse_existing_slots(window: DesktopWindow) -> None:
    show(window, [sess("a", State.WORKING, NOW - 100)])
    first = window.slots[0].inner
    show(window, [sess("a", State.WORKING, NOW - 100), sess("b", State.WORKING, NOW - 50)])
    assert window.slots[0].inner is first
    assert len(window.slots) == 2


def test_fewer_sessions_drop_extra_slots(window: DesktopWindow) -> None:
    show(window, [sess("a", State.WORKING, NOW - 100), sess("b", State.WORKING, NOW - 50)])
    gone = window.slots[1].inner
    show(window, [sess("a", State.WORKING, NOW - 100)])
    assert len(window.slots) == 1
    assert not gone.winfo_exists()


def test_overflow_line_counts_hidden_cards(window: DesktopWindow) -> None:
    show(window, [sess(f"s{i}", State.WORKING, NOW - i) for i in range(9)])
    assert len(window.slots) == 5  # 6 card heights minus the "+N" line
    assert window.more.cget("text") == "+4 autres sessions"
    assert window.more.winfo_manager() == "pack"


@pytest.mark.parametrize("loop", ["_poll", "_tick", "_expire"])
def test_loops_survive_an_exception(
    window: DesktopWindow, monkeypatch: pytest.MonkeyPatch, loop: str
) -> None:
    def boom(*_: object) -> None:
        raise ValueError("boom")

    scheduled: list[object] = []
    monkeypatch.setattr(window.controller, "screen", boom)
    monkeypatch.setattr(window.controller, "expire", boom)
    monkeypatch.setattr(window.root, "after", lambda ms, fn: scheduled.append(fn))
    window.events.put(StatusEvent(ConnectionStatus(Phase.DISCONNECTED, "x")))
    getattr(window, loop)()
    assert getattr(window, loop) in scheduled
