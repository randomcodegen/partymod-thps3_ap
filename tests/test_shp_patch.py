from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))

from make_shp_patch import add_comm_lip_starts, token


def test_adds_comm_lip_start_to_all_helipad_ramp_variants() -> None:
    over_comm = (
        token("StartGap") + token("gapID") + b"\x07" + token("OverComm")
        + token("flags") + b"\x07" + token("PURE_AIR") + b"\x01"
    )
    source = b"".join(
        b"\x23" + token(name) + b"\x01" + over_comm + b"\x24"
        for name in (
            "shpsf_HelipadQuarterPipe01Script",
            "shpsf_HelipadQuarterPipe02Script",
            "shpsf_HelipadQuarterPipe01aScript",
            "shpsf_HelipadQuarterPipe02aScript",
        )
    )
    target = add_comm_lip_starts(source)
    assert target.count(token("CommLip")) == 4
    assert target.count(token("OverComm")) == 4
