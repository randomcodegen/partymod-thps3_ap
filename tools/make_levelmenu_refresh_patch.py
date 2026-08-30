from __future__ import annotations

import argparse
from pathlib import Path

from make_levels_patch import make_bps_patch


def checksum_table_start(qb: bytes) -> int:
    for candidate, byte in enumerate(qb):
        if byte != 0x2B:
            continue
        cursor = candidate
        records = 0
        while cursor < len(qb) - 1 and qb[cursor] == 0x2B:
            cursor += 5
            try:
                string_end = qb.index(0, cursor)
            except ValueError:
                break
            if any(value < 0x20 or value > 0x7E for value in qb[cursor:string_end]):
                break
            cursor = string_end + 1
            records += 1
        if records and cursor == len(qb) - 1 and qb[cursor] == 0:
            return candidate
    raise ValueError("could not locate the trailing QB checksum-name table")


def add_script_header_line_number(qb: bytes) -> bytes:
    script_header = qb.find(b"\x23\x16")
    if script_header < 0:
        raise ValueError("compiled refresh QB has no script header")
    first_body_token = script_header + 6
    if qb[first_body_token] != 0x01:
        raise ValueError(
            "compiled refresh script does not use the expected generic newline"
        )

    # THPS3's CScript constructor advances over a five-byte NewLineNumber
    # token after the script checksum. NeverScript emits a one-byte NewLine
    # here, which registers correctly but causes execution to start four bytes
    # into the first statement. Preserve generic newlines elsewhere.
    return qb[:first_body_token] + b"\x02\x01\x00\x00\x00" + qb[first_body_token + 1:]


def add_refresh_script(source: bytes, refresh_script: bytes) -> bytes:
    if not source or source[-1] != 0:
        raise ValueError("source levelmenu.qb does not end with the QB EOF token")
    if not refresh_script or refresh_script[-1] != 0:
        raise ValueError("compiled refresh script does not end with the QB EOF token")
    if b"AP_Refresh_Level_Menus\x00" in source:
        raise ValueError("source levelmenu.qb already contains the AP refresh script")

    refresh_script = add_script_header_line_number(refresh_script)

    # Script bytecode must precede the trailing checksum-name table. Merge the
    # independently compiled script's code into that section, then concatenate
    # both checksum tables under one final EOF token.
    source_table = checksum_table_start(source)
    refresh_table = checksum_table_start(refresh_script)
    return (
        source[:source_table]
        + refresh_script[:refresh_table]
        + source[source_table:-1]
        + refresh_script[refresh_table:]
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Append the AP live level-menu refresh script to levelmenu.qb."
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--refresh-script", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--patched-qb", type=Path)
    args = parser.parse_args()

    source = args.input.read_bytes()
    target = add_refresh_script(source, args.refresh_script.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(make_bps_patch(source, target))
    if args.patched_qb is not None:
        args.patched_qb.parent.mkdir(parents=True, exist_ok=True)
        args.patched_qb.write_bytes(target)

    print(f"Created {args.output} ({len(source)} -> {len(target)} QB bytes)")


if __name__ == "__main__":
    main()
