from __future__ import annotations

import argparse
import json
import re
import zlib
from collections import deque
from pathlib import Path


LEVEL_IDS = {
    "foundry": 1,
    "canada": 2,
    "rio": 3,
    "suburbia": 4,
    "airport": 5,
    "skater_island": 6,
    "los_angeles": 7,
    "tokyo": 8,
    "cruise_ship": 9,
}


def qb_checksum(value: str | None) -> int:
    if not value:
        return 0
    return zlib.crc32(value.replace("/", "\\").lower().encode("ascii")) ^ 0xFFFFFFFF


def is_local_rail_link(
    start: tuple[float, ...], end: tuple[float, ...], limit: float = 1000.0
) -> bool:
    return sum((left - right) ** 2 for left, right in zip(start, end)) <= limit ** 2


def semantic_tokens(value: object) -> set[str]:
    words = re.findall(
        r"[A-Z]+(?=[A-Z][a-z]|\d|$)|[A-Z]?[a-z]+|\d+", str(value)
    )
    aliases = {
        "airplane": "plane", "bwl": "bowl", "doorbell": "door", "handrail": "rail",
        "hh": "haunted", "lp": "loop",
        "rmp": "ramp", "vrt": "vert",
    }
    ignored = {
        "gap", "trg", "trgp", "north", "south", "east", "west", "mid",
        "conc", "park", "floor", "building",
    }
    return {
        aliases.get(word.casefold(), word.casefold())
        for word in words
        if (len(aliases.get(word.casefold(), word.casefold())) >= 4
            or word.casefold() == "box")
        and aliases.get(word.casefold(), word.casefold()) not in ignored
    }


def identity_tokens(value: object) -> set[str]:
    aliases = {
        "benches": "bench", "braces": "brace", "beams": "beam",
        "dumpsters": "dumpster",
    }
    return {
        aliases.get(word.casefold(), word.casefold())
        for word in re.findall(r"[A-Za-z]+|\d+", str(value))
    }


def parse_decompiled(
    path: Path, script_sources: tuple[Path, ...] = ()
) -> tuple[dict[str, list[dict[str, object]]], dict[str, str]]:
    source = path.read_text(encoding="utf-8")
    node_source = source.split("script ", 1)[0]
    nodes: list[dict[str, object]] = []
    by_script: dict[str, list[dict[str, object]]] = {}
    for match in re.finditer(r"\{([^{}]*)\}", node_source):
        body = match.group(1)

        def field(name: str) -> str | None:
            found = re.search(rf"\b{name}=([^\s}}]+)", body)
            return found.group(1) if found else None

        position = re.search(r"\bPosition=\(([^)]+)\)", body)
        links = re.search(r"\bLinks=\[([^]]*)\]", body)
        node: dict[str, object] = {
            "index": len(nodes),
            "node": field("Name"),
            "class": field("Class"),
            "cluster": field("Cluster"),
            "trigger": field("TriggerScript"),
            "position": position.group(1) if position else None,
            "links": [int(value) for value in links.group(1).split()] if links else [],
        }
        nodes.append(node)
        trigger = field("TriggerScript")
        if trigger:
            by_script.setdefault(trigger, []).append(node)

    scripts: dict[str, str] = {}
    for script_source in (source, *(item.read_text(encoding="utf-8") for item in script_sources)):
        for match in re.finditer(r"\bscript\s+(\S+)\s*\{", script_source):
            depth = 1
            cursor = match.end()
            while cursor < len(script_source) and depth:
                depth += (script_source[cursor] == "{") - (script_source[cursor] == "}")
                cursor += 1
            scripts[match.group(1)] = script_source[match.end():cursor - 1]

    for node in nodes:
        trigger = node.get("trigger")
        node["starts_rail_gap"] = bool(
            trigger and re.search(
                r"\bStartRailGap\b", scripts.get(str(trigger), ""), re.IGNORECASE
            )
        )

    # Environment objects created as two-sided ramps commonly put the real
    # trigger on one object and DuplicateTrigger on its reciprocal linked peer.
    for index, node in enumerate(nodes):
        trigger = node.get("trigger")
        if not trigger or not re.fullmatch(
            r"\s*DuplicateTrigger\s*", scripts.get(str(trigger), ""), re.IGNORECASE
        ):
            continue
        for linked_index in node["links"]:
            if linked_index >= len(nodes):
                continue
            linked = nodes[linked_index]
            linked_trigger = linked.get("trigger")
            if index not in linked["links"] or not linked_trigger:
                continue
            node.setdefault("duplicate_peers", []).append(linked_index)
            linked.setdefault("duplicate_peers", []).append(index)
            roots = by_script.setdefault(str(linked_trigger), [])
            if node not in roots:
                roots.append(node)

    gap_ids_by_name: dict[str, set[str]] = {}
    end_scripts: dict[str, set[str]] = {}
    start_scripts: dict[str, set[str]] = {}
    continued_from_ids: dict[str, set[str]] = {}
    score_targets_by_name: dict[str, set[str]] = {}
    unkeyed_start_scripts: set[str] = set()
    for root_script in by_script:
        pending = [(root_script, 0)]
        reachable: dict[str, int] = {}
        while pending:
            script, depth = pending.pop()
            if script not in scripts or reachable.get(script, depth + 1) <= depth:
                continue
            reachable[script] = depth
            for line in scripts[script].splitlines():
                call = re.match(r"\s*([A-Za-z_]\w*)\b", line) if depth == 0 else None
                if call and call.group(1) in scripts:
                    pending.append((call.group(1), depth + 1))
                if depth < 2:
                    gap_script = re.search(
                        r"\bGapScript=([^\s}]+)", line, re.IGNORECASE
                    )
                    if gap_script and gap_script.group(1) in scripts:
                        pending.append((gap_script.group(1), depth + 1))
        for script, depth in reachable.items():
            for line in scripts[script].splitlines():
                gap_id = re.search(r"\bGapID=([^\s}]+)", line, re.IGNORECASE)
                text = re.search(r'\btext="([^"]+)"', line, re.IGNORECASE)
                gap_script = re.search(
                    r"\bGapScript=([^\s}]+)", line, re.IGNORECASE
                )
                if re.search(r"\bEndGap\b", line) and text:
                    if gap_id and not gap_id.group(1).startswith("<"):
                        gap_ids_by_name.setdefault(text.group(1), set()).add(
                            gap_id.group(1).casefold()
                        )
                    end_scripts.setdefault(text.group(1), set()).add(root_script)
                    if gap_script:
                        score_targets_by_name.setdefault(text.group(1), set()).add(
                            gap_script.group(1)
                        )
                if gap_id and gap_script and not gap_id.group(1).startswith("<"):
                    for nested_line in scripts.get(gap_script.group(1), "").splitlines():
                        nested_text = re.search(
                            r'\bEndGap\b[^\n}]*\btext="([^"]+)"',
                            nested_line,
                            re.IGNORECASE,
                        )
                        if nested_text:
                            gap_ids_by_name.setdefault(nested_text.group(1), set()).add(
                                gap_id.group(1).casefold()
                            )
                if (
                    depth <= 1
                    and gap_id
                    and not gap_id.group(1).startswith("<")
                    and re.search(r"\b(?:StartGap|StartAirGap|StartRailGap)\b", line)
                    and not (
                        "CANCEL_GROUND" in line.upper()
                        and all(
                            node.get("class") == "EnvironmentObject"
                            for node in by_script.get(root_script, ())
                        )
                    )
                ):
                    start_scripts.setdefault(gap_id.group(1).casefold(), set()).add(root_script)
                if depth == 0 and re.search(
                    r"\b(?:StartGap|StartAirGap)\b", line
                ):
                    unkeyed_start_scripts.add(root_script)
                continued = re.search(
                    r"\bcontinue\s*=\s*\{[^}]*\bGapID=([^\s}]+)",
                    line,
                    re.IGNORECASE,
                )
                if depth <= 1 and continued and not continued.group(1).startswith("<"):
                    start_scripts.setdefault(
                        continued.group(1).casefold(), set()
                    ).add(root_script)
                    if gap_id and not gap_id.group(1).startswith("<"):
                        continued_from_ids.setdefault(
                            continued.group(1).casefold(), set()
                        ).add(gap_id.group(1).casefold())

    starts_by_name = {}
    for name in end_scripts:
        gap_ids = gap_ids_by_name.get(name, ())
        starts = [
            node
            for gap_id in sorted(gap_ids)
            for script in sorted(start_scripts.get(gap_id, ()))
            for node in by_script.get(script, ())
        ]
        end_nodes = [
            node
            for script in sorted(end_scripts.get(name, ()))
            for node in by_script.get(script, ())
        ]
        for script in sorted(unkeyed_start_scripts):
            for node in by_script.get(script, ()):
                identity = semantic_tokens(node.get("node")) - {"start", "end"}
                linked_ends = [
                    end for end in end_nodes
                    if end["index"] in node["links"] or node["index"] in end["links"]
                ]
                if node not in end_nodes and identity and any(
                    identity & (semantic_tokens(end.get("node")) - {"start", "end"})
                    for end in linked_ends
                ) and node not in starts:
                    starts.append(node)
        starts_by_name[name] = starts
    starts_by_gap_id = {
        gap_id: [
            node
            for script in sorted(script_names)
            for node in by_script.get(script, ())
        ]
        for gap_id, script_names in start_scripts.items()
    }
    predecessor_starts_by_gap_id = {
        gap_id: [
            node
            for predecessor in predecessor_ids
            for script in sorted(start_scripts.get(predecessor, ()))
            for node in by_script.get(script, ())
        ]
        for gap_id, predecessor_ids in continued_from_ids.items()
    }
    ends_by_name = {
        name: [
            node
            for script in sorted(script_names)
            for node in by_script.get(script, ())
        ]
        for name, script_names in end_scripts.items()
    }
    callers: dict[str, list[dict[str, object]]] = {}
    call_targets: dict[str, set[str]] = {}
    for script, script_nodes in by_script.items():
        for match in re.finditer(
            r"\b(CJR_[A-Za-z0-9_]+)\b([^\n}]*)", scripts.get(script, "")
        ):
            called = match.group(1)
            callers.setdefault(called, []).extend(script_nodes)
            call_targets.setdefault(called, set()).update(
                re.findall(r"\bID\d+=([A-Za-z0-9_]+)", match.group(2))
            )
    return {
        "nodes": nodes,
        "starts": starts_by_name,
        "starts_by_gap_id": starts_by_gap_id,
        "predecessor_starts_by_gap_id": predecessor_starts_by_gap_id,
        "ends": ends_by_name,
        "callers": callers,
        "call_targets": call_targets,
        "score_targets": score_targets_by_name,
    }, scripts


def snap_duplicate_coping(
    roots: list[tuple[dict[str, object], int]],
    nodes: list[dict[str, object]],
) -> list[tuple[dict[str, object], int]]:
    """Replace linked two-sided ramp origins with their closest coping points."""
    rail_nodes: dict[str, list[dict[str, object]]] = {}
    for node in nodes:
        if node.get("class") == "RailNode" and node.get("cluster") and node.get("position"):
            rail_nodes.setdefault(str(node["cluster"]), []).append(node)

    snapped: list[tuple[dict[str, object], int]] = []
    replaced: set[int] = set()
    for left_index, (left, role) in enumerate(roots):
        for right_index in range(left_index + 1, len(roots)):
            right, right_role = roots[right_index]
            if (
                role != right_role
                or (
                    right.get("index") not in left.get("duplicate_peers", ())
                    and left.get("index") not in right.get("duplicate_peers", ())
                )
            ):
                continue
            left_cluster = left.get("cluster")
            right_cluster = right.get("cluster")
            if not left_cluster or not right_cluster or left_cluster == right_cluster:
                continue
            candidates = (
                (sum((a - b) ** 2 for a, b in zip(
                    map(float, str(left_rail["position"]).split(",")),
                    map(float, str(right_rail["position"]).split(",")),
                )), left_rail, right_rail)
                for left_rail in rail_nodes.get(str(left_cluster), ())
                for right_rail in rail_nodes.get(str(right_cluster), ())
            )
            closest = min(candidates, default=None, key=lambda item: item[0])
            if closest is None:
                continue
            replaced.update((left_index, right_index))
            for root, rail in ((left, closest[1]), (right, closest[2])):
                snapped.append(({
                    "node": root.get("node"),
                    "class": root.get("class"),
                    "cluster": root["cluster"],
                    "position": rail["position"],
                }, role))

    return [root for index, root in enumerate(roots) if index not in replaced] + snapped


def snap_paired_transfer_planes(
    roots: list[tuple[dict[str, object], int]],
    nodes: list[dict[str, object]],
    horizontal_limit: float = 160.0,
    vertical_limit: float = 500.0,
) -> list[tuple[dict[str, object], int]]:
    """Snap a matched pair of bare transfer planes to distinct nearby copings."""
    if len(roots) != 2 or any(
        role != roots[0][1]
        or endpoint.get("class") != "EnvironmentObject"
        or endpoint.get("cluster")
        or not endpoint.get("position")
        for endpoint, role in roots
    ):
        return roots
    stems = {
        re.sub(r"\d+$", "", str(endpoint.get("node", ""))).casefold()
        for endpoint, _ in roots
    }
    reciprocal = all(
        endpoint.get("index") in other.get("duplicate_peers", ())
        for (endpoint, _), (other, _) in (roots, roots[::-1])
    )
    if (len(stems) != 1 or not next(iter(stems))) and not reciprocal:
        return roots

    incoming_clusters: dict[int, set[str]] = {}
    for node in nodes:
        if node.get("class") != "RailNode" or not node.get("cluster"):
            continue
        for linked in node.get("links", ()):
            incoming_clusters.setdefault(linked, set()).add(str(node["cluster"]))
    rails = [
        (node, cluster)
        for index, node in enumerate(nodes)
        if node.get("class") == "RailNode" and node.get("position")
        for cluster in ({str(node["cluster"])} if node.get("cluster") else set())
        | incoming_clusters.get(index, set())
    ]
    candidates_by_endpoint = []
    for endpoint, role in roots:
        x, y, z = map(float, str(endpoint["position"]).split(","))
        candidates = []
        for rail, cluster in rails:
            rail_x, rail_y, rail_z = map(float, str(rail["position"]).split(","))
            horizontal = (x - rail_x) ** 2 + (z - rail_z) ** 2
            if horizontal <= horizontal_limit ** 2 and abs(y - rail_y) <= vertical_limit:
                candidates.append((horizontal, {**rail, "cluster": cluster}))
        if not candidates:
            return roots
        candidates_by_endpoint.append(candidates)

    def family(cluster: object) -> str:
        return re.sub(r"(?:[_-]?[A-Za-z]|\d+)$", "", str(cluster)).casefold()

    pairs = [
        (left_distance + right_distance, left, right)
        for left_distance, left in candidates_by_endpoint[0]
        for right_distance, right in candidates_by_endpoint[1]
        if left["cluster"] != right["cluster"]
        and family(left["cluster"]) == family(right["cluster"])
    ]
    if not pairs:
        return roots
    _, left, right = min(pairs, key=lambda item: item[0])
    return [
        ({**left, "paired_transfer": True}, roots[0][1]),
        ({**right, "paired_transfer": True}, roots[1][1]),
    ]


def infer_paired_cluster_endpoints(
    roots: list[tuple[dict[str, object], int]],
    nodes: list[dict[str, object]],
    max_distance: float = 750.0,
) -> list[tuple[dict[str, object], int]]:
    """Find nearby E/W or N/S rail-cluster pairs surrounding a bare trigger."""
    rails: dict[str, list[dict[str, object]]] = {}
    for node in nodes:
        if node.get("class") == "RailNode" and node.get("cluster") and node.get("position"):
            rails.setdefault(str(node["cluster"]), []).append(node)

    groups: dict[str, dict[str, str]] = {}
    for cluster in rails:
        match = re.fullmatch(r"(.+)_([NSEW])", cluster, re.IGNORECASE)
        if match:
            groups.setdefault(match.group(1).casefold(), {})[
                match.group(2).upper()
            ] = cluster

    inferred: list[tuple[dict[str, object], int]] = []
    emitted: set[tuple[str, str]] = set()
    limit_squared = max_distance * max_distance
    for endpoint, role in roots:
        if endpoint.get("cluster") or endpoint.get("class") != "EnvironmentObject":
            continue
        position = endpoint.get("position")
        if not position:
            continue
        point = tuple(map(float, str(position).split(",")))
        candidates = []
        for siblings in groups.values():
            for first_direction, second_direction, axis in (
                ("W", "E", 0), ("N", "S", 2)
            ):
                if first_direction not in siblings or second_direction not in siblings:
                    continue
                first_cluster = siblings[first_direction]
                second_cluster = siblings[second_direction]
                first_rails = rails[first_cluster]
                second_rails = rails[second_cluster]
                first_center = sum(
                    float(str(node["position"]).split(",")[axis]) for node in first_rails
                ) / len(first_rails)
                second_center = sum(
                    float(str(node["position"]).split(",")[axis]) for node in second_rails
                ) / len(second_rails)
                if (first_center - point[axis]) * (second_center - point[axis]) > 0:
                    continue

                def closest(cluster_rails: list[dict[str, object]]):
                    return min(
                        (
                            sum((a - b) ** 2 for a, b in zip(
                                point, map(float, str(node["position"]).split(","))
                            )),
                            node,
                        )
                        for node in cluster_rails
                    )

                first = closest(first_rails)
                second = closest(second_rails)
                if max(first[0], second[0]) <= limit_squared:
                    candidates.append((first[0] + second[0], (
                        (first_cluster, first[1]), (second_cluster, second[1])
                    )))
        if not candidates:
            continue
        for cluster, rail in min(candidates, key=lambda item: item[0])[1]:
            key = (cluster, str(rail["position"]))
            if key in emitted:
                continue
            emitted.add(key)
            inferred.append(({
                "node": None,
                "class": None,
                "cluster": cluster,
                "position": rail["position"],
                "rail_cluster": True,
            }, role))
    return inferred


def attach_aligned_clusters(
    roots: list[tuple[dict[str, object], int]],
    nodes: list[dict[str, object]],
    horizontal_limit: float = 48.0,
    vertical_limit: float = 750.0,
) -> list[tuple[dict[str, object], int]]:
    """Attach a bare trigger to uniquely aligned clustered environment geometry."""
    geometry = [
        node for node in nodes
        if node.get("class") == "EnvironmentObject"
        and node.get("cluster")
        and not str(node.get("node", "")).casefold().startswith("trg")
        and node.get("position")
    ]
    unclustered_geometry = [
        node for node in nodes
        if node.get("class") == "EnvironmentObject"
        and not node.get("cluster")
        and node.get("node")
        and node.get("position")
        and not str(node["node"]).casefold().startswith(
            ("collision", "particle", "trg", "trgp")
        )
        and "shadow" not in str(node["node"]).casefold()
    ]
    attached = []
    for endpoint, role in roots:
        if endpoint.get("class") != "EnvironmentObject":
            attached.append((endpoint, role))
            continue
        position = endpoint.get("position")
        if not position:
            attached.append((endpoint, role))
            continue
        x, y, z = map(float, str(position).split(","))
        current_cluster = endpoint.get("cluster")
        endpoint_tokens = semantic_tokens(endpoint.get("node"))
        if not current_cluster:
            aligned = [
                node for node in unclustered_geometry
                if (x - float(str(node["position"]).split(",")[0])) ** 2
                + (z - float(str(node["position"]).split(",")[2])) ** 2
                <= horizontal_limit * horizontal_limit
                and abs(y - float(str(node["position"]).split(",")[1])) <= 96.0
            ]
            if len(aligned) == 1:
                attached.extend(((endpoint, role), (aligned[0], 2)))
                continue
        if current_cluster and (
            endpoint_tokens.intersection(semantic_tokens(current_cluster))
            or any(
                node.get("cluster") == current_cluster
                and (x - float(str(node["position"]).split(",")[0])) ** 2
                + (z - float(str(node["position"]).split(",")[2])) ** 2
                <= horizontal_limit * horizontal_limit
                for node in geometry
            )
        ):
            attached.append((endpoint, role))
            if role != 2:
                aligned = [
                    node for node in geometry
                    if node.get("cluster") == current_cluster
                    and (x - float(str(node["position"]).split(",")[0])) ** 2
                    + (z - float(str(node["position"]).split(",")[2])) ** 2
                    <= horizontal_limit * horizontal_limit
                    and abs(y - float(str(node["position"]).split(",")[1])) <= 96.0
                ]
                if len(aligned) == 1:
                    attached.append((aligned[0], 2))
            continue
        by_cluster: dict[str, tuple[float, float]] = {}
        for node in geometry:
            if not endpoint_tokens.intersection(semantic_tokens(node["cluster"])):
                continue
            node_x, node_y, node_z = map(float, str(node["position"]).split(","))
            horizontal = (x - node_x) ** 2 + (z - node_z) ** 2
            vertical = abs(y - node_y)
            if vertical > vertical_limit:
                continue
            key = str(node["cluster"])
            if key not in by_cluster or (horizontal, vertical) < by_cluster[key]:
                by_cluster[key] = (horizontal, vertical)
        candidates = sorted((distance, cluster) for cluster, distance in by_cluster.items())
        if (
            not candidates
            or candidates[0][0][0] > horizontal_limit * horizontal_limit
            or len(candidates) > 1
            and candidates[1][0][0] < (horizontal_limit * 2) ** 2
        ):
            attached.append((endpoint, role))
            continue
        attached.append(({
            "node": None,
            "class": None,
            "cluster": candidates[0][1],
            "position": endpoint["position"],
            "source_node": endpoint.get("node"),
        }, role))
    return attached


def attach_linked_launch_geometry(
    roots: list[tuple[dict[str, object], int]],
    nodes: list[dict[str, object]],
    gap_geometry: list[dict[str, object]],
    launch_limit: float = 160.0,
    moving_limit: float = 600.0,
) -> list[tuple[dict[str, object], int]]:
    """Associate a bare start plane with unique moving gap geometry."""
    end_links = {
        linked
        for endpoint, role in roots
        if role == 1 and endpoint.get("class") == "RailNode"
        for linked in endpoint.get("links", ())
        if linked < len(nodes)
        and nodes[linked].get("class") == "RailNode"
        and not nodes[linked].get("cluster")
    }
    if len(end_links) != 1:
        return roots
    launch_rail = nodes[next(iter(end_links))]
    if not launch_rail.get("position"):
        return roots

    def distance(left: dict[str, object], right: dict[str, object]) -> float:
        return sum(
            (a - b) ** 2
            for a, b in zip(
                map(float, str(left["position"]).split(",")),
                map(float, str(right["position"]).split(",")),
            )
        )

    result = []
    for endpoint, role in roots:
        if (
            role != 0 or endpoint.get("class") != "EnvironmentObject"
            or endpoint.get("cluster") or not endpoint.get("position")
            or distance(endpoint, launch_rail) > launch_limit * launch_limit
        ):
            result.append((endpoint, role))
            continue
        candidates = {
            int(candidate["index"]): candidate
            for candidate in gap_geometry
            if candidate.get("index") is not None
            and candidate.get("class") == "EnvironmentObject"
            and candidate.get("node") and candidate.get("cluster")
            and any(
                rail.get("class") == "RailNode"
                and rail.get("cluster") == candidate.get("cluster")
                and rail.get("position")
                and distance(endpoint, rail) <= moving_limit * moving_limit
                and len(semantic_tokens(candidate["node"]).intersection(
                    semantic_tokens(rail.get("node")))) >= 2
                for rail in nodes
            )
        }
        if len(candidates) == 1:
            replacement = dict(next(iter(candidates.values())))
            replacement["position"] = endpoint["position"]
            result.append((replacement, role))
        else:
            result.append((endpoint, role))
    return result


def include_parameterized_render_targets(
    roots: list[tuple[dict[str, object], int]],
    nodes: list[dict[str, object]],
    routes: list[dict[str, object]],
    extra_targets: set[str] | None = None,
) -> list[tuple[dict[str, object], int]]:
    """Resolve symbolic scoring-call targets to one visible object in an exact cluster."""
    targets = {
        value
        for route in routes
        for call in route.get("call_chain", ())
        for value in re.findall(r"\bID\d+=([A-Za-z0-9_]+)", str(call))
    }
    targets.update(extra_targets or ())
    added = list(roots)
    for target in targets:
        tokens = identity_tokens(target)
        clusters = {
            str(node["cluster"])
            for node in nodes
            if node.get("cluster") and tokens <= identity_tokens(node["cluster"])
        }
        if not clusters:
            words = {token for token in tokens if not token.isdigit()}
            clusters = {
                str(node["cluster"])
                for node in nodes
                if node.get("cluster") and words <= identity_tokens(node["cluster"])
            }
        if len(clusters) != 1:
            continue
        cluster = next(iter(clusters))
        visible = [
            node for node in nodes
            if node.get("class") == "EnvironmentObject"
            and node.get("cluster") == cluster
            and identity_tokens(node.get("node")) & tokens
            and not str(node.get("node", "")).casefold().startswith("collision")
            and "legs" not in str(node.get("node", "")).casefold()
            and "plank" not in str(node.get("node", "")).casefold()
        ]
        added.extend((node, 2) for node in visible)
    return added


def include_root_cluster_render_targets(
    roots: list[tuple[dict[str, object], int]],
    nodes: list[dict[str, object]],
) -> list[tuple[dict[str, object], int]]:
    """Add one exact visible object for a rail root when its QB cluster identifies it."""
    added = list(roots)
    for endpoint, _ in roots:
        if endpoint.get("class") != "RailNode" or not endpoint.get("cluster"):
            continue
        endpoint_tokens = semantic_tokens(endpoint.get("node"))
        cluster_tokens = semantic_tokens(endpoint["cluster"])
        if endpoint.get("paired_transfer"):
            bodies = [
                node for node in nodes
                if node.get("class") == "EnvironmentObject"
                and node.get("cluster") == endpoint["cluster"]
                and cluster_tokens <= semantic_tokens(node.get("node"))
                and not any(word in str(node.get("node", "")).casefold()
                            for word in ("collision", "coping", "inviso", "shadow"))
            ]
            if len(bodies) == 1:
                added.append((bodies[0], 2))
                continue
            surfaces = [
                node for node in nodes
                if node.get("class") == "EnvironmentObject"
                and node.get("cluster") == endpoint["cluster"]
                and identity_tokens(node.get("node"))
                    & {"ramp", "qtr", "tranny", "bowl"}
                and not identity_tokens(node.get("node"))
                    & {"back", "beam", "brace", "coping", "deck"}
                and not str(node.get("node", "")).casefold().startswith(
                    ("collision", "inviso", "trg", "trgp")
                )
            ]
            if surfaces:
                added.extend((node, 2) for node in surfaces)
                continue
        visible = [
            node for node in nodes
            if node.get("class") == "EnvironmentObject"
            and node.get("cluster") == endpoint["cluster"]
            and semantic_tokens(node.get("node")) & endpoint_tokens
            and semantic_tokens(node.get("node")) & cluster_tokens
            and not str(node.get("node", "")).casefold().startswith("collision")
            and not str(node.get("node", "")).casefold().startswith(("trg", "trgp"))
            and "legs" not in str(node.get("node", "")).casefold()
        ]
        if len(visible) > 1:
            if "qtr" in identity_tokens(endpoint["cluster"]):
                visible = [
                    node for node in visible
                    if "qtr" in identity_tokens(node.get("node"))
                ]
                added.extend((node, 2) for node in visible)
                continue
            cluster_words = semantic_tokens(endpoint["cluster"])
            if len(cluster_words) >= 2 and all(
                len(semantic_tokens(node.get("node")) & cluster_words) >= 2
                for node in visible
            ):
                added.extend((node, 2) for node in visible)
                continue
            precise = [
                node for node in visible
                if len(semantic_tokens(node.get("node")) & endpoint_tokens) >= 2
            ]
            if precise:
                visible = precise
        bodies = [
            node for node in visible
            if "coping" not in str(node.get("node", "")).casefold()
        ]
        if len(visible) == 1 or len(bodies) == 1 and len(bodies) < len(visible):
            added.extend((node, 2) for node in visible)
    return added


def include_semantic_render_targets(
    roots: list[tuple[dict[str, object], int]],
    nodes: list[dict[str, object]],
) -> list[tuple[dict[str, object], int]]:
    """Resolve a trigger to the one visible object carrying its QB identity."""
    added = list(roots)
    for endpoint, _ in roots:
        if endpoint.get("class") != "EnvironmentObject":
            continue
        tokens = semantic_tokens(endpoint.get("node")) - {"start", "end"}
        if len(tokens) < 2:
            continue
        numbers = set(re.findall(r"\d+", str(endpoint.get("node"))))
        candidates = [
            node for node in nodes
            if node is not endpoint
            and node.get("class") == "EnvironmentObject"
            and node.get("node") and node.get("position")
            and len(tokens & semantic_tokens(node["node"])) >= 2
            and (not numbers or numbers == set(re.findall(r"\d+", str(node["node"]))))
            and not str(node["node"]).casefold().startswith(
                ("collision", "inviso", "particle", "trg", "trgp")
            )
            and "shadow" not in str(node["node"]).casefold()
        ]
        if len(candidates) == 1:
            added.append((candidates[0], 2))
    return added


def include_reviewed_highlight_overrides(
    roots: list[tuple[dict[str, object], int]],
    nodes: list[dict[str, object]],
    overrides: dict[str, object],
) -> list[tuple[dict[str, object], int]]:
    """Add exact tint and marker objects explicitly reviewed against the level QB."""
    added = [] if overrides.get("replace_auto") else [
        root for root in roots
        if not (overrides.get("replace_tint") and root[1] == 2)
    ]
    for kind, role in (("tint", 4 if overrides.get("no_markers") else 2), ("markers", 3)):
        for name in overrides.get(kind, ()):
            matches = [node for node in nodes if node.get("node") == name]
            if len(matches) != 1:
                raise ValueError(f"reviewed {kind} target {name!r} is not unique")
            added.append((matches[0], role))
    for names in overrides.get("rail_paths", ()):
        if len(names) != 2:
            raise ValueError(f"reviewed rail path {names!r} must have two endpoints")
        indices = [
            [index for index, node in enumerate(nodes) if node.get("node") == name]
            for name in names
        ]
        if any(len(matches) != 1 for matches in indices):
            raise ValueError(f"reviewed rail path endpoints {names!r} are not unique")
        first, second = indices[0][0], indices[1][0]
        clusters = {nodes[first].get("cluster"), nodes[second].get("cluster")}
        path = rail_path(
            nodes, first, second,
            str(nodes[first]["cluster"])
            if len(clusters) == 1 and None not in clusters else None,
        )
        root_index = next((
            index for index, (root, _) in enumerate(added)
            if root.get("class") == "RailNode"
            and root.get("node") in {nodes[item].get("node") for item in path}
            and not root.get("rail_path")
        ), None)
        if not path:
            raise ValueError(f"reviewed rail path {names!r} is not connected")
        if root_index is None:
            added.append((nodes[first], 3))
            root_index = len(added) - 1
        root, role = added[root_index]
        added[root_index] = ({**root, "rail_path": tuple(path)}, role)
    return added


def include_scoring_render_targets(
    roots: list[tuple[dict[str, object], int]],
    nodes: list[dict[str, object]],
    targets: set[str],
) -> list[tuple[dict[str, object], int]]:
    """Resolve an EndGap GapScript name inside the trigger's exact cluster."""
    clusters = {
        str(endpoint["cluster"]) for endpoint, _ in roots if endpoint.get("cluster")
    }
    tokens = set().union(*(semantic_tokens(target) for target in targets))
    if not clusters or not tokens:
        return roots
    candidates = [
        node for node in nodes
        if node.get("class") == "EnvironmentObject"
        and node.get("cluster") in clusters
        and node.get("node") and node.get("position")
        and tokens & semantic_tokens(node["node"])
        and not str(node["node"]).casefold().startswith(
            ("collision", "inviso", "particle", "trg", "trgp")
        )
        and "shadow" not in str(node["node"]).casefold()
    ]
    return [*roots, (candidates[0], 2)] if len(candidates) == 1 else roots


def include_named_moving_objects(
    roots: list[tuple[dict[str, object], int]],
    nodes: list[dict[str, object]],
    gap_name: str,
) -> list[tuple[dict[str, object], int]]:
    """Add one uniquely named moving vehicle as a live marker target."""
    gap_tokens = semantic_tokens(gap_name)
    matches = [
        node for node in nodes
        if str(node.get("class", "")).casefold() == "vehicle"
        and node.get("node")
        and node.get("position")
        and (node_tokens := semantic_tokens(node["node"]))
        and node_tokens <= gap_tokens
    ]
    return [*roots, (matches[0], 3)] if len(matches) == 1 else roots


def rail_path(
    nodes: list[dict[str, object]], start: int, end: int, cluster: str | None
) -> list[int]:
    """Return the shortest undirected QB Links path inside one rail cluster."""
    neighbors: dict[int, set[int]] = {}
    for index, node in enumerate(nodes):
        if (node.get("class") != "RailNode"
                or cluster is not None and node.get("cluster") != cluster):
            continue
        for linked in node.get("links", ()):
            if (
                linked < len(nodes)
                and nodes[linked].get("class") == "RailNode"
                and (cluster is None or nodes[linked].get("cluster") == cluster)
            ):
                neighbors.setdefault(index, set()).add(linked)
                neighbors.setdefault(linked, set()).add(index)
    pending = deque([(start, [start])])
    visited = {start}
    while pending:
        current, path = pending.popleft()
        if current == end:
            return path
        for linked in neighbors.get(current, ()):
            if linked not in visited:
                visited.add(linked)
                pending.append((linked, [*path, linked]))
    return []


def focus_connected_rail_near_starts(
    roots: list[tuple[dict[str, object], int]],
    nodes: list[dict[str, object]],
    max_distance: float = 750.0,
) -> list[tuple[dict[str, object], int]]:
    """Use the connected rail edge nearest explicit launch geometry."""
    starts = [
        endpoint for endpoint, role in roots
        if role == 0 and endpoint.get("class") == "EnvironmentObject"
        and endpoint.get("position")
    ]
    start_clusters = {endpoint.get("cluster") for endpoint in starts}
    if len(starts) < 2 or len(start_clusters) != 1 or None in start_clusters:
        return roots

    def point(node: dict[str, object]) -> tuple[float, float, float]:
        return tuple(map(float, str(node["position"]).split(",")))

    def distance_to_segment(value, start, end):
        delta = tuple(end[axis] - start[axis] for axis in range(3))
        length = sum(component * component for component in delta)
        amount = 0.0 if length == 0 else max(0.0, min(1.0, sum(
            (value[axis] - start[axis]) * delta[axis] for axis in range(3)
        ) / length))
        closest = tuple(start[axis] + amount * delta[axis] for axis in range(3))
        return sum((value[axis] - closest[axis]) ** 2 for axis in range(3)), closest

    neighbors: dict[int, set[int]] = {}
    edges = set()
    for index, node in enumerate(nodes):
        if node.get("class") != "RailNode" or not node.get("position"):
            continue
        for linked in node.get("links", ()):
            if (
                linked < len(nodes)
                and nodes[linked].get("class") == "RailNode"
                and nodes[linked].get("position")
            ):
                neighbors.setdefault(index, set()).add(linked)
                neighbors.setdefault(linked, set()).add(index)
                edges.add((min(index, linked), max(index, linked)))

    focused = []
    for endpoint, role in roots:
        root = endpoint.get("index")
        if role != 1 or endpoint.get("class") != "RailNode" or root is None:
            focused.append((endpoint, role))
            continue
        component = {int(root)}
        pending = [int(root)]
        while pending:
            current = pending.pop()
            for linked in neighbors.get(current, ()):
                if linked not in component:
                    component.add(linked)
                    pending.append(linked)
        candidates = []
        for left, right in edges:
            if left not in component or right not in component:
                continue
            cluster = nodes[left].get("cluster")
            if (
                not cluster or cluster in start_clusters
                or nodes[right].get("cluster") != cluster
            ):
                continue
            best = min(
                (distance_to_segment(point(start), point(nodes[left]), point(nodes[right]))
                 for start in starts),
                key=lambda candidate: candidate[0],
            )
            candidates.append((best[0], left, right, cluster, best[1]))
        if not candidates:
            focused.append((endpoint, role))
            continue
        best = min(candidates, key=lambda candidate: candidate[0])
        same_cluster = min(
            (candidate for candidate in candidates
             if candidate[3] == endpoint.get("cluster")),
            default=None,
            key=lambda candidate: candidate[0],
        )
        if (
            best[3] == endpoint.get("cluster")
            or best[0] > max_distance * max_distance
            or same_cluster is None
            or best[0] * 4 >= same_cluster[0]
        ):
            focused.append((endpoint, role))
            continue
        replacement = dict(nodes[best[1]])
        replacement["position"] = ",".join(str(value) for value in best[4])
        replacement["rail_path"] = (best[1], best[2])
        focused.append((replacement, role))
    return focused


def inherit_chained_launch_geometry(
    roots: list[tuple[dict[str, object], int]],
    starts_by_name: dict[str, list[dict[str, object]]],
    ends_by_name: dict[str, list[dict[str, object]]],
) -> list[tuple[dict[str, object], int]]:
    """Replace an unclustered handoff plane with its unique launch geometry."""
    inherited = []
    for endpoint, role in roots:
        if (
            role != 0 or endpoint.get("class") != "EnvironmentObject"
            or endpoint.get("cluster") or endpoint.get("index") is None
        ):
            inherited.append((endpoint, role))
            continue
        candidates = {
            start.get("index"): start
            for name, ends in ends_by_name.items()
            if any(end.get("index") == endpoint.get("index") for end in ends)
            for start in starts_by_name.get(name, ())
            if start.get("class") == "EnvironmentObject"
            and start.get("cluster") and start.get("index") is not None
        }
        inherited.append((next(iter(candidates.values())), role)
                         if len(candidates) == 1 else (endpoint, role))
    return inherited


def include_chained_rail_route(
    roots: list[tuple[dict[str, object], int]],
    predecessor_starts: list[dict[str, object]],
    nodes: list[dict[str, object]],
) -> list[tuple[dict[str, object], int]]:
    """Keep exact rail components for a gap continued from a predecessor gap."""
    starts = {endpoint.get("index"): endpoint for endpoint, role in roots
              if role == 0 and endpoint.get("class") == "RailNode"
              and endpoint.get("index") is not None}
    ends = {endpoint.get("index"): endpoint for endpoint, role in roots
            if role == 1 and endpoint.get("class") == "RailNode"
            and endpoint.get("index") is not None}
    if len(starts) != 1 or len(ends) != 1:
        return roots
    start_index, start = next(iter(starts.items()))
    end_index, end = next(iter(ends.items()))
    if start.get("cluster") != end.get("cluster"):
        return roots

    def chain(index: int) -> tuple[int, ...]:
        cluster = nodes[index].get("cluster")
        path = [index]
        while True:
            links = [linked for linked in nodes[path[-1]].get("links", ())
                     if linked < len(nodes) and linked not in path
                     and nodes[linked].get("class") == "RailNode"
                     and nodes[linked].get("cluster") == cluster]
            if len(links) != 1:
                return tuple(path)
            path.append(links[0])

    start_path = chain(int(start_index))
    end_path = chain(int(end_index))
    if len(start_path) < 2 or len(end_path) < 2 or set(start_path) & set(end_path):
        return roots
    predecessors = {
        predecessor.get("index"): predecessor
        for predecessor in predecessor_starts
        if predecessor.get("class") == "RailNode"
        and predecessor.get("index") is not None
    }
    if len(predecessors) != 1:
        return roots
    predecessor_index, predecessor = next(iter(predecessors.items()))
    predecessor_path = chain(int(predecessor_index))
    if len(predecessor_path) < 2:
        return roots

    routed = []
    for endpoint, role in roots:
        replacement = dict(endpoint)
        if role == 0 and endpoint.get("index") == start_index:
            replacement["rail_path"] = start_path
        elif role == 1 and endpoint.get("index") == end_index:
            replacement["rail_path"] = end_path
        routed.append((replacement, role))
    predecessor = dict(predecessor)
    predecessor["rail_path"] = predecessor_path
    routed.append((predecessor, 0))
    return routed


def focus_disconnected_rail_transfer(
    roots: list[tuple[dict[str, object], int]],
    nodes: list[dict[str, object]],
    max_transfer: float = 1200.0,
) -> list[tuple[dict[str, object], int]]:
    """Mark the directed launch rail and nearest disconnected landing edge."""
    starts = {endpoint.get("index"): endpoint for endpoint, role in roots
              if role == 0 and endpoint.get("class") == "RailNode"
              and endpoint.get("index") is not None}
    ends = {endpoint.get("index"): endpoint for endpoint, role in roots
            if role == 1 and endpoint.get("class") == "RailNode"
            and endpoint.get("index") is not None}
    if len(starts) != 1 or len(ends) != 1:
        return roots
    start_index, start_root = next(iter(starts.items()))
    end_index, _ = next(iter(ends.items()))

    launch_path = [int(start_index)]
    while True:
        links = [linked for linked in nodes[launch_path[-1]].get("links", ())
                 if linked < len(nodes) and nodes[linked].get("class") == "RailNode"]
        if len(links) != 1 or links[0] in launch_path:
            break
        launch_path.append(links[0])
    if len(launch_path) < 2:
        return roots

    neighbors: dict[int, set[int]] = {}
    for index, node in enumerate(nodes):
        if node.get("class") != "RailNode":
            continue
        for linked in node.get("links", ()):
            if linked < len(nodes) and nodes[linked].get("class") == "RailNode":
                neighbors.setdefault(index, set()).add(linked)
                neighbors.setdefault(linked, set()).add(index)
    start_component = {int(start_index)}
    pending = [int(start_index)]
    while pending:
        current = pending.pop()
        for linked in neighbors.get(current, ()):
            if linked not in start_component:
                start_component.add(linked)
                pending.append(linked)
    if int(end_index) in start_component:
        return roots
    end_component = {int(end_index)}
    pending = [int(end_index)]
    while pending:
        current = pending.pop()
        for linked in neighbors.get(current, ()):
            if linked not in end_component:
                end_component.add(linked)
                pending.append(linked)

    def point(index: int) -> tuple[float, float, float]:
        return tuple(map(float, str(nodes[index]["position"]).split(",")))

    launch = point(launch_path[-1])
    target = min(end_component, key=lambda index: sum(
        (left - right) ** 2 for left, right in zip(launch, point(index))))
    if target == end_index:
        return roots
    distance = sum((left - right) ** 2 for left, right in zip(launch, point(target)))
    start_cluster = nodes[int(start_index)].get("cluster")
    target_cluster = nodes[target].get("cluster")
    target_links = [linked for linked in nodes[target].get("links", ())
                    if linked in end_component]
    if (
        distance > max_transfer * max_transfer or len(target_links) != 1
        or not start_cluster or not target_cluster or start_cluster == target_cluster
        or len(semantic_tokens(start_cluster) & semantic_tokens(target_cluster)) < 2
    ):
        return roots

    start = dict(start_root)
    start["rail_path"] = tuple(launch_path)
    end = dict(nodes[target])
    return [
        (start if role == 0 and endpoint.get("index") == start_index else
         end if role == 1 and endpoint.get("index") == end_index else endpoint, role)
        for endpoint, role in roots
    ]


def infer_linked_rail_span(
    roots: list[tuple[dict[str, object], int]],
    nodes: list[dict[str, object]],
    max_distance: float = 160.0,
) -> list[tuple[dict[str, object], int]]:
    """Snap reciprocal StartRailGap trigger gates to their linked rail path."""
    bare = [
        (index, endpoint, role)
        for index, (endpoint, role) in enumerate(roots)
        if endpoint.get("class") == "EnvironmentObject"
        and not endpoint.get("cluster")
        and endpoint.get("position")
        and endpoint.get("index") is not None
    ]
    if len(bare) != 2 or bare[0][2] != bare[1][2]:
        return roots

    def point(node: dict[str, object]) -> tuple[float, float, float]:
        return tuple(map(float, str(node["position"]).split(",")))

    def same_position(left: dict[str, object], right: dict[str, object]) -> bool:
        return sum((a - b) ** 2 for a, b in zip(point(left), point(right))) < 1.0

    for current, opposite in ((bare[0], bare[1]), (bare[1], bare[0])):
        endpoint = current[1]
        if not any(
            node.get("starts_rail_gap")
            and endpoint["index"] in node.get("links", ())
            and node.get("position")
            and same_position(node, opposite[1])
            for node in nodes
        ):
            return roots

    limit = max_distance * max_distance
    candidates: list[tuple[float, int, int, list[int]]] = []
    first_tokens = semantic_tokens(bare[0][1].get("node"))
    second_tokens = semantic_tokens(bare[1][1].get("node"))
    for first_index, first in enumerate(nodes):
        cluster = first.get("cluster")
        if (
            first.get("class") != "RailNode" or not cluster
            or not first_tokens.intersection(semantic_tokens(cluster))
        ):
            continue
        first_distance = sum(
            (a - b) ** 2 for a, b in zip(point(first), point(bare[0][1]))
        )
        if first_distance > limit:
            continue
        for second_index, second in enumerate(nodes):
            if (
                second.get("class") != "RailNode"
                or second.get("cluster") != cluster
                or not second_tokens.intersection(semantic_tokens(cluster))
            ):
                continue
            second_distance = sum(
                (a - b) ** 2 for a, b in zip(point(second), point(bare[1][1]))
            )
            if second_distance > limit:
                continue
            path = rail_path(nodes, first_index, second_index, str(cluster))
            if path:
                candidates.append((
                    first_distance + second_distance,
                    first_index,
                    second_index,
                    path,
                ))
    if not candidates:
        return roots

    _, first_index, second_index, path = min(candidates, key=lambda item: item[0])
    replacements = []
    for (_, _, role), node_index in zip(bare, (first_index, second_index)):
        replacement = dict(nodes[node_index])
        replacement["rail_path"] = tuple(path)
        replacements.append((replacement, role))
    replaced = {item[0] for item in bare}
    return [root for index, root in enumerate(roots) if index not in replaced] + replacements


def infer_rail_render_target(
    nodes: list[dict[str, object]],
    path: tuple[int, ...],
    horizontal_limit: float = 48.0,
    vertical_limit: float = 96.0,
) -> dict[str, object] | None:
    """Find a uniquely aligned render object for an exact QB rail path."""
    if len(path) < 2:
        return None
    rails = [nodes[index] for index in path]
    cluster = rails[0].get("cluster")
    if not cluster or any(node.get("cluster") != cluster for node in rails):
        return None
    guide_tokens = set().union(*(semantic_tokens(node.get("node")) for node in rails))

    def point(node: dict[str, object]) -> tuple[float, float, float]:
        return tuple(map(float, str(node["position"]).split(",")))

    def distance_to_segment(
        value: tuple[float, float, float],
        start: tuple[float, float, float],
        end: tuple[float, float, float],
    ) -> tuple[float, float]:
        delta_x = end[0] - start[0]
        delta_z = end[2] - start[2]
        length = delta_x * delta_x + delta_z * delta_z
        amount = 0.0 if length == 0 else max(0.0, min(1.0, (
            (value[0] - start[0]) * delta_x
            + (value[2] - start[2]) * delta_z
        ) / length))
        x = start[0] + amount * delta_x
        y = start[1] + amount * (end[1] - start[1])
        z = start[2] + amount * delta_z
        return (value[0] - x) ** 2 + (value[2] - z) ** 2, abs(value[1] - y)

    segments = [(point(left), point(right)) for left, right in zip(rails, rails[1:])]
    candidates = []
    for node in nodes:
        if (
            node.get("class") != "EnvironmentObject"
            or node.get("cluster") != cluster
            or not node.get("node")
            or not node.get("position")
        ):
            continue
        shared = semantic_tokens(node["node"]).intersection(guide_tokens)
        if len(shared) < 2:
            continue
        horizontal, vertical = min(
            (distance_to_segment(point(node), start, end) for start, end in segments),
            key=lambda distance: distance[0],
        )
        if horizontal <= horizontal_limit * horizontal_limit and vertical <= vertical_limit:
            candidates.append((horizontal, vertical, -len(shared), node))
    if len(candidates) != 1:
        return None
    return candidates[0][3]


def load_route_sources(
    specs: list[str], script_sources: dict[str, tuple[Path, ...]] | None = None
) -> dict[str, dict[str, object]]:
    routes: dict[str, dict[str, object]] = {}
    script_sources = script_sources or {}
    for spec in specs:
        level, separator, filename = spec.partition("=")
        if not separator or not level or not filename:
            raise ValueError(f"invalid --level-source {spec!r}; expected LEVEL=FILE")
        routes[level] = parse_decompiled(Path(filename), script_sources.get(level, ()))[0]
    return routes


def load_script_sources(specs: list[str]) -> dict[str, tuple[Path, ...]]:
    sources: dict[str, list[Path]] = {}
    for spec in specs:
        level, separator, filename = spec.partition("=")
        if not separator or not level or not filename:
            raise ValueError(f"invalid --script-source {spec!r}; expected LEVEL=FILE")
        sources.setdefault(level, []).append(Path(filename))
    return {level: tuple(paths) for level, paths in sources.items()}


def retain_other_levels(
    output: Path, replaced_gaps: set[tuple[int, int]]
) -> tuple[list[str], list[str]]:
    if not output.exists():
        return [], []
    sections: tuple[list[str], list[str]] = ([], [])
    section = -1
    for line in output.read_text(encoding="utf-8").splitlines():
        if line.startswith("constexpr std::array<GapHighlightEndpoint"):
            section = 0
        elif line.startswith("constexpr std::array<GapRailSegment"):
            section = 1
        elif section >= 0 and line.startswith("    {0x"):
            checksum = int(line[7:15], 16)
            level_match = re.search(r",\s*(\d+)u\},$", line)
            level = int(level_match.group(1)) if level_match else 0
            if level and (checksum, level) not in replaced_gaps:
                sections[section].append(line)
            elif not level and checksum not in {gap for gap, _ in replaced_gaps}:
                sections[section].append(line)
    return sections


def fallback_markers(
    report: dict, covered: set[tuple[int, int]]
) -> list[tuple[int, float, float, float, int]]:
    markers = []
    for gap in report["gap_coverage"]:
        key = (gap["checksum"], LEVEL_IDS[gap["level"]])
        roots = gap.get("endpoint_roots") or gap.get("endpoint_roots_sample") or ()
        if key in covered or not roots:
            continue
        positions = [
            tuple(float(value) for value in root["position"].split(","))
            for root in roots if root.get("position")
        ]
        if not positions:
            continue
        markers.append((gap["checksum"], *(
            sum(position[axis] for position in positions) / len(positions)
            for axis in range(3)
        ), key[1]))
        covered.add(key)
    return markers


def main() -> None:
    parser = argparse.ArgumentParser(description="Build native gap endpoint metadata.")
    parser.add_argument("report", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--level", default="canada")
    parser.add_argument("--decompiled", type=Path)
    parser.add_argument(
        "--level-source",
        action="append",
        default=[],
        metavar="LEVEL=FILE",
        help="repeat for each level to generate one combined table",
    )
    parser.add_argument("--script-source", action="append", default=[], metavar="LEVEL=FILE")
    parser.add_argument(
        "--merge-existing",
        action="store_true",
        help="replace selected levels while retaining other levels already in OUTPUT",
    )
    parser.add_argument("--fallback-output", type=Path)
    args = parser.parse_args()

    report = json.loads(args.report.read_text(encoding="utf-8"))
    reviewed_targets = json.loads(
        Path(__file__).with_name("gap_highlight_targets.json").read_text(
            encoding="utf-8"
        )
    )
    script_sources = load_script_sources(args.script_source)
    route_sources = load_route_sources(args.level_source, script_sources)
    if not route_sources:
        route_sources[args.level] = (
            parse_decompiled(args.decompiled, script_sources.get(args.level, ()))[0]
            if args.decompiled
            else {"nodes": [], "starts": {}, "ends": {}}
        )
    endpoints: list[tuple[int, int, int, int, int, float, float, float]] = []
    rail_clusters: dict[tuple[str, int], set[str]] = {}
    rail_endpoints: dict[
        tuple[str, int], list[tuple[str, str, int, tuple[int, ...]]]
    ] = {}
    for gap in report["gap_coverage"]:
        level = gap["level"]
        route_data = route_sources.get(level)
        if route_data is None:
            continue
        end_roots = route_data["ends"].get(gap["name"], ())
        if not end_roots:
            end_roots = [
                endpoint
                for route in gap.get("routes", ())
                for endpoint in route_data.get("callers", {}).get(route.get("script"), ())
            ]
        if not end_roots:
            end_roots = gap.get("endpoint_roots", gap.get("endpoint_roots_sample", ()))
        roots = [(endpoint, 1) for endpoint in end_roots]
        start_roots = list(route_data["starts"].get(gap["name"], ()))
        start_roots.extend(
            endpoint
            for gap_id in gap.get("gap_ids", ())
            for endpoint in route_data.get("starts_by_gap_id", {}).get(
                str(gap_id).casefold(), ()
            )
        )
        start_roots.extend(
            endpoint
            for route in gap.get("routes", ())
            for endpoint in route.get("start_roots", ())
        )
        roots.extend(
            (endpoint, 0)
            for endpoint in start_roots
        )
        start_clusters = {
            endpoint.get("cluster") for endpoint in start_roots
            if endpoint.get("cluster")
        }
        if len(start_clusters) == 1:
            start_cluster = next(iter(start_clusters))
            cluster_nodes = [
                node for node in route_data["nodes"]
                if node.get("cluster") == start_cluster
            ]
            if any(node.get("class") == "BouncyObject" for node in cluster_nodes):
                focus = [
                    node for node in cluster_nodes
                    if node.get("class") != "RailNode"
                    and not str(node.get("node", "")).startswith("#")
                    and semantic_tokens(node.get("node")) & semantic_tokens(start_cluster)
                ]
                if focus:
                    roots = [(endpoint, 0) for endpoint in start_roots]
                    roots.extend((endpoint, 2) for endpoint in focus)
        roots = inherit_chained_launch_geometry(
            roots, route_data["starts"], route_data["ends"])
        roots = include_chained_rail_route(
            roots, [
                endpoint
                for gap_id in gap.get("gap_ids", ())
                for endpoint in route_data.get(
                    "predecessor_starts_by_gap_id", {}).get(
                        str(gap_id).casefold(), ())
            ], route_data["nodes"])
        roots = focus_connected_rail_near_starts(roots, route_data["nodes"])
        roots = focus_disconnected_rail_transfer(roots, route_data["nodes"])
        roots = snap_duplicate_coping(roots, route_data["nodes"])
        roots = snap_paired_transfer_planes(roots, route_data["nodes"])
        roots = infer_linked_rail_span(roots, route_data["nodes"])
        for path in {
            tuple(endpoint.get("rail_path", ()))
            for endpoint, _ in roots
            if endpoint.get("rail_path")
        }:
            target = infer_rail_render_target(route_data["nodes"], path)
            if target is not None:
                roots.append((target, 2))
        roots = attach_linked_launch_geometry(
            roots,
            route_data["nodes"],
            [
                endpoint
                for routes in (route_data["starts"], route_data["ends"])
                for endpoints in routes.values()
                for endpoint in endpoints
            ],
        )
        roots = include_parameterized_render_targets(
            roots,
            route_data["nodes"],
            gap.get("routes", ()),
            {
                target
                for route in gap.get("routes", ())
                for target in route_data.get("call_targets", {}).get(
                    route.get("script"), ())
            },
        )
        roots = include_named_moving_objects(roots, route_data["nodes"], gap["name"])
        roots = include_semantic_render_targets(roots, route_data["nodes"])
        roots = include_scoring_render_targets(
            roots, route_data["nodes"],
            route_data.get("score_targets", {}).get(gap["name"], set()),
        )
        roots = include_root_cluster_render_targets(roots, route_data["nodes"])
        roots = attach_aligned_clusters(roots, route_data["nodes"])
        roots.extend(infer_paired_cluster_endpoints(roots, route_data["nodes"]))
        overrides = reviewed_targets.get(level, {}).get(gap["name"], {})
        if overrides.get("copy"):
            overrides = reviewed_targets[level][overrides["copy"]]
        roots = include_reviewed_highlight_overrides(
            roots,
            route_data["nodes"],
            overrides,
        )
        seen: set[tuple[object, int, object]] = set()
        for endpoint, role in roots:
            key = (
                endpoint.get("node") or endpoint.get("source_node")
                or endpoint.get("cluster"),
                role,
                endpoint.get("position")
                if not endpoint.get("node") and not endpoint.get("source_node")
                else None,
            )
            if key in seen or not endpoint.get("position"):
                continue
            seen.add(key)
            position = tuple(float(value) for value in endpoint["position"].split(","))
            endpoints.append((
                gap["checksum"],
                qb_checksum(endpoint["node"]),
                qb_checksum(endpoint["cluster"]),
                qb_checksum(
                    None if endpoint.get("class") == "EnvironmentObject"
                    and not endpoint.get("node") and endpoint.get("cluster")
                    else endpoint["class"]
                ),
                role,
                *position,
                LEVEL_IDS[level],
            ))
            if endpoint.get("class") == "RailNode" and endpoint.get("cluster"):
                rail_clusters.setdefault((level, gap["checksum"]), set()).add(endpoint["cluster"])
                if endpoint.get("node"):
                    rail_endpoints.setdefault((level, gap["checksum"]), []).append((
                        str(endpoint["node"]), str(endpoint["cluster"]), role,
                        tuple(endpoint.get("rail_path", ())),
                    ))
            if endpoint.get("rail_cluster"):
                rail_clusters.setdefault((level, gap["checksum"]), set()).add(endpoint["cluster"])

    rail_segments: list[tuple[int, float, float, float, float, float, float]] = []
    seen_segments: set[tuple[int, int, int]] = set()
    for (level, gap), clusters in rail_clusters.items():
        nodes = route_sources[level]["nodes"]
        explicit = rail_endpoints.get((level, gap), ())
        path_clusters = {cluster for _, cluster, _, path in explicit if path}
        path_edges = {
            (min(left, right), max(left, right))
            for _, _, _, path in explicit if path
            for left, right in zip(path, path[1:])
        }
        alternative_clusters = {
            cluster
            for cluster in clusters
            for role in (0, 1)
            if sum(
                endpoint_cluster == cluster and endpoint_role == role
                for _, endpoint_cluster, endpoint_role, _ in explicit
            ) >= 2
            and cluster not in path_clusters
        }
        alternative_endpoints = {
            cluster: {
                endpoint for endpoint, endpoint_cluster, _, _ in explicit
                if endpoint_cluster == cluster
            }
            for cluster in alternative_clusters
        }
        direct_edge_clusters: set[str] = set()
        for _, _, _, path in explicit:
            for left, right in zip(path, path[1:]):
                key = (gap, min(left, right), max(left, right))
                if key in seen_segments:
                    continue
                seen_segments.add(key)
                start = tuple(float(value) for value in str(nodes[left]["position"]).split(","))
                end = tuple(float(value) for value in str(nodes[right]["position"]).split(","))
                rail_segments.append((gap, *start, *end, LEVEL_IDS[level]))
        for endpoint_name, cluster, _, path in explicit:
            if path:
                continue
            index = next((
                index for index, node in enumerate(nodes)
                if node.get("node") == endpoint_name
                and node.get("cluster") == cluster
            ), None)
            if index is None:
                continue
            for link in nodes[index]["links"]:
                if link >= len(nodes):
                    continue
                linked = nodes[link]
                if (
                    linked.get("class") != "RailNode"
                    or linked.get("cluster") == cluster
                ):
                    continue
                key = (gap, min(index, link), max(index, link))
                if key in seen_segments:
                    continue
                start = tuple(float(value) for value in str(nodes[index]["position"]).split(","))
                end = tuple(float(value) for value in str(linked["position"]).split(","))
                if not is_local_rail_link(start, end, 2000.0):
                    continue
                direct_edge_clusters.add(cluster)
                seen_segments.add(key)
                rail_segments.append((gap, *start, *end, LEVEL_IDS[level]))
        for index, node in enumerate(nodes):
            if node.get("class") != "RailNode" or node.get("cluster") not in clusters:
                continue
            if node.get("cluster") in direct_edge_clusters:
                continue
            for link in node["links"]:
                if link >= len(nodes):
                    continue
                linked = nodes[link]
                if linked.get("class") != "RailNode" or linked.get("cluster") != node.get("cluster"):
                    continue
                if (
                    node.get("cluster") in alternative_clusters
                    and node.get("node") not in alternative_endpoints[node["cluster"]]
                    and linked.get("node") not in alternative_endpoints[node["cluster"]]
                ):
                    continue
                if (
                    node.get("cluster") in path_clusters
                    and (min(index, link), max(index, link)) not in path_edges
                ):
                    continue
                key = (gap, min(index, link), max(index, link))
                if key in seen_segments:
                    continue
                seen_segments.add(key)
                start = tuple(float(value) for value in str(node["position"]).split(","))
                end = tuple(float(value) for value in str(linked["position"]).split(","))
                rail_segments.append((gap, *start, *end, LEVEL_IDS[level]))

    endpoint_lines = [
        "    {0x%08xu, 0x%08xu, 0x%08xu, 0x%08xu, %du, %.6ff, %.6ff, %.6ff, %du}," % endpoint
        for endpoint in endpoints
    ]
    rail_lines = [
        "    {0x%08xu, %.6ff, %.6ff, %.6ff, %.6ff, %.6ff, %.6ff, %du}," % segment
        for segment in rail_segments
    ]
    if args.merge_existing:
        replaced_gaps = {
            (gap["checksum"], LEVEL_IDS[gap["level"]])
            for gap in report["gap_coverage"]
            if gap["level"] in route_sources
        }
        old_endpoints, old_rails = retain_other_levels(args.output, replaced_gaps)
        endpoint_lines = old_endpoints + endpoint_lines
        rail_lines = old_rails + rail_lines

    lines = [
        "// Generated by tools/make_gap_highlights.py from reviewed QB evidence.",
        "constexpr std::array<GapHighlightEndpoint, %d> kGapHighlightEndpoints{{" % len(endpoint_lines),
    ]
    lines.extend(endpoint_lines)
    lines.append("}};")
    lines.append(
        "constexpr std::array<GapRailSegment, %d> kGapRailSegments{{" % len(rail_lines)
    )
    lines.extend(rail_lines)
    lines.append("}};")
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")

    if args.fallback_output:
        covered = {
            (int(match.group(1), 16), int(match.group(2)))
            for line in endpoint_lines
            if (match := re.match(
                r"\s*\{0x([0-9a-f]{8})u,.*?,\s*(\d+)u\},$", line))
        }
        markers = fallback_markers(report, covered)
        fallback = [
            "// Generated fallback crosses from reviewed QB endpoint evidence.",
            "constexpr std::array<GapHighlightEndpoint, %d> kGapFallbackMarkers{{" % len(markers),
            *("    {0x%08xu, 0u, 0u, 0u, 0u, %.6ff, %.6ff, %.6ff, %du}," % marker for marker in markers),
            "}};",
        ]
        args.fallback_output.write_text("\n".join(fallback) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
