from __future__ import annotations

import argparse
import struct
from pathlib import Path

from make_levels_patch import make_bps_patch, qb_checksum
from make_levelmenu_refresh_patch import checksum_table_start
from make_mainmenu_patch import checksum_token


def string_token(value: str) -> bytes:
    encoded = value.encode("ascii") + b"\x00"
    return b"\x1b" + len(encoded).to_bytes(4, "little") + encoded


def add_career_gap_column(source: bytes) -> bytes:
    def argument(name: str, value: bytes) -> bytes:
        return checksum_token(name) + b"\x07" + value

    def command(name: str, *arguments: bytes) -> bytes:
        return checksum_token(name) + b"".join(arguments)

    def float_token(value: float) -> bytes:
        return b"\x1a" + struct.pack("<f", value)

    def text_element(element_id: str, text: str, header: bool = False) -> bytes:
        return (
            b"\x03"
            + argument("type", checksum_token("textmenuelement"))
            + argument("id", checksum_token(element_id))
            + argument("text", string_token(text))
            + (checksum_token("static") + argument(
                "drawer", checksum_token("main_smaller")) if header else b"")
            + b"\x04"
        )

    create_outer = (
        checksum_token("CreateAndAttachMenu") + b"\x03"
        + argument("type", checksum_token("verticalmenu"))
        + argument("id", checksum_token("career_change_level_menu"))
    )
    destroy_names = command(
        "DestroyElement", argument("id", checksum_token("career_level_names")))
    if source.count(create_outer) != 1 or source.count(destroy_names) != 1:
        raise ValueError("expected one career change-level menu")
    start = source.index(create_outer)
    end = source.index(destroy_names, start)
    outer = source[start:end]
    old_x = argument("x", float_token(140.0))
    old_w = argument("w", float_token(360.0))
    if outer.count(old_x) != 1 or outer.count(old_w) != 1:
        raise ValueError("career change-level menu has unexpected dimensions")
    outer = outer.replace(old_x, argument("x", float_token(16.0)), 1)
    outer = outer.replace(old_w, argument("w", float_token(608.0)), 1)
    source = source[:start] + outer + source[end:]

    gap_id = "career_level_gaps"
    item_id = "career_level_items"
    goals_layout = (
        checksum_token("createmenu") + b"\x03"
        + argument("type", checksum_token("verticalmenu"))
        + argument("id", checksum_token("career_level_goals"))
        + argument("x", float_token(280.0))
        + argument("y", float_token(0.0))
        + argument("w", float_token(80.0))
    )
    if source.count(goals_layout) != 1:
        raise ValueError("career goals column has unexpected dimensions")
    source = source.replace(
        goals_layout,
        goals_layout[:-5] + float_token(88.0),
        1,
    )
    destroy_goals = command(
        "DestroyElement", argument("id", checksum_token("career_level_goals")))
    destroy_gaps = command(
        "DestroyElement", argument("id", checksum_token(gap_id)))
    destroy_items = command(
        "DestroyElement", argument("id", checksum_token(item_id)))
    if source.count(destroy_goals) != 1:
        raise ValueError("expected one career goals-column cleanup")
    source = source.replace(
        destroy_goals,
        destroy_goals + b"\x01" + destroy_gaps + b"\x01" + destroy_items,
        1)

    children = text_element("AP_LevelGapHeader", "Gaps", True) + b"".join(
        text_element(f"AP_LevelGap{level}", "0/0") for level in range(1, 10)
    )
    gap_menu = command(
        "createmenu",
        b"\x03",
        argument("type", checksum_token("verticalmenu")),
        argument("id", checksum_token(gap_id)),
        argument("x", float_token(368.0)),
        argument("y", float_token(0.0)),
        argument("w", float_token(120.0)),
        argument("h", float_token(336.0)),
        checksum_token("just_center_x"),
        checksum_token("just_center_y"),
        checksum_token("not_rounded"),
        checksum_token("static"),
        argument("children", b"\x05" + children + b"\x06"),
        b"\x04",
    )
    item_children = text_element("AP_LevelItemHeader", "Items", True) + b"".join(
        text_element(f"AP_LevelItem{level}", "0/0") for level in range(1, 10)
    )
    item_menu = command(
        "createmenu",
        b"\x03",
        argument("type", checksum_token("verticalmenu")),
        argument("id", checksum_token(item_id)),
        argument("x", float_token(488.0)),
        argument("y", float_token(0.0)),
        argument("w", float_token(120.0)),
        argument("h", float_token(336.0)),
        checksum_token("just_center_x"),
        checksum_token("just_center_y"),
        checksum_token("not_rounded"),
        checksum_token("static"),
        argument("children", b"\x05" + item_children + b"\x06"),
        b"\x04",
    )
    attach_names = command(
        "attachchild",
        argument("parent", checksum_token("career_levels_multi_container")),
        argument("child", checksum_token("career_level_names")),
    )
    attach_goals = command(
        "attachchild",
        argument("parent", checksum_token("career_levels_multi_container")),
        argument("child", checksum_token("career_level_goals")),
    )
    attach_gaps = command(
        "attachchild",
        argument("parent", checksum_token("career_levels_multi_container")),
        argument("child", checksum_token(gap_id)),
    )
    attach_items = command(
        "attachchild",
        argument("parent", checksum_token("career_levels_multi_container")),
        argument("child", checksum_token(item_id)),
    )
    if source.count(attach_names) != 1 or source.count(attach_goals) != 1:
        raise ValueError("expected one pair of career level-column attachments")
    source = source.replace(
        attach_names, gap_menu + b"\x01" + item_menu + b"\x01" + attach_names, 1)
    return source.replace(
        attach_goals,
        attach_goals + b"\x01" + attach_gaps + b"\x01" + attach_items,
        1)


def keep_pause_menu_music_playing(source: bytes) -> bytes:
    pause_music = checksum_token("PauseMusic") + b"\x17\x01\x00\x00\x00"
    if source.count(pause_music) != 1:
        raise ValueError(
            f"expected one PauseMusic 1 command, found {source.count(pause_music)}"
        )
    return source.replace(
        pause_music,
        checksum_token("PauseMusic") + b"\x17\x00\x00\x00\x00",
        1,
    )


def add_gap_list_menu(source: bytes, gap_menu: bytes) -> bytes:
    goal_line = (
        checksum_token("AddLine")
        + checksum_token("parent") + b"\x07" + checksum_token("game_menu")
        + checksum_token("id") + b"\x07" + checksum_token("ListAllGoals")
        + checksum_token("text") + b"\x07" + string_token("View Goals List")
        + checksum_token("target") + b"\x07" + string_token("View_ListAllGoals")
        + checksum_token("kill_menu")
    )
    if source.count(goal_line) != 1:
        raise ValueError(f"expected one View Goals List line, found {source.count(goal_line)}")

    gap_line = (
        checksum_token("AddLine")
        + checksum_token("parent") + b"\x07" + checksum_token("game_menu")
        + checksum_token("id") + b"\x07" + checksum_token("AP_ViewGapList")
        + checksum_token("text") + b"\x07" + string_token("View Gap List")
        + checksum_token("link") + b"\x07" + checksum_token("APGapMenu")
    )
    marker_line = (
        checksum_token("AddLine")
        + checksum_token("parent") + b"\x07" + checksum_token("game_menu")
        + checksum_token("id") + b"\x07" + checksum_token("AP_ToggleCollectibleMarkers")
        + checksum_token("text") + b"\x07" + string_token("Item Markers: Off")
        + checksum_token("target") + b"\x07" + string_token("AP_ToggleCollectibleMarkers")
    )
    goal_start = source.find(goal_line)
    career_if = b"\x25" + checksum_token("IsCareerMode")
    career_start = source.rfind(career_if, 0, goal_start)
    insert_at = career_start + len(career_if)
    if career_start < 0 or source[insert_at:insert_at + 1] != b"\x02":
        raise ValueError("could not find the career pause-menu branch")
    insert_at += 5  # numbered newline
    target = (
        source[:insert_at] + gap_line + b"\x01" + marker_line + b"\x01"
        + source[insert_at:]
    )

    create_menu = checksum_token("CreateAndAttachMenu") + checksum_token("APGapMenu")
    restart_menu = (
        checksum_token("CreateAndAttachMenu")
        + b"\x03"
        + checksum_token("Type") + b"\x07" + checksum_token("verticalmenu")
        + checksum_token("id") + b"\x07" + checksum_token("restart_menu")
    )
    if target.count(restart_menu) != 1:
        raise ValueError("expected one restart_menu creation")
    target = target.replace(restart_menu, create_menu + b"\x01" + restart_menu, 1)

    if not gap_menu or gap_menu[-1] != 0:
        raise ValueError("compiled gap menu does not end with the QB EOF token")
    source_table = checksum_table_start(target)
    gap_table = checksum_table_start(gap_menu)
    return (
        target[:source_table]
        + gap_menu[:gap_table]
        + target[source_table:-1]
        + gap_menu[gap_table:]
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Add the AP in-run gap-list command.")
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--gap-menu", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--patched-qb", type=Path)
    args = parser.parse_args()
    source = args.input.read_bytes()
    target = add_gap_list_menu(
        add_career_gap_column(keep_pause_menu_music_playing(source)),
        args.gap_menu.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(make_bps_patch(source, target))
    if args.patched_qb:
        args.patched_qb.parent.mkdir(parents=True, exist_ok=True)
        args.patched_qb.write_bytes(target)
    print(f"Created {args.output} ({len(source)} -> {len(target)} QB bytes)")


if __name__ == "__main__":
    main()
