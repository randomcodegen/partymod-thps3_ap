from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))

from make_levels_patch import make_bps_patch
from make_mainmenu_status_patch import apply_bps, decode_number


def patch_actions(patch: bytes) -> list[tuple[int, int]]:
    offset = 4
    _, offset = decode_number(patch, offset)
    target_size, offset = decode_number(patch, offset)
    metadata_size, offset = decode_number(patch, offset)
    offset += metadata_size
    actions = []
    produced = 0
    while produced < target_size:
        header, offset = decode_number(patch, offset)
        action = header & 3
        length = (header >> 2) + 1
        actions.append((action, length))
        produced += length
        if action == 1:
            offset += length
        elif action in (2, 3):
            _, offset = decode_number(patch, offset)
    return actions


def test_bps_uses_the_original_file_instead_of_embedding_the_target() -> None:
    source = bytes(range(256)) * 8
    target = source[:700] + b"Archipelago" + source[700:1400] + b"AP" + source[1402:]
    patch = make_bps_patch(source, target)
    actions = patch_actions(patch)

    assert apply_bps(source, patch) == target
    assert sum(length for action, length in actions if action in (0, 2)) > 1900
    assert sum(length for action, length in actions if action == 1) < 32


def test_shipped_patches_are_source_referencing_deltas() -> None:
    patch_root = Path(__file__).parents[1] / "patches"
    for name in (
        "game_ap.bps", "gameflow_ap.bps", "goal_scripts_ap.bps",
        "judges_ap.bps", "levelmenu_ap.bps", "levels.bps", "mainmenu_ap.bps",
        "casmenu_ap.bps",
        "memcard_ap.bps",
        "netmessages_ap.bps", "shp_ap.bps", "cjr_scripts_ap.bps",
        "ajc_scripts_ap.bps", "alf_scripts_ap.bps", "cpf_scripts_ap.bps",
        "la_objectives_ap.bps", "bdj_scripts_ap.bps",
    ):
        actions = patch_actions((patch_root / name).read_bytes())
        source_bytes = sum(length for action, length in actions if action in (0, 2))
        literal_bytes = sum(length for action, length in actions if action == 1)
        assert source_bytes > literal_bytes, name
        assert max(length for action, length in actions if action == 1) <= 1024, name


def test_gameflow_keeps_the_current_music_track() -> None:
    from make_gameflow_patch import (
        keep_current_music_track, keep_transition_music_playing, pause_music)
    from make_levels_patch import qb_checksum
    import struct

    skip = b"\x16" + struct.pack("<I", qb_checksum("SkipMusicTrack"))
    no_op = b"\x16" + struct.pack("<I", qb_checksum("NullScript"))

    assert keep_current_music_track(b"before" + skip + b"after") == (
        b"before" + no_op + b"after"
    )
    assert keep_transition_music_playing(pause_music(1) * 2, 2) == (
        pause_music(0) * 2
    )


def test_mainmenu_routes_custom_skater_entry_through_its_target() -> None:
    from make_mainmenu_patch import (
        checksum_token, restrict_custom_skater_entries, string_token)

    def entry(label: str, target: str, link: bytes = b"") -> bytes:
        return (
            checksum_token("createmenu") + b"\x03"
            + string_token(label) + link
            + checksum_token("target") + b"\x07" + string_token(target)
            + b"\x04"
        )

    source = entry(
        "Create-a-Skater", "link_to_cas",
        checksum_token("link") + b"\x07" + checksum_token("pre_cas_main_menu"),
    )
    appearance_link = (
        checksum_token("link") + b"\x07" + checksum_token("cas_menu_container")
    )
    source += entry(
        "Change Appearance", "Player1ToChangeAppearance", appearance_link
    ) * 6
    source += entry(
        "Change Appearance", "Player2ToChangeAppearance", appearance_link
    )
    source += (
        b"\x23" + checksum_token("Player1ToChangeAppearance") + b"\x01"
        + checksum_token("SetCurrentSkaterProfile") + b"\x17\x00\x00\x00\x00"
        + b"\x01\x24"
        + b"\x23" + checksum_token("Player2ToChangeAppearance") + b"\x01"
        + checksum_token("SetCurrentSkaterProfile") + b"\x17\x01\x00\x00\x00"
        + b"\x01\x24"
    )
    target = restrict_custom_skater_entries(source)

    assert checksum_token("link") not in target
    assert string_token("link_to_cas") in target
    assert target.count(checksum_token("APCustomSkaterAllowed")) == 2
    assert target.count(checksum_token("SwitchToMenu")) == 2


def test_casmenu_blocks_unselected_custom_skater_transition() -> None:
    from make_casmenu_patch import guard_custom_skater_entry
    from make_mainmenu_patch import checksum_token

    body = (
        checksum_token("load_pro_skater")
        + checksum_token("name") + b"\x07" + checksum_token("custom")
        + b"\x01" + checksum_token("MainMenuToPlayer1CamAnim") + b"\x01"
    )
    source = b"\x23" + checksum_token("link_to_cas") + b"\x01" + body + b"\x24\x00"
    target = guard_custom_skater_entry(source)

    assert (
        b"\x25\x0e" + checksum_token("APCustomSkaterAllowed") + b"\x0f\x01"
    ) in target
    assert checksum_token("SwitchToMenu") in target
    assert checksum_token("pre_cas_main_menu") in target
    assert b"\x28\x01\x24" in target


def test_memcard_blocks_every_custom_skater_load_entry() -> None:
    from make_memcard_patch import LOAD_WRAPPERS, guard_custom_skater_loads
    from make_mainmenu_patch import checksum_token

    source = b""
    for wrapper, load_target in LOAD_WRAPPERS:
        source += (
            b"\x23" + checksum_token(wrapper) + b"\x01"
            + checksum_token("SpawnScript") + checksum_token(load_target)
            + b"\x01\x24"
        )
    target = guard_custom_skater_loads(source + b"\x00")

    guard = b"\x25\x0e" + checksum_token("APCustomSkaterAllowed") + b"\x0f\x01"
    assert target.count(guard) == len(LOAD_WRAPPERS)
    assert target.count(b"\x28\x01") == len(LOAD_WRAPPERS)
