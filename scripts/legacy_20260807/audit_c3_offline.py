#!/usr/bin/env python3
"""Offline C3 trust-gated marginal-cost reallocation audit.

Replays recorded C2/PRCT telemetry only. This is not a simulator run and not a
paper-level performance claim.
"""

import argparse
import csv
import json
import math
import statistics
from collections import Counter, defaultdict
from pathlib import Path

METHODS = ("b0", "b1", "b2", "b3", "c3")


def finite(value):
    return isinstance(value, (int, float)) and math.isfinite(value)


def method_from_name(name):
    for method in METHODS:
        if name.startswith(method + "_"):
            return method
    return "unknown"


def instance_from_name(name):
    marker = "_run_"
    if marker in name:
        return name.rsplit(marker, 1)[-1]
    return name


def load_manifest(run_dir):
    result = {}
    path = run_dir / "run_manifest.txt"
    try:
        for raw in path.read_text(encoding="utf-8").splitlines():
            if "=" in raw:
                key, _, value = raw.partition("=")
                result[key.strip()] = value.strip()
    except OSError:
        pass
    return result


def load_events(run_dir):
    events = []
    errors = []
    for path in sorted(run_dir.glob("telemetry_drone_*.jsonl")):
        try:
            for line_number, raw in enumerate(
                path.read_text(encoding="utf-8").splitlines(), 1
            ):
                try:
                    events.append(json.loads(raw))
                except json.JSONDecodeError as exc:
                    errors.append(f"{path}:{line_number}: {exc}")
        except OSError as exc:
            errors.append(f"{path}: {exc}")
    return events, errors


def rounded_goal(event):
    keys = ("goal_x", "goal_y", "goal_z")
    if not all(key in event for key in keys):
        return None
    try:
        return tuple(round(float(event[key]), 1) for key in keys)
    except (TypeError, ValueError):
        return None


def make_failure_records(events):
    target_by_plan = {}
    for event in events:
        if event.get("event") != "target_selection":
            continue
        drone = event.get("drone_id")
        plan_seq = event.get("expected_plan_seq")
        frontier = event.get("selected_frontier_id")
        if isinstance(drone, int) and isinstance(plan_seq, int) and isinstance(frontier, int):
            target_by_plan[(drone, plan_seq)] = frontier

    records = []
    for event in events:
        if event.get("event") != "astar_search_diagnostic":
            continue
        if event.get("termination") != "open_set_exhausted":
            continue
        drone = event.get("drone_id")
        plan_seq = event.get("plan_seq")
        if not isinstance(drone, int) or not isinstance(plan_seq, int):
            continue
        goal = rounded_goal(event)
        if goal is None:
            continue
        records.append({
            "wall_time_s": event.get("wall_time_s"),
            "drone_id": drone,
            "plan_seq": plan_seq,
            "frontier_id": target_by_plan.get((drone, plan_seq), -1),
            "goal": goal,
        })
    records.sort(
        key=lambda rec: (
            rec["wall_time_s"] if finite(rec["wall_time_s"]) else 0.0,
            rec["plan_seq"],
        )
    )
    return records


def parse_register_map_versions(events):
    versions = set()
    for event in events:
        if event.get("event") != "prct_retry_suppression_register":
            continue
        drone = event.get("drone_id")
        frontier = event.get("frontier_id")
        version = event.get("map_version")
        goal = rounded_goal(event)
        if isinstance(drone, int) and isinstance(frontier, int) and isinstance(version, int) and goal is not None:
            versions.add((drone, frontier, goal, version))
    return versions


def owner_stuck_cost(repeat_count, elapsed_s, p):
    repeat_cost = p["owner_repeat_cost_s"] * max(0, repeat_count - 1)
    return p["owner_stuck_alpha"] * elapsed_s + p["owner_fallback_penalty_s"] + repeat_cost


def peer_marginal_cost(path_length, trust, p, peer_load=0.0):
    travel = path_length / p["nominal_speed_m_s"] if p["nominal_speed_m_s"] > 1e-6 else path_length
    trust = min(1.0, max(0.0, trust))
    return (
        travel
        + p["load_weight"] * peer_load
        + p["handoff_overhead_s"]
        + (1.0 - trust) * p["trust_penalty_s"]
    )


def collect_peer_cert_oracle(root):
    oracle = defaultdict(list)
    for run_dir in sorted(root.glob("*/*/b*")):
        if not run_dir.is_dir():
            continue
        method = method_from_name(run_dir.name)
        if method not in ("b2", "b3"):
            if method != "b0" or not oracle:
                continue
            if not oracle.get((scene, uav_num)):
                continue
        manifest = load_manifest(run_dir)
        scene = manifest.get("scene") or run_dir.parent.parent.name
        uav_num = manifest.get("drone_num", "")
        events, _ = load_events(run_dir)
        for event in events:
            if event.get("event") != "peer_local_map_reachability_response":
                continue
            if event.get("success") is not True:
                continue
            path_length = event.get("path_length_m")
            if finite(path_length):
                oracle[(scene, uav_num)].append(path_length)
    return oracle


def audit_run(run_dir, p, oracle=None):
    manifest = load_manifest(run_dir)
    events, errors = load_events(run_dir)
    method = method_from_name(run_dir.name)
    scene = manifest.get("scene") or run_dir.parent.parent.name
    uav_num = manifest.get("drone_num", "")
    failures = make_failure_records(events)
    versions = parse_register_map_versions(events)

    responses = defaultdict(list)
    for event in events:
        if event.get("event") == "peer_local_map_reachability_response":
            plan_seq = event.get("owner_plan_seq")
            if isinstance(plan_seq, int):
                responses[plan_seq].append(event)

    sent = {}
    for event in events:
        if event.get("event") == "peer_takeover_goal_sent":
            plan_seq = event.get("owner_plan_seq")
            if isinstance(plan_seq, int):
                sent[plan_seq] = event

    repeat_counts = Counter()
    chain_start = {}
    cooldown_until = {}
    c3_attempts = 0
    low_repeat = 0
    cooldown_skip = 0
    no_cert = 0
    no_benefit = 0
    triggers = 0
    synthetic_cert = 0
    synthetic_no_benefit = 0
    synthetic_triggers = 0
    max_repeat = 0
    benefits = []
    triggered_plan_seqs = set()

    for rec in failures:
        wall = rec["wall_time_s"]
        if not finite(wall):
            continue
        key = (rec["drone_id"], rec["frontier_id"], rec["goal"])
        if any(k[:3] == key and k[3] > 0 for k in versions):
            # Conservative map-version handling: if this exact target has ever
            # been registered with a real map_version, keep that version in key.
            key = (rec["drone_id"], rec["frontier_id"], rec["goal"], max(k[3] for k in versions if k[:3] == key))
        else:
            key = (rec["drone_id"], rec["frontier_id"], rec["goal"], 0)
        repeat_counts[key] += 1
        repeat = repeat_counts[key]
        if repeat == 1:
            chain_start[key] = wall
        max_repeat = max(max_repeat, repeat)

        if method not in ("b2", "b3"):
            if method != "b0" or not oracle:
                continue
            if not oracle.get((scene, uav_num)):
                continue
        if wall < cooldown_until.get(key, -1e300):
            cooldown_skip += 1
            continue
        if repeat < p["min_repeat_count"]:
            low_repeat += 1
            continue

        c3_attempts += 1
        stuck = owner_stuck_cost(repeat, wall - chain_start[key], p)
        certs = [
            e for e in responses.get(rec["plan_seq"], [])
            if e.get("success") is True
            and finite(e.get("peer_state_age_s"))
            and e["peer_state_age_s"] <= p["peer_state_max_age_s"]
            and finite(e.get("path_length_m"))
        ]
        best = -math.inf
        best_marginal = math.inf
        synthetic = False
        if certs:
            for cert in certs:
                peer_load = cert.get("peer_load", 0.0)
                if not finite(peer_load):
                    peer_load = 0.0
                marginal = peer_marginal_cost(
                    cert["path_length_m"], p["trust"], p, peer_load
                )
                benefit = stuck - marginal
                if benefit > best:
                    best = benefit
                    best_marginal = marginal
        elif method == "b0" and oracle and oracle.get((scene, uav_num)):
            synthetic = True
            synthetic_cert += 1
            path_length = statistics.median(oracle[(scene, uav_num)])
            best_marginal = peer_marginal_cost(path_length, p["trust"], p, 0.0)
            best = stuck - best_marginal
        else:
            no_cert += 1
            continue
        benefits.append(best)
        if best > p["benefit_margin_s"]:
            triggers += 1
            triggered_plan_seqs.add(rec["plan_seq"])
            cooldown_until[key] = wall + p["cooldown_s"]
            if synthetic:
                synthetic_triggers += 1
        else:
            no_benefit += 1
            cooldown_until[key] = wall + p["cooldown_s"]
            if synthetic:
                synthetic_no_benefit += 1

    makespan = None
    finish_count = 0
    init_times = []
    finish_times = []
    for event in events:
        if event.get("event") == "initialize" and finite(event.get("wall_time_s")):
            init_times.append(event["wall_time_s"])
        if event.get("event") == "fsm_transition" and event.get("to") == "FINISH":
            finish_count += 1
            if finite(event.get("wall_time_s")):
                finish_times.append(event["wall_time_s"])
    if init_times and finish_times:
        makespan = max(finish_times) - min(init_times)

    return {
        "run_dir": str(run_dir),
        "method": method,
        "scene": scene,
        "uav_num": uav_num,
        "instance": instance_from_name(run_dir.name),
        "astar_failures": len(failures),
        "repeat_keys": len(repeat_counts),
        "max_repeat": max_repeat,
        "c3_attempts": c3_attempts,
        "skipped_low_repeat": low_repeat,
        "skipped_by_cooldown": cooldown_skip,
        "no_cert_count": no_cert,
        "no_benefit_count": no_benefit,
        "c3_trigger_count": triggers,
        "synthetic_cert_count": synthetic_cert,
        "synthetic_no_benefit_count": synthetic_no_benefit,
        "synthetic_trigger_count": synthetic_triggers,
        "benefit_median_s": statistics.median(benefits) if benefits else None,
        "benefit_max_s": max(benefits) if benefits else None,
        "actual_b3_sent": len(sent),
        "actual_b3_sent_c3_accept": sum(1 for plan_seq in sent if plan_seq in triggered_plan_seqs),
        "actual_b3_sent_c3_reject": sum(1 for plan_seq in sent if plan_seq not in triggered_plan_seqs),
        "makespan_s_proxy": makespan,
        "finish_count": finish_count,
        "json_parse_error_count": len(errors),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("/home/c2dev/c2_explorer_reproduction/logs/reachability_retry/formal"))
    parser.add_argument("--out-json", type=Path, required=True)
    parser.add_argument("--out-csv", type=Path, required=True)
    parser.add_argument("--min-repeat-count", type=int, default=3)
    parser.add_argument("--benefit-margin-s", type=float, default=1.0)
    parser.add_argument("--trust", type=float, default=0.5)
    parser.add_argument("--handoff-overhead-s", type=float, default=0.5)
    parser.add_argument("--trust-penalty-s", type=float, default=2.0)
    parser.add_argument("--nominal-speed-m-s", type=float, default=2.0)
    parser.add_argument("--load-weight", type=float, default=0.5)
    parser.add_argument("--owner-fallback-penalty-s", type=float, default=3.0)
    parser.add_argument("--owner-stuck-alpha", type=float, default=1.0)
    parser.add_argument("--owner-repeat-cost-s", type=float, default=0.3)
    parser.add_argument("--cert-wait-s", type=float, default=0.25)
    parser.add_argument("--cooldown-s", type=float, default=5.0)
    parser.add_argument("--peer-state-max-age-s", type=float, default=2.0)
    parser.add_argument(
        "--inject-b0-oracle",
        action="store_true",
        help="For B0 long chains without recorded certificates, use the same-scene B2/B3 empirical peer path length distribution as a shadow oracle.",
    )
    args = parser.parse_args()

    p = {
        "min_repeat_count": args.min_repeat_count,
        "benefit_margin_s": args.benefit_margin_s,
        "trust": args.trust,
        "handoff_overhead_s": args.handoff_overhead_s,
        "trust_penalty_s": args.trust_penalty_s,
        "nominal_speed_m_s": args.nominal_speed_m_s,
        "load_weight": args.load_weight,
        "owner_fallback_penalty_s": args.owner_fallback_penalty_s,
        "owner_stuck_alpha": args.owner_stuck_alpha,
        "owner_repeat_cost_s": args.owner_repeat_cost_s,
        "cert_wait_s": args.cert_wait_s,
        "cooldown_s": args.cooldown_s,
        "peer_state_max_age_s": args.peer_state_max_age_s,
    }
    oracle = collect_peer_cert_oracle(args.root) if args.inject_b0_oracle else None

    rows = []
    for run_dir in sorted(args.root.glob("*/*/b*")):
        if run_dir.is_dir():
            rows.append(audit_run(run_dir, p, oracle))
    if not rows:
        raise SystemExit("no run dirs matched under " + str(args.root))

    config_agg = defaultdict(lambda: Counter())
    for row in rows:
        key = (row["method"], row["scene"], row["uav_num"])
        agg = config_agg[key]
        agg["n"] += 1
        for field in (
            "astar_failures",
            "repeat_keys",
            "c3_attempts",
            "skipped_low_repeat",
            "skipped_by_cooldown",
            "no_cert_count",
            "no_benefit_count",
            "c3_trigger_count",
            "synthetic_cert_count",
            "synthetic_no_benefit_count",
            "synthetic_trigger_count",
            "actual_b3_sent",
            "actual_b3_sent_c3_accept",
            "actual_b3_sent_c3_reject",
            "json_parse_error_count",
        ):
            agg[field] += row[field] or 0
        agg["max_repeat"] = max(agg["max_repeat"], row["max_repeat"])

    config_rows = []
    for (method, scene, uav_num), agg in sorted(config_agg.items()):
        config_rows.append({
            "method": method,
            "scene": scene,
            "uav_num": uav_num,
            "n": agg["n"],
            **{field: agg[field] for field in (
                "astar_failures",
                "repeat_keys",
                "c3_attempts",
                "skipped_low_repeat",
                "skipped_by_cooldown",
                "no_cert_count",
                "no_benefit_count",
                "c3_trigger_count",
                "synthetic_cert_count",
                "synthetic_no_benefit_count",
                "synthetic_trigger_count",
                "actual_b3_sent",
                "actual_b3_sent_c3_accept",
                "actual_b3_sent_c3_reject",
                "max_repeat",
                "json_parse_error_count",
            )},
        })

    csv_rows = rows
    with args.out_csv.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(csv_rows[0].keys()))
        writer.writeheader()
        writer.writerows(csv_rows)

    args.out_json.write_text(
        json.dumps({
            "schema_version": 1,
            "params": p,
            "scope": "offline C3 v2 replay; not a paper-level result",
            "rows": rows,
            "config_rows": config_rows,
        }, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"rows={len(rows)}")
    print(f"configs={len(config_rows)}")
    print(f"triggers={sum(r['c3_trigger_count'] for r in rows)}")
    print(f"no_cert={sum(r['no_cert_count'] for r in rows)}")
    print(f"no_benefit={sum(r['no_benefit_count'] for r in rows)}")


if __name__ == "__main__":
    main()
