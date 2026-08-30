from __future__ import annotations

import argparse
import struct
from pathlib import Path

from make_levels_patch import make_bps_patch, qb_checksum


def command(name: str) -> bytes:
    return b"\x16" + struct.pack("<I", qb_checksum(name))


def pause_music(value: int) -> bytes:
    return command("PauseMusic") + b"\x17" + value.to_bytes(4, "little")


def keep_current_music_track(source: bytes) -> bytes:
    skip_music = command("SkipMusicTrack")
    no_op = command("NullScript")
    if source.count(skip_music) != 1:
        raise ValueError(
            f"expected one SkipMusicTrack command, found {source.count(skip_music)}"
        )
    return source.replace(skip_music, no_op, 1)


def keep_transition_music_playing(source: bytes, expected_pauses: int) -> bytes:
    pause = pause_music(1)
    if source.count(pause) != expected_pauses:
        raise ValueError(
            f"expected {expected_pauses} PauseMusic 1 commands, "
            f"found {source.count(pause)}"
        )
    return source.replace(pause, pause_music(0))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Keep the current song when THPS3 starts another run."
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--patched-qb", type=Path)
    parser.add_argument(
        "--kind", choices=("gameflow", "goal_scripts", "judges"),
        default="gameflow")
    args = parser.parse_args()

    source = args.input.read_bytes()
    if args.kind == "gameflow":
        target = keep_transition_music_playing(
            keep_current_music_track(source), 3)
    else:
        target = keep_transition_music_playing(
            source, 2 if args.kind == "goal_scripts" else 1)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(make_bps_patch(source, target))
    if args.patched_qb:
        args.patched_qb.parent.mkdir(parents=True, exist_ok=True)
        args.patched_qb.write_bytes(target)


if __name__ == "__main__":
    main()
