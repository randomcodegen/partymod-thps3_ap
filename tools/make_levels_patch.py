from __future__ import annotations

import argparse
from bisect import bisect_left
import struct
import zlib
from pathlib import Path


def qb_checksum(name: str) -> int:
    normalized = name.lower().replace("/", "\\")
    return zlib.crc32(normalized.encode("ascii")) ^ 0xFFFFFFFF


def encode_bps_number(value: int) -> bytes:
    encoded = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value == 0:
            encoded.append(byte | 0x80)
            return bytes(encoded)
        encoded.append(byte)
        value -= 1


def add_foundry_unlock_flag(source: bytes) -> bytes:
    master_list_header = (
        b"\x16"
        + struct.pack("<I", qb_checksum("master_level_list"))
        + b"\x07\x05\x03"
    )
    matches: list[int] = []
    offset = 0
    while True:
        offset = source.find(master_list_header, offset)
        if offset < 0:
            break
        matches.append(offset)
        offset += 1
    if len(matches) != 1:
        raise ValueError(
            "expected exactly one master_level_list array, "
            f"found {len(matches)}"
        )

    first_struct_end = source.find(b"\x04", matches[0] + len(master_list_header))
    if first_struct_end < 0:
        raise ValueError("could not find the Foundry level structure terminator")

    assignment = (
        b"\x16"
        + struct.pack("<I", qb_checksum("unlock_flag"))
        + b"\x07\x16"
        + struct.pack("<I", qb_checksum("LEVEL_UNLOCKED_FOUNDRY"))
    )
    target = source[:first_struct_end] + assignment + source[first_struct_end:]

    # Vanilla omits Cruise Ship completely until SPECIAL_HAS_SEEN_SHIP is set.
    # AP needs a stable menu element that can be toggled in place, so remove
    # that special-case field; its regular unlock flag still controls access.
    special_assignment = (
        b"\x16"
        + struct.pack("<I", qb_checksum("special_flag"))
        + b"\x07\x16"
        + struct.pack("<I", qb_checksum("SPECIAL_HAS_SEEN_SHIP"))
    )
    if target.count(special_assignment) != 1:
        raise ValueError(
            "expected exactly one Cruise Ship special_flag assignment, "
            f"found {target.count(special_assignment)}"
        )
    target = target.replace(special_assignment, b"", 1)

    stop_music = b"\x16" + struct.pack("<I", qb_checksum("StopMusic"))
    no_op = b"\x16" + struct.pack("<I", qb_checksum("NullScript"))
    if target.count(stop_music) != 1:
        raise ValueError(
            f"expected one StopMusic command, found {target.count(stop_music)}"
        )
    return target.replace(stop_music, no_op, 1)


def _matching_source_ranges(source: bytes, target: bytes) -> list[tuple[int, int, int]]:
    anchor_size = 16
    index: dict[bytes, list[int]] = {}
    for source_offset in range(len(source) - anchor_size + 1):
        index.setdefault(
            source[source_offset:source_offset + anchor_size], []
        ).append(source_offset)

    ranges: list[tuple[int, int, int]] = []
    target_offset = 0
    expected_source_offset = 0
    while target_offset + anchor_size <= len(target):
        key = target[target_offset:target_offset + anchor_size]
        candidates = index.get(key, ())
        if not candidates:
            target_offset += 1
            continue

        nearest = bisect_left(candidates, expected_source_offset)
        candidate_indexes = range(
            max(0, nearest - 8), min(len(candidates), nearest + 8)
        )
        best_offset = -1
        best_length = 0
        for candidate_index in candidate_indexes:
            source_offset = candidates[candidate_index]
            length = anchor_size
            limit = min(
                len(source) - source_offset,
                len(target) - target_offset,
            )
            while length < limit and source[source_offset + length] == target[target_offset + length]:
                length += 1
            if length > best_length:
                best_offset, best_length = source_offset, length

        ranges.append((target_offset, best_offset, best_length))
        target_offset += best_length
        expected_source_offset = best_offset + best_length
    return ranges


def make_bps_patch(source: bytes, target: bytes) -> bytes:
    patch = bytearray(b"BPS1")
    patch.extend(encode_bps_number(len(source)))
    patch.extend(encode_bps_number(len(target)))
    patch.extend(encode_bps_number(0))

    target_offset = 0
    source_relative_offset = 0
    for match_target, match_source, match_length in _matching_source_ranges(source, target):
        if match_target > target_offset:
            literal = target[target_offset:match_target]
            patch.extend(encode_bps_number(((len(literal) - 1) << 2) | 1))
            patch.extend(literal)
        action = 0 if match_source == match_target else 2
        patch.extend(encode_bps_number(((match_length - 1) << 2) | action))
        if action == 2:
            delta = match_source - source_relative_offset
            patch.extend(encode_bps_number(
                ((-delta) << 1 | 1) if delta < 0 else delta << 1
            ))
            source_relative_offset = match_source + match_length
        target_offset = match_target + match_length
    if target_offset < len(target):
        literal = target[target_offset:]
        patch.extend(encode_bps_number(((len(literal) - 1) << 2) | 1))
        patch.extend(literal)
    patch.extend(struct.pack("<I", zlib.crc32(source)))
    patch.extend(struct.pack("<I", zlib.crc32(target)))
    patch.extend(struct.pack("<I", zlib.crc32(patch)))
    return bytes(patch)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Add an AP-controlled Foundry unlock flag to THPS3 levels.qb."
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--patched-qb", type=Path)
    args = parser.parse_args()

    source = args.input.read_bytes()
    target = add_foundry_unlock_flag(source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(make_bps_patch(source, target))
    if args.patched_qb is not None:
        args.patched_qb.parent.mkdir(parents=True, exist_ok=True)
        args.patched_qb.write_bytes(target)

    print(
        f"Created {args.output} ({len(source)} -> {len(target)} QB bytes); "
        f"LEVEL_UNLOCKED_FOUNDRY=0x{qb_checksum('LEVEL_UNLOCKED_FOUNDRY'):08x}"
    )


if __name__ == "__main__":
    main()
