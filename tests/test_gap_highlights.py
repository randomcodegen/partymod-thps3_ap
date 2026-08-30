import sys

import pytest

from tools.make_gap_highlights import (
    fallback_markers,
    include_reviewed_highlight_overrides,
    include_root_cluster_render_targets,
    infer_linked_rail_span,
    infer_rail_render_target,
    is_local_rail_link,
    load_route_sources,
    load_script_sources,
    main,
    parse_decompiled,
    retain_other_levels,
    snap_paired_transfer_planes,
)


def test_cross_cluster_rail_links_must_be_local():
    assert is_local_rail_link((0.0, 0.0, 0.0), (1000.0, 0.0, 0.0))
    assert not is_local_rail_link((0.0, 0.0, 0.0), (1000.1, 0.0, 0.0))
    assert is_local_rail_link((0.0, 0.0, 0.0), (1500.0, 0.0, 0.0), 2000.0)

def test_positionless_nodes_still_occupy_qb_link_indices(tmp_path):
    source = tmp_path / "level.ns"
    source.write_text(
        "NodeArray = [\n"
        "{Name=Placeholder Class=GameObject}\n"
        "{Position=(0,0,0) Name=Start Class=RailNode Links=[2]}\n"
        "{Position=(10,0,0) Name=End Class=RailNode}\n"
        "]\n",
        encoding="utf-8",
    )

    routes, _ = parse_decompiled(source)

    assert len(routes["nodes"]) == 3
    assert routes["nodes"][0]["position"] is None
    assert routes["nodes"][1]["links"] == [2]
    assert routes["nodes"][2]["node"] == "End"

def test_reciprocal_transfer_planes_snap_to_and_tint_numbered_ramps():
    planes = [
        ({"index": 0, "node": "Gap_Start", "class": "EnvironmentObject",
          "position": "120,200,0", "duplicate_peers": [1]}, 1),
        ({"index": 1, "node": "Gap_End", "class": "EnvironmentObject",
          "position": "120,200,300", "duplicate_peers": [0]}, 1),
    ]
    left = {"node": "Rail01", "class": "RailNode", "cluster": "Ramp03",
            "position": "0,-250,0"}
    right = {"node": "Rail02", "class": "RailNode", "cluster": "Ramp04",
             "position": "0,100,300"}
    left_body = {"node": "Ramp03", "class": "EnvironmentObject",
                 "cluster": "Ramp03", "position": "0,0,0"}
    right_body = {"node": "Ramp04", "class": "EnvironmentObject",
                  "cluster": "Ramp04", "position": "0,0,300"}
    nodes = [left, right, left_body, right_body]

    snapped = snap_paired_transfer_planes(planes, nodes)
    highlighted = include_root_cluster_render_targets(snapped, nodes)

    assert [(node["node"], role) for node, role in highlighted] == [
        ("Rail01", 1), ("Rail02", 1), ("Ramp03", 2), ("Ramp04", 2)
    ]

def test_reviewed_highlight_overrides_require_exact_qb_objects():
    bowl = {"node": "Bowl", "class": "EnvironmentObject", "position": "0,0,0"}
    rail = {"node": "Rail", "class": "RailNode", "position": "1,0,0"}
    rail_start = {"node": "RailStart", "class": "RailNode", "cluster": "Tree",
                  "position": "2,0,0", "links": [3]}
    rail_end = {"node": "RailEnd", "class": "RailNode", "cluster": None,
                "position": "3,0,0", "links": []}

    assert include_reviewed_highlight_overrides(
        [], [bowl, rail], {"tint": ["Bowl"], "markers": ["Rail"]}
    ) == [(bowl, 2), (rail, 3)]
    assert include_reviewed_highlight_overrides(
        [(rail, 1), (bowl, 2)], [bowl, rail],
        {"replace_tint": True, "tint": ["Rail"]},
    ) == [(rail, 1), (rail, 2)]
    assert include_reviewed_highlight_overrides(
        [(bowl, 2)], [bowl, rail],
        {"replace_auto": True, "markers": ["Rail"]},
    ) == [(rail, 3)]
    assert include_reviewed_highlight_overrides(
        [], [bowl], {"tint": ["Bowl"], "no_markers": True}
    ) == [(bowl, 4)]
    reviewed = include_reviewed_highlight_overrides(
        [], [bowl, rail, rail_start, rail_end],
        {"rail_paths": [["RailStart", "RailEnd"]]},
    )
    assert reviewed[0][0]["rail_path"] == (2, 3)
    with pytest.raises(ValueError):
        include_reviewed_highlight_overrides([], [bowl], {"markers": ["Missing"]})

def test_fallback_markers_scope_duplicate_gap_names_by_level():
    report = {"gap_coverage": [
        {"level": "foundry", "checksum": 7,
         "endpoint_roots": [{"position": "1,2,3"}]},
        {"level": "airport", "checksum": 7,
         "endpoint_roots": [{"position": "4,5,6"}]},
    ]}

    assert fallback_markers(report, {(7, 1)}) == [(7, 4.0, 5.0, 6.0, 5)]

def test_level_sources_combine_levels_and_reject_bad_specs(tmp_path):
    source = tmp_path / "level.ns"
    source.write_text("NodeArray = []\n", encoding="utf-8")

    routes = load_route_sources([f"foundry={source}", f"canada={source}"])

    assert set(routes) == {"foundry", "canada"}
    with pytest.raises(ValueError):
        load_route_sources(["canada"])

def test_shared_gap_marks_each_alternative_rail_and_its_cluster(tmp_path, monkeypatch):
    level = tmp_path / "level.ns"
    level.write_text(
        """NodeArray = [
{Position=(-2, 0, -2) Name=LeftTail Class=RailNode Cluster=Lips Links=[1]}
{Position=(2, 0, -2) Name=Left Class=RailNode Cluster=Lips TriggerScript=LeftEnd Links=[4]}
{Position=(2, 0, 2) Name=Middle Class=RailNode Cluster=Lips}
{Position=(-2, 0, 2) Name=Right Class=RailNode Cluster=Lips TriggerScript=RightEnd Links=[2]}
{Position=(4000, 0, 0) Name=Unrelated Class=RailNode Cluster=Elsewhere}
{Position=(0, -1, 0) Name=LipMeshes Class=EnvironmentObject Cluster=Lips}
]
script LeftEnd { SharedEnd }
script RightEnd { SharedEnd }
""",
        encoding="utf-8",
    )
    shared = tmp_path / "shared.ns"
    shared.write_text(
        'script SharedEnd { EndGap GapID=LipGap text="Alternative Lip" }\n',
        encoding="utf-8",
    )
    report = tmp_path / "report.json"
    report.write_text(
        '{"gap_coverage":[{"level":"foundry","name":"Alternative Lip",'
        '"checksum":7}]}',
        encoding="utf-8",
    )
    output = tmp_path / "gap_highlights.inc"
    monkeypatch.setattr(sys, "argv", [
        "make_gap_highlights.py", str(report), str(output),
        "--level-source", f"foundry={level}",
        "--script-source", f"foundry={shared}",
    ])

    main()

    generated = output.read_text(encoding="utf-8")
    assert generated.count("{0x00000007u,") == 4
    assert "constexpr std::array<GapHighlightEndpoint, 2>" in generated
    assert "constexpr std::array<GapRailSegment, 2>" in generated

def test_route_parser_follows_endgap_gapscript_chain(tmp_path):
    level = tmp_path / "level.ns"
    level.write_text(
        """NodeArray = [
{Position=(-1, 2, 3) Name=Start Class=EnvironmentObject TriggerScript=StartScript}
{Position=(1, 2, 3) Name=End Class=EnvironmentObject TriggerScript=EndScript}
]
script StartScript { SharedStart }
script EndScript { SharedEnd }
""",
        encoding="utf-8",
    )
    shared = tmp_path / "shared.ns"
    shared.write_text(
        """script SharedStart { StartGap GapID=OuterGap }
script SharedEnd { EndGap GapID=OuterGap GapScript=ScoreGap }
script ScoreGap { StartGap GapID=InnerGap
EndGap GapID=InnerGap text=\"Chained Gap\" }
""",
        encoding="utf-8",
    )

    routes, _ = parse_decompiled(level, (shared,))

    assert [node["node"] for node in routes["starts"]["Chained Gap"]] == ["Start"]
    assert [node["node"] for node in routes["ends"]["Chained Gap"]] == ["End"]

def test_reciprocal_rail_gap_gates_snap_to_exact_link_path(tmp_path, monkeypatch):
    source = tmp_path / "level.ns"
    source.write_text(
        """NodeArray = [
{Position=(0, 0, -200) Name=PressOuterA Class=RailNode Cluster=Press_Booth Links=[1]}
{Position=(0, 0, 10) Name=PressRailA Class=RailNode Cluster=Press_Booth Links=[2]}
{Position=(0, 5, 50) Name=PressRailKink Class=RailNode Cluster=Press_Booth Links=[3]}
{Position=(0, 0, 90) Name=PressRailB Class=RailNode Cluster=Press_Booth Links=[4]}
{Position=(0, 0, 300) Name=PressOuterB Class=RailNode Cluster=Press_Booth}
{Position=(0, 10, 100) Name=StartA Class=EnvironmentObject TriggerScript=StartA Links=[7]}
{Position=(0, 10, 0) Name=StartB Class=EnvironmentObject TriggerScript=StartB Links=[8]}
{Position=(0, 10, 0) Name=Gap_Press_EndA Class=EnvironmentObject TriggerScript=EndA}
{Position=(0, 10, 100) Name=Gap_Press_EndB Class=EnvironmentObject TriggerScript=EndB}
{Position=(0, -10, 50) Name=Press_Rail_02 Class=EnvironmentObject Cluster=Press_Booth}
]
script StartA { StartRailGap }
script StartB { StartRailGap }
script EndA { EndGap Text="Press Kink" }
script EndB { EndGap Text="Press Kink" }
""",
        encoding="utf-8",
    )
    routes, _ = parse_decompiled(source)
    roots = [(node, 1) for node in routes["ends"]["Press Kink"]]

    inferred = infer_linked_rail_span(roots, routes["nodes"])

    assert [node["node"] for node, _ in inferred] == ["PressRailA", "PressRailB"]
    assert all(node["rail_path"] == (1, 2, 3) for node, _ in inferred)
    assert infer_rail_render_target(routes["nodes"], (1, 2, 3))["node"] == "Press_Rail_02"

    report = tmp_path / "report.json"
    report.write_text(
        '{"gap_coverage":[{"level":"foundry","name":"Press Kink",'
        '"checksum":7}]}',
        encoding="utf-8",
    )
    output = tmp_path / "gap_highlights.inc"
    monkeypatch.setattr(sys, "argv", [
        "make_gap_highlights.py", str(report), str(output),
        "--level-source", f"foundry={source}",
    ])
    main()

    generated = output.read_text(encoding="utf-8")
    assert "constexpr std::array<GapHighlightEndpoint, 3>" in generated
    assert "constexpr std::array<GapRailSegment, 2>" in generated
    assert ", 2u, 0.000000f, -10.000000f, 50.000000f, 1u}" in generated
    assert "-200.000000f" not in generated
    assert "300.000000f" not in generated

def test_script_sources_are_scoped_per_level(tmp_path):
    sources = load_script_sources([
        f"tokyo={tmp_path / 'js.ns'}",
        f"cruise_ship={tmp_path / 'bdj.ns'}",
    ])

    assert sources["tokyo"] == (tmp_path / "js.ns",)
    assert sources["cruise_ship"] == (tmp_path / "bdj.ns",)

def test_invalid_cross_map_link_keeps_local_incoming_rail(tmp_path, monkeypatch):
    source = tmp_path / "level.ns"
    source.write_text(
        """NodeArray = [
{Position=(0, 0, 0) Name=LocalRail Class=RailNode Cluster=Launch Links=[1]}
{Position=(10, 0, 0) Name=GapRail Class=RailNode Cluster=Launch TriggerScript=Start Links=[2]}
{Position=(4000, 0, 0) Name=Unrelated Class=RailNode Cluster=Elsewhere}
]
script Start { StartGap GapID=Launch }
""",
        encoding="utf-8",
    )
    report = tmp_path / "report.json"
    report.write_text(
        '{"gap_coverage":[{"level":"rio","name":"Launch gap",'
        '"checksum":7,"gap_ids":["Launch"]}]}',
        encoding="utf-8",
    )
    output = tmp_path / "gap_highlights.inc"
    monkeypatch.setattr(sys, "argv", [
        "make_gap_highlights.py", str(report), str(output),
        "--level-source", f"rio={source}",
    ])

    main()

    generated = output.read_text(encoding="utf-8")
    assert "constexpr std::array<GapRailSegment, 1>" in generated
    assert "0.000000f, 0.000000f, 0.000000f, 10.000000f" in generated
    assert "4000.000000f" not in generated

def test_bouncy_start_cluster_focuses_named_geometry(tmp_path, monkeypatch):
    source = tmp_path / "level.ns"
    source.write_text(
        """NodeArray = [
{Position=(0, 0, 0) Name=TRG_Totem_Rail Class=RailNode Cluster=ParkTotem TriggerScript=Start}
{Position=(1, 0, 0) Name=Wild_Totem_Wings Class=EnvironmentObject Cluster=ParkTotem}
{Position=(2, 0, 0) Name=Wild_Totem_Head Class=BouncyObject Cluster=ParkTotem}
{Position=(3, 0, 0) Name=#deadbeef Class=EnvironmentObject Cluster=ParkTotem}
{Position=(100, 0, 0) Name=UnrelatedFloor Class=EnvironmentObject TriggerScript=End}
]
script Start { StartGap GapID=Totem }
script End { EndGap GapID=Totem Text="Head gap" }
""",
        encoding="utf-8",
    )
    report = tmp_path / "report.json"
    report.write_text(
        '{"gap_coverage":[{"level":"canada","name":"Head gap",'
        '"checksum":7}]}',
        encoding="utf-8",
    )
    output = tmp_path / "gap_highlights.inc"
    monkeypatch.setattr(sys, "argv", [
        "make_gap_highlights.py", str(report), str(output),
        "--level-source", f"canada={source}",
    ])

    main()

    generated = output.read_text(encoding="utf-8")
    assert "constexpr std::array<GapHighlightEndpoint, 3>" in generated
    assert generated.count(", 2u,") == 2
    assert "100.000000f" not in generated
    assert "3.000000f" not in generated

def test_merge_retains_only_unreplaced_gaps(tmp_path):
    output = tmp_path / "gap_highlights.inc"
    output.write_text(
        "constexpr std::array<GapHighlightEndpoint, 2> kGapHighlightEndpoints{{\n"
        "    {0x11111111u, endpoint},\n"
        "    {0x22222222u, endpoint},\n"
        "}};\n"
        "constexpr std::array<GapRailSegment, 2> kGapRailSegments{{\n"
        "    {0x11111111u, rail},\n"
        "    {0x22222222u, rail},\n"
        "}};\n",
        encoding="utf-8",
    )

    assert retain_other_levels(output, {(0x11111111, 1)}) == (
        ["    {0x22222222u, endpoint},"],
        ["    {0x22222222u, rail},"],
    )
