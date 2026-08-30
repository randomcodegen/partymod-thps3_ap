from __future__ import annotations

import argparse
import struct
from pathlib import Path

from make_levels_patch import make_bps_patch, qb_checksum
from make_mainmenu_patch import checksum_token


LOAD_WRAPPERS = (
    ("_CreationOptionsLoadCAS", "CreationOptionsLoadCAS"),
    ("_CareerMenuLoadCAS", "CareerMenuLoadCAS"),
    ("_SingleSessionLoadCAS", "SingleSessionLoadCAS"),
    ("_FreeSkateLoadCAS", "FreeSkateLoadCAS"),
    ("_Player1LoadCAS", "Player1LoadCAS"),
    ("_Player2LoadCAS", "Player2LoadCAS"),
)


def guard_custom_skater_loads(source: bytes) -> bytes:
    def after_newline(offset: int) -> int:
        if source[offset:offset + 1] == b"\x01":
            return offset + 1
        if source[offset:offset + 1] == b"\x02":
            return offset + 5
        raise ValueError(f"expected QB newline at {offset}")

    for wrapper, target in LOAD_WRAPPERS:
        script = b"\x23" + checksum_token(wrapper)
        if source.count(script) != 1:
            raise ValueError(f"expected one {wrapper} script, found {source.count(script)}")
        start = after_newline(source.index(script) + len(script))
        spawn = checksum_token("SpawnScript") + checksum_token(target)
        if source[start:start + len(spawn)] != spawn:
            raise ValueError(f"{wrapper} has an unexpected body")
        end = after_newline(start + len(spawn))
        guarded = (
            b"\x25\x0e" + checksum_token("APCustomSkaterAllowed") + b"\x0f\x01"
            + spawn + b"\x01"
            + b"\x28\x01"
        )
        source = source[:start] + guarded + source[end:]

    name = b"APCustomSkaterAllowed\x00"
    if not source or source[-1] != 0 or name in source:
        raise ValueError("unexpected memcard.qb checksum table")
    record = b"\x2b" + struct.pack("<I", qb_checksum("APCustomSkaterAllowed")) + name
    return source[:-1] + record + source[-1:]


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Block saved custom-skater loads unless AP selected CAS."
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--patched-qb", type=Path)
    args = parser.parse_args()

    source = args.input.read_bytes()
    target = guard_custom_skater_loads(source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(make_bps_patch(source, target))
    if args.patched_qb is not None:
        args.patched_qb.parent.mkdir(parents=True, exist_ok=True)
        args.patched_qb.write_bytes(target)
    print(f"Created {args.output} ({len(source)} -> {len(target)} QB bytes)")


if __name__ == "__main__":
    main()
