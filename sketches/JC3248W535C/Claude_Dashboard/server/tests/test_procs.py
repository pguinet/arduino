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
