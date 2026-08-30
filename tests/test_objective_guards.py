import inspect
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))

from make_objective_guards_patch import (
    PATCH_GROUPS,
    add_objective_guards,
    guard_bytes,
    token,
)


def test_every_configured_entry_gets_a_read_only_unlock_guard() -> None:
    for group, guards in PATCH_GROUPS.items():
        source = b"".join(
            b"\x23" + token(guard.script) + b"\x01" + token("OriginalBody")
            + b"\x01\x24\x00"
            for guard in guards
        )
        target = add_objective_guards(source, group)
        for guard in guards:
            start = target.index(b"\x23" + token(guard.script))
            body = start + 1 + len(token(guard.script)) + 1
            assert target[body:body + len(guard_bytes(guard))] == guard_bytes(guard)


def test_guard_uses_the_stock_qb_command_if_shape() -> None:
    guard = PATCH_GROUPS["foundry"][0]
    encoded = guard_bytes(guard)
    command = token("APObjectiveUnlocked")
    goal = token("goal") + b"\x07" + token(guard.goal)
    jump = token(guard.jump) + token(guard.resume)

    # Walk the control tokens exactly as THPS3 does: command-style if ends at
    # newline, then an empty true branch, else line, jump line, and endif line.
    cursor = 0
    assert encoded[cursor] == 0x25
    cursor += 1
    assert encoded[cursor:cursor + len(command)] == command
    cursor += len(command)
    assert encoded[cursor:cursor + len(goal)] == goal
    cursor += len(goal)
    assert encoded[cursor] == 0x01
    cursor += 1
    assert encoded[cursor:cursor + 2] == b"\x26\x01"
    cursor += 2
    assert encoded[cursor:cursor + len(jump)] == jump
    cursor += len(jump)
    assert encoded[cursor:cursor + 2] == b"\x01\x28"
    cursor += 2
    assert encoded[cursor:] == b"\x01"
    encoder_source = inspect.getsource(guard_bytes)
    assert "\\x25\\x0e" not in encoder_source
    assert "\\x0f\\x01" not in encoder_source


def test_shared_script_parameters_remain_before_the_guard() -> None:
    guard = PATCH_GROUPS["airport"][1]  # CPF_AP_BustingFlag Flagname=...
    declaration = token("Flagname") + b"\x07" + token("TRG_Flag")
    source = (
        b"\x23" + token(guard.script) + declaration + b"\x02\x59\x0c\x00\x00"
        + token("OriginalBody") + b"\x01\x24\x00"
    )
    # Isolate the parameter-bearing script while retaining the production helper.
    from make_objective_guards_patch import after_script_declaration
    offset = 1 + len(token(guard.script))
    body = after_script_declaration(source, offset)
    assert source[offset:body].startswith(declaration)
    assert body == offset + len(declaration) + 5


def test_all_18_objectives_have_a_safe_locked_path() -> None:
    configured = {
        ("foundry", guard.goal) for guard in PATCH_GROUPS["foundry"]
    } | {
        ("canada", guard.goal) for guard in PATCH_GROUPS["canada"]
    } | {
        ("suburbia", guard.goal) for guard in PATCH_GROUPS["suburbia"]
    } | {
        ("airport", guard.goal) for guard in PATCH_GROUPS["airport"]
    } | {
        ("los_angeles", guard.goal)
        for group in ("los_angeles_common", "los_angeles_level")
        for guard in PATCH_GROUPS[group]
    } | {
        ("cruise_ship", guard.goal) for guard in PATCH_GROUPS["cruise_ship"]
    }
    # Airport G7 is guarded at setup: locked terrorists do not exist. Its
    # undecompiled object-exception entry is deliberately not redirected.
    configured.add(("airport", "GOAL_SCRIPTED2"))
    assert configured == {
        (level, f"GOAL_SCRIPTED{goal}")
        for level in (
            "foundry", "canada", "suburbia", "airport",
            "los_angeles", "cruise_ship",
        )
        for goal in range(1, 4)
    }
