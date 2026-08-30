from __future__ import annotations

import argparse
import struct
from pathlib import Path

from make_levels_patch import make_bps_patch, qb_checksum
from make_mainmenu_patch import checksum_token


def guard_custom_skater_entry(source: bytes) -> bytes:
    def after_newline(offset: int) -> int:
        if source[offset:offset + 1] == b"\x01":
            return offset + 1
        if source[offset:offset + 1] == b"\x02":
            return offset + 5
        raise ValueError(f"expected QB newline at {offset}")

    script = b"\x23" + checksum_token("link_to_cas")
    if source.count(script) != 1:
        raise ValueError(f"expected one link_to_cas script, found {source.count(script)}")
    start = after_newline(source.index(script) + len(script))
    load_custom = (
        checksum_token("load_pro_skater")
        + checksum_token("name") + b"\x07" + checksum_token("custom")
    )
    if source[start:start + len(load_custom)] != load_custom:
        raise ValueError("link_to_cas does not load the custom skater")
    second = after_newline(start + len(load_custom))
    camera = checksum_token("MainMenuToPlayer1CamAnim")
    if source[second:second + len(camera)] != camera:
        raise ValueError("link_to_cas does not run the expected camera transition")
    end = after_newline(second + len(camera))
    guarded = (
        b"\x25\x0e" + checksum_token("APCustomSkaterAllowed") + b"\x0f\x01"
        + load_custom + b"\x01"
        + camera + b"\x01"
        + checksum_token("SwitchToMenu")
        + checksum_token("menu") + b"\x07" + checksum_token("pre_cas_main_menu")
        + checksum_token("DoNotMakeRoot") + b"\x01"
        + b"\x28\x01"
    )
    target = source[:start] + guarded + source[end:]
    name = b"APCustomSkaterAllowed\x00"
    if not target or target[-1] != 0 or name in target:
        raise ValueError("unexpected casmenu.qb checksum table")
    record = b"\x2b" + struct.pack("<I", qb_checksum("APCustomSkaterAllowed")) + name
    return target[:-1] + record + target[-1:]


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Only enter Create-A-Skater when AP selected the custom skater."
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--patched-qb", type=Path)
    args = parser.parse_args()

    source = args.input.read_bytes()
    target = guard_custom_skater_entry(source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(make_bps_patch(source, target))
    if args.patched_qb is not None:
        args.patched_qb.parent.mkdir(parents=True, exist_ok=True)
        args.patched_qb.write_bytes(target)
    print(f"Created {args.output} ({len(source)} -> {len(target)} QB bytes)")


if __name__ == "__main__":
    main()
