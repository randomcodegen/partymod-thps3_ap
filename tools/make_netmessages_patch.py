from __future__ import annotations

import argparse
import struct
from pathlib import Path

try:
    from .make_levels_patch import make_bps_patch, qb_checksum
except ImportError:
    from make_levels_patch import make_bps_patch, qb_checksum


def compact_chat_props(source: bytes) -> bytes:
    target = bytearray(source)
    old_scale = b"\x1a" + struct.pack("<f", 1.01)

    old_dims = (
        b"\x16" + struct.pack("<I", qb_checksum("dims")) + b"\x07\x1f"
        + struct.pack("<ff", 600.0, 112.0)
    )
    for line in range(1, 10):
        new_scale = b"\x1a" + struct.pack("<f", 0.40)
        name = f"chat{line}props"
        marker = (
            b"\x16"
            + struct.pack("<I", qb_checksum(name))
            + b"\x07\x03"
        )
        offset = source.find(marker)
        if offset < 0 or source.find(marker, offset + 1) >= 0:
            raise ValueError(f"expected exactly one {name} structure")

        end = offset + 339
        if target[offset:end].count(old_scale) != 2:
            raise ValueError(f"expected two 1.01 scale values in {name}")
        pos = (
            b"\x16" + struct.pack("<I", qb_checksum("pos")) + b"\x07\x1f"
            + struct.pack("<ff", 320.0, float(130 + min(line, 5) * 20))
        )
        if target[offset:end].count(pos) != 1:
            raise ValueError(f"expected stock position for {name}")
        target[offset:end] = target[offset:end].replace(old_scale, new_scale)
        if line <= 7:
            compact_pos = pos[:-8] + struct.pack("<ff", *chat_position(line))
            target[offset:end] = target[offset:end].replace(pos, compact_pos)
        if target[offset:end].count(old_dims) != 1:
            raise ValueError(f"expected one 600x112 boundary in {name}")

    return bytes(target)


def chat_position(line: int) -> tuple[float, float]:
    if line <= 5:
        return 320.0, float(170 + (line - 1) * 15)
    if line == 6:
        return 320.0, 35.0
    if line == 7:
        return 560.0, 15.0
    return 320.0, 230.0


def use_archipelago_colors(source: bytes) -> bytes:
    target = source
    colors = (
        ((50.0, 80.0, 128.0), (109.0, 139.0, 232.0)),  # useful: slateblue
        ((180.0, 160.0, 0.0), (175.0, 153.0, 239.0)),  # progression: plum
        ((110.0, 50.0, 50.0), (255.0, 255.0, 0.0)),  # player: ANSI yellow
        ((128.0, 128.0, 128.0), (255.0, 255.0, 255.0)),  # normal/filler: white
    )
    for old, new in colors:
        old_bytes = struct.pack("<fff", *old)
        if target.count(old_bytes) != 12:
            raise ValueError(f"expected twelve {old} chat colors")
        target = target.replace(old_bytes, struct.pack("<fff", *new))
    return target


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Shrink THPS3's five native AP chat rows."
        )
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--patched-qb", type=Path)
    args = parser.parse_args()

    source = args.input.read_bytes()
    target = use_archipelago_colors(compact_chat_props(source))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(make_bps_patch(source, target))
    if args.patched_qb is not None:
        args.patched_qb.parent.mkdir(parents=True, exist_ok=True)
        args.patched_qb.write_bytes(target)

    print(
        f"Created {args.output}; five 0.40-scale compact chat rows "
        "and Archipelago colors"
    )


if __name__ == "__main__":
    main()
