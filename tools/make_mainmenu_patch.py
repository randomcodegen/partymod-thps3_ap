from __future__ import annotations

import argparse
import struct
from pathlib import Path

from make_levels_patch import make_bps_patch, qb_checksum


def checksum_token(name: str) -> bytes:
    return b"\x16" + struct.pack("<I", qb_checksum(name))


def integer_token(value: int) -> bytes:
    return b"\x17" + struct.pack("<i", value)


def string_token(value: str) -> bytes:
    encoded = value.encode("ascii") + b"\x00"
    return b"\x1b" + struct.pack("<I", len(encoded)) + encoded


def add_competition_progress_elements(source: bytes) -> bytes:
    def command(name: str, *arguments: bytes) -> bytes:
        return checksum_token(name) + b"".join(arguments)

    def argument(name: str, value: bytes) -> bytes:
        return checksum_token(name) + b"\x07" + value

    def progress_element(element_id: str, x: int, y: int = 103) -> bytes:
        return command(
            "createmenu",
            b"\x03",
            argument("Type", checksum_token("textmenuelement")),
            argument("Id", checksum_token(element_id)),
            argument("text", string_token(" ")),
            checksum_token("static"),
            checksum_token("dont_gray"),
            argument("drawer", checksum_token("goals_text")),
            checksum_token("just_center_x"),
            argument("x", integer_token(x)),
            argument("y", integer_token(y)),
            argument("w", integer_token(150)),
            b"\x04",
        )

    def insert_after(target: bytes, marker: bytes, additions: tuple[bytes, ...]) -> bytes:
        if target.count(marker) != 1:
            raise ValueError(
                f"expected one main-menu insertion marker, found {target.count(marker)}"
            )
        return target.replace(marker, marker + b"\x01" + b"\x01".join(additions), 1)

    def move_element(target: bytes, element_id: str, old_y: int, new_y: int) -> bytes:
        create = checksum_token("createmenu") + b"\x03"
        element = argument("Id", checksum_token(element_id))
        starts = [
            position for position in range(len(target))
            if target.startswith(create, position)
        ]
        matches = [
            (start, starts[index + 1])
            for index, start in enumerate(starts[:-1])
            if element in target[start:start + 64]
        ]
        if len(matches) != 1:
            raise ValueError(f"expected one {element_id} menu definition")
        start, end = matches[0]
        old = argument("y", integer_token(old_y))
        if target[start:end].count(old) != 1:
            raise ValueError(f"expected {element_id} at y={old_y}")
        return target[:start] + target[start:end].replace(
            old, argument("y", integer_token(new_y)), 1) + target[end:]

    medal_id = "AP_CassetteGoals"
    gap_id = "AP_CassetteGaps"
    item_id = "AP_CassetteItems"
    destroy = lambda element_id: command(
        "DestroyElement", argument("Id", checksum_token(element_id)))
    attach = lambda element_id: command(
        "attachchild",
        argument("parent", checksum_token("contain1")),
        argument("child", checksum_token(element_id)),
    )

    source = insert_after(
        source,
        destroy("cassette_menu_line_2"),
        (destroy(medal_id), destroy(gap_id), destroy(item_id)),
    )
    source = insert_after(
        source,
        attach("cassette_menu_line_2"),
        (attach(medal_id), attach(gap_id), attach(item_id)),
    )
    source = insert_after(
        source,
        progress_element("cassette_menu_line_2", 430),
        (progress_element(medal_id, 280), progress_element(gap_id, 430),
         progress_element(item_id, 280, 126)),
    )
    source = move_element(source, "cassette_menu_best_medal_text", 115, 149)
    for element_id in (
        "cassette_menu_best_medal_gold",
        "cassette_menu_best_medal_silver",
        "cassette_menu_best_medal_bronze",
        "cassette_menu_best_medal_none",
    ):
        source = move_element(source, element_id, 110, 144)

    if not source or source[-1] != 0:
        raise ValueError("mainmenu.qb does not end with the QB EOF token")
    names = (medal_id, gap_id, item_id, "APCustomSkaterAllowed")
    if any(name.encode("ascii") + b"\x00" in source for name in names):
        raise ValueError("competition progress checksum names already exist")
    records = b"".join(
        b"\x2b" + struct.pack("<I", qb_checksum(name))
        + name.encode("ascii") + b"\x00"
        for name in names
    )
    return source[:-1] + records + source[-1:]


def add_foundry_cassette_unlock_flag(source: bytes) -> bytes:
    # Vanilla makes Foundry permanently available by setting its career flag
    # every time BuildCassetteMenu runs, then omits GlobalFlag from the Foundry
    # cassette itself. AP needs Foundry to behave like the other eight levels.
    forced_unlock = (
        checksum_token("SetGlobalFlag")
        + checksum_token("flag")
        + b"\x07"
        + checksum_token("LEVEL_UNLOCKED_FOUNDRY")
    )
    if source.count(forced_unlock) != 1:
        raise ValueError(
            "expected exactly one forced Foundry SetGlobalFlag command, "
            f"found {source.count(forced_unlock)}"
        )
    target = source.replace(forced_unlock, b"", 1)

    foundry_goal_params = (
        checksum_token("GoalParams")
        + b"\x07"
        + checksum_token("FoundryGoalParams")
    )
    if target.count(foundry_goal_params) != 1:
        raise ValueError(
            "expected exactly one Foundry GoalParams property, "
            f"found {target.count(foundry_goal_params)}"
        )
    global_flag = (
        checksum_token("GlobalFlag")
        + b"\x07"
        + checksum_token("LEVEL_UNLOCKED_FOUNDRY")
    )
    return target.replace(
        foundry_goal_params,
        global_flag + foundry_goal_params,
        1,
    )


def disable_options_and_pros_autoload(source: bytes) -> bytes:
    options_check = (
        checksum_token("MemCardFileExists")
        + checksum_token("name")
        + b"\x07\x1b\x11\x00\x00\x00Options and Pros\x00"
        + checksum_token("Type")
        + b"\x07"
        + checksum_token("OptionsAndPros")
    )
    if source.count(options_check) != 1:
        raise ValueError(
            "expected exactly one startup Options and Pros existence check, "
            f"found {source.count(options_check)}"
        )
    return source.replace(options_check, b"\x17\x00\x00\x00\x00", 1)


def restrict_custom_skater_entries(source: bytes) -> bytes:
    def after_newline(offset: int) -> int:
        if source[offset:offset + 1] == b"\x01":
            return offset + 1
        if source[offset:offset + 1] == b"\x02":
            return offset + 5
        raise ValueError(f"expected QB newline at {offset}")

    linked_entry = (
        string_token("Create-a-Skater")
        + checksum_token("link") + b"\x07" + checksum_token("pre_cas_main_menu")
        + checksum_token("target") + b"\x07" + string_token("link_to_cas")
    )
    if source.count(linked_entry) != 1:
        raise ValueError(
            "expected one main-menu Create-a-Skater entry, "
            f"found {source.count(linked_entry)}"
        )
    unlinked_entry = (
        string_token("Create-a-Skater")
        + checksum_token("target") + b"\x07" + string_token("link_to_cas")
    )
    source = source.replace(linked_entry, unlinked_entry, 1)

    def unlink_appearance(target: str, expected: int) -> None:
        nonlocal source
        linked = (
            string_token("Change Appearance")
            + checksum_token("link") + b"\x07" + checksum_token("cas_menu_container")
            + checksum_token("target") + b"\x07" + string_token(target)
        )
        if source.count(linked) != expected:
            raise ValueError(
                f"expected {expected} Change Appearance entries for {target}, "
                f"found {source.count(linked)}"
            )
        unlinked = (
            string_token("Change Appearance")
            + checksum_token("target") + b"\x07" + string_token(target)
        )
        source = source.replace(linked, unlinked)

    unlink_appearance("Player1ToChangeAppearance", 6)
    unlink_appearance("Player2ToChangeAppearance", 1)

    def guard_appearance_script(name: str, profile: int) -> None:
        nonlocal source
        script = b"\x23" + checksum_token(name)
        if source.count(script) != 1:
            raise ValueError(f"expected one {name} script, found {source.count(script)}")
        start = after_newline(source.index(script) + len(script))
        command = checksum_token("SetCurrentSkaterProfile") + integer_token(profile)
        if source[start:start + len(command)] != command:
            raise ValueError(f"{name} has an unexpected body")
        end = after_newline(start + len(command))
        guarded = (
            b"\x25\x0e" + checksum_token("APCustomSkaterAllowed") + b"\x0f\x01"
            + command + b"\x01"
            + checksum_token("SwitchToMenu")
            + checksum_token("menu") + b"\x07" + checksum_token("cas_menu_container")
            + checksum_token("DoNotMakeRoot") + b"\x01"
            + b"\x28\x01"
        )
        source = source[:start] + guarded + source[end:]

    guard_appearance_script("Player1ToChangeAppearance", 0)
    guard_appearance_script("Player2ToChangeAppearance", 1)
    return source


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Make THPS3's Foundry cassette use an AP-controlled unlock flag."
        )
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--patched-qb", type=Path)
    args = parser.parse_args()

    source = args.input.read_bytes()
    target = restrict_custom_skater_entries(
        add_competition_progress_elements(
            disable_options_and_pros_autoload(
                add_foundry_cassette_unlock_flag(source)
            )
        )
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(make_bps_patch(source, target))
    if args.patched_qb is not None:
        args.patched_qb.parent.mkdir(parents=True, exist_ok=True)
        args.patched_qb.write_bytes(target)

    print(f"Created {args.output} ({len(source)} -> {len(target)} QB bytes)")


if __name__ == "__main__":
    main()
