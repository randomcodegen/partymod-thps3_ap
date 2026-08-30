from __future__ import annotations

import argparse
import struct
from pathlib import Path

from make_levels_patch import make_bps_patch, qb_checksum


def decode_number(data: bytes, offset: int) -> tuple[int, int]:
    value = 0
    shift = 1
    while True:
        byte = data[offset]
        offset += 1
        value += (byte & 0x7F) * shift
        if byte & 0x80:
            return value, offset
        shift <<= 7
        value += shift


def apply_bps(source: bytes, patch: bytes) -> bytes:
    assert patch[:4] == b"BPS1"
    offset = 4
    source_size, offset = decode_number(patch, offset)
    target_size, offset = decode_number(patch, offset)
    metadata_size, offset = decode_number(patch, offset)
    assert source_size == len(source)
    offset += metadata_size
    output = bytearray()
    source_relative = 0
    target_relative = 0
    while len(output) < target_size:
        header, offset = decode_number(patch, offset)
        action = header & 3
        length = (header >> 2) + 1
        if action == 0:
            output.extend(source[len(output):len(output) + length])
        elif action == 1:
            output.extend(patch[offset:offset + length])
            offset += length
        else:
            relative, offset = decode_number(patch, offset)
            delta = -(relative >> 1) if relative & 1 else relative >> 1
            if action == 2:
                source_relative += delta
                output.extend(source[source_relative:source_relative + length])
                source_relative += length
            else:
                target_relative += delta
                for _ in range(length):
                    output.append(output[target_relative])
                    target_relative += 1
    return bytes(output)


def set_initial_footer(qb: bytes) -> bytes:
    old = (
        b"\x16" + struct.pack("<I", qb_checksum("text"))
        + b"\x07\x1b\x02\x00\x00\x00 \x00"
        + b"\x16" + struct.pack("<I", qb_checksum("x"))
    )
    text = b"Archipelago - Connecting\x00"
    new = (
        b"\x16" + struct.pack("<I", qb_checksum("text"))
        + b"\x07\x1b" + struct.pack("<I", len(text)) + text
        + b"\x16" + struct.pack("<I", qb_checksum("x"))
    )
    if qb.count(old) != 1:
        raise ValueError(f"expected one main-menu footer, found {qb.count(old)}")
    return qb.replace(old, new, 1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--base-patch", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    source = args.input.read_bytes()
    target = set_initial_footer(apply_bps(source, args.base_patch.read_bytes()))
    args.output.write_bytes(make_bps_patch(source, target))
    print(f"Created {args.output} ({len(source)} -> {len(target)} QB bytes)")


if __name__ == "__main__":
    main()
