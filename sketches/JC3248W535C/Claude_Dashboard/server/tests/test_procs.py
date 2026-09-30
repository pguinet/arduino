import os

from claude_dash.procs import find_claude_pid, parse_stat, pid_alive, proc_info


def test_returns_first_claude_ancestor() -> None:
    tree = {300: (200, "sh"), 200: (100, "claude"), 100: (1, "bash")}
    assert find_claude_pid(300, lambda p: tree.get(p)) == 200


def test_node_process_with_claude_cmdline() -> None:
    tree = {
        300: (200, "sh"),
        200: (1, "node /usr/lib/node_modules/@anthropic-ai/claude-code/cli.js"),
    }
    assert find_claude_pid(300, lambda p: tree.get(p)) == 200


def test_none_when_not_found() -> None:
    tree = {300: (1, "sh")}
    assert find_claude_pid(300, lambda p: tree.get(p)) is None


def test_proc_info_self_returns_parent_pid() -> None:
    info = proc_info(os.getpid())
    assert info is not None
    assert info[0] == os.getppid()


def test_proc_info_missing_process() -> None:
    assert proc_info(2**22 + 1) is None  # au-dela de pid_max par defaut


def test_pid_alive_self() -> None:
    assert pid_alive(os.getpid())


def test_parse_stat_comm_with_spaces_and_parens() -> None:
    stat = "1234 ((my (weird) proc)) S 567 1234 1234 0 -1 4194304"
    assert parse_stat(stat) == (567, "(my (weird) proc)")
