from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path

from make_levels_patch import make_bps_patch, qb_checksum


def token(name: str) -> bytes:
    return b"\x16" + struct.pack("<I", qb_checksum(name))


def after_newline(source: bytes, offset: int) -> int:
    if source[offset : offset + 1] == b"\x01":
        return offset + 1
    if source[offset : offset + 1] == b"\x02":
        return offset + 5
    raise ValueError(f"expected QB newline at {offset}")


def after_script_declaration(source: bytes, offset: int) -> int:
    try:
        return after_newline(source, offset)
    except ValueError:
        pass

    # Some shared scripts declare positional parameters between their name and
    # first source line. Debug QB newlines carry a little-endian source line.
    for cursor in range(offset, min(offset + 128, len(source) - 4)):
        if (
            source[cursor] == 2
            and struct.unpack_from("<I", source, cursor + 1)[0] < 100_000
        ):
            return cursor + 5
    raise ValueError(f"could not find end of script declaration at {offset}")


@dataclass(frozen=True)
class Guard:
    script: str
    goal: str
    resume: str = "NullScript"
    jump: str = "Goto"


PATCH_GROUPS: dict[str, tuple[Guard, ...]] = {
    "foundry": (
        Guard("CJR_Foun_Fire_Bucket_Scripts", "GOAL_SCRIPTED1"),
        *(
            Guard(f"CJR_Foun_EndGap_ValveRail0{i}", "GOAL_SCRIPTED2")
            for i in range(1, 6)
        ),
        Guard("CJR_Foun_Fire_Soaker_Scripts", "GOAL_SCRIPTED3"),
    ),
    "canada": (
        Guard(
            "AJC_Can_Script_Goal_Finished",
            "GOAL_SCRIPTED1",
            "AJC_Can_Script_Snowball_Bully",
            "GotoPreserveParams",
        ),
        Guard(
            "AJC_Can_Script_Grommet_Increment_Count",
            "GOAL_SCRIPTED2",
            "AJC_Can_Script_Grommet_Count",
        ),
        Guard(
            "AJC_Can_Script_Chuck_Knockdown", "GOAL_SCRIPTED3", "AJC_Can_Script_Chuck"
        ),
    ),
    "suburbia": (
        Guard("ALF_SUB_Axe_Grab", "GOAL_SCRIPTED1", "ALF_SUB_Axe"),
        *(Guard(f"ALF_SUB_Branch{i}", "GOAL_SCRIPTED2") for i in range(1, 5)),
        Guard("ALF_SUB_Pumpkin_Squash", "GOAL_SCRIPTED3", "ALF_SUB_Pumpkin"),
    ),
    "airport": (
        Guard("CPF_AP_GotTicket", "GOAL_SCRIPTED1"),
        # CPF_Ped_Terrorist_Hit is intentionally absent.
        # Terrorists are never spawned.
        Guard("CPF_AP_BustingFlag", "GOAL_SCRIPTED3"),
        Guard("CPF_AP_FLAG_COUNTER", "GOAL_SCRIPTED3"),
    ),
    "los_angeles_common": (
        Guard("CPF_Quake_North", "GOAL_SCRIPTED1"),
        Guard("CPF_Quake_South", "GOAL_SCRIPTED1"),
        Guard("CPF_Quake_East", "GOAL_SCRIPTED1"),
        Guard("CPF_Quake_West", "GOAL_SCRIPTED1"),
        Guard("CPF_LA_QuakeStart", "GOAL_SCRIPTED1"),
        Guard("CPF_LA_FallingCar", "GOAL_SCRIPTED2"),
        Guard("CPF_Car_Cutscene", "GOAL_SCRIPTED2"),
    ),
    "los_angeles_level": (
        Guard("LAGO_PershBall_01Script", "GOAL_SCRIPTED3"),
        Guard("LAGO_PershBall_02Script", "GOAL_SCRIPTED3"),
    ),
    "cruise_ship": (
        Guard("BDJ_SHP_PowerBoxA", "GOAL_SCRIPTED1"),
        Guard("BDJ_SHP_TriggerPropLogicA", "GOAL_SCRIPTED2"),
        Guard("BDJ_SHP_TriggerPropLogicB", "GOAL_SCRIPTED2"),
        Guard("BDJ_SHP_ChickCounterCheck", "GOAL_SCRIPTED3", "BDJ_SHP_ChickCounter"),
    ),
}


def guard_bytes(guard: Guard) -> bytes:
    return (
        b"\x25"
        + token("APObjectiveUnlocked")
        + token("goal")
        + b"\x07"
        + token(guard.goal)
        + b"\x01"
        + b"\x26\x01"
        + token(guard.jump)
        + token(guard.resume)
        + b"\x01"
        + b"\x28\x01"
    )


def add_objective_guards(source: bytes, group: str) -> bytes:
    target = source
    for guard in PATCH_GROUPS[group]:
        header = b"\x23" + token(guard.script)
        count = target.count(header)
        if count != 1:
            raise ValueError(f"expected one {guard.script}, found {count}")
        body = after_script_declaration(target, target.index(header) + len(header))
        preamble = guard_bytes(guard)
        if target[body : body + len(preamble)] != preamble:
            target = target[:body] + preamble + target[body:]
    return target


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Guard destructive scripted-objective entries with AP unlock state."
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--group", required=True, choices=PATCH_GROUPS)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--patched-qb", type=Path)
    args = parser.parse_args()

    source = args.input.read_bytes()
    target = add_objective_guards(source, args.group)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(make_bps_patch(source, target))
    if args.patched_qb is not None:
        args.patched_qb.parent.mkdir(parents=True, exist_ok=True)
        args.patched_qb.write_bytes(target)


if __name__ == "__main__":
    main()
