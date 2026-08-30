from __future__ import annotations

import argparse
import struct
from pathlib import Path

from make_levels_patch import make_bps_patch, qb_checksum


def token(name: str) -> bytes:
    return b"\x16" + struct.pack("<I", qb_checksum(name))


def after_newline(source: bytes, offset: int) -> int:
    if source[offset:offset + 1] == b"\x01":
        return offset + 1
    if source[offset:offset + 1] == b"\x02":
        return offset + 5
    raise ValueError(f"expected QB newline at {offset}")


def add_comm_lip_starts(source: bytes) -> bytes:
    over_comm = (
        token("StartGap") + token("gapID") + b"\x07" + token("OverComm")
        + token("flags") + b"\x07" + token("PURE_AIR") + b"\x01"
    )
    comm_lip = (
        token("StartGap") + token("gapID") + b"\x07" + token("CommLip")
        + token("flags") + b"\x07\x05"
        + token("CANCEL_MANUAL") + token("CANCEL_GROUND")
        + token("CANCEL_WALL") + token("CANCEL_RAIL") + b"\x06\x01"
    )
    target = source
    for script in (
        "shpsf_HelipadQuarterPipe01Script",
        "shpsf_HelipadQuarterPipe02Script",
        "shpsf_HelipadQuarterPipe01aScript",
        "shpsf_HelipadQuarterPipe02aScript",
    ):
        header = b"\x23" + token(script)
        if target.count(header) != 1:
            raise ValueError(f"expected one {script}, found {target.count(header)}")
        body = after_newline(target, target.index(header) + len(header))
        command = over_comm[:-1]
        if target[body:body + len(command)] != command:
            raise ValueError(f"unexpected {script} body")
        end = after_newline(target, body + len(command))
        if target[end:end + 1] != b"\x24":
            raise ValueError(f"expected {script} terminator")
        target = target[:end] + comm_lip + target[end:]
    return target


def main() -> None:
    parser = argparse.ArgumentParser(description="Repair Cruise Ship Comm Lip starts.")
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--patched-qb", type=Path)
    args = parser.parse_args()
    source = args.input.read_bytes()
    target = add_comm_lip_starts(source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(make_bps_patch(source, target))
    if args.patched_qb:
        args.patched_qb.parent.mkdir(parents=True, exist_ok=True)
        args.patched_qb.write_bytes(target)


if __name__ == "__main__":
    main()
