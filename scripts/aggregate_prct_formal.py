#!/usr/bin/env python3
"""Aggregate formal PRCT-C2 paired runs into CSV/JSON audit tables."""

import argparse
import csv
import json
import math
import random
import statistics
from collections import defaultdict
from pathlib import Path

METHODS = ("b0", "b1", "b2", "b3")


def percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    index = (len(ordered) - 1) * fraction
    low = int(math.floor(index))
    high = int(math.ceil(index))
    if low == high:
        return ordered[low]
    return ordered[low] + (ordered[high] - ordered[low]) * (index - low)


def finite(value):
    return isinstance(value, (int, float)) and math.isfinite(value)


def load_json(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


def load_manifest(run_dir):
    result = {}
    path = Path(run_dir) / "run_manifest.txt"
    try:
        for raw in path.read_text(encoding="utf-8").splitlines():
            if "=" in raw:
                key, _, value = raw.partition("=")
                result[key.strip()] = value.strip()
    except OSError:
        pass
    return result


def method_from_name(name):
    for method in METHODS:
        if name.startswith(method + "_"):
            return method
    return None


def instance_from_name(name):
    marker = "_run_"
    if marker in name:
        return name.rsplit(marker, 1)[-1]
    return name


def int_or(value, default=0):
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def float_or(value, default=None):
    try:
        result = float(value)
    except (TypeError, ValueError):
        return default
    return result if math.isfinite(result) else default


def sum_lkh_failures(telemetry):
    result = 0
    for value in (telemetry.get("lkh") or {}).values():
        if isinstance(value, dict):
            result += int_or(value.get("failure"))
    return result


def make_run_row(run_dir):
    run_dir = Path(run_dir)
    name = run_dir.name
    manifest = load_manifest(run_dir)
    telemetry = load_json(run_dir / "telemetry_summary.json")
    audit = load_json(run_dir / "peer_takeover_audit.json")

    method = method_from_name(name) or ""
    scene = manifest.get("scene") or run_dir.parent.parent.name
    uav_num = int_or(manifest.get("drone_num"))
    event_counts = telemetry.get("event_counts") or {}
    astar = telemetry.get("astar") or {}
    planning = telemetry.get("planning") or {}
    lkh = telemetry.get("lkh") or {}
    lkh_failures = sum_lkh_failures(telemetry)
    lkh_success = sum(
        int_or(v.get("success")) for v in lkh.values() if isinstance(v, dict)
    )
    lkh_durations = []
    for value in lkh.values():
        if isinstance(value, dict):
            for key in ("duration_p95_wall_s", "duration_p50_wall_s"):
                if finite(value.get(key)):
                    lkh_durations.append(value[key])

    makespan = float_or(audit.get("makespan_s_proxy"))
    if makespan is None:
        makespan = float_or(telemetry.get("local_finish_makespan_wall_s"))
    finish_count = int_or(audit.get("finish_count"))
    if finish_count == 0:
        finish_count = len(telemetry.get("finish_drone_ids") or [])

    goal_counts = astar.get("goal_coordinate_counts") or {}
    repeated_goals = sum(1 for count in goal_counts.values() if count >= 2)
    max_goal_failures = max(goal_counts.values(), default=0)
    trajectory_end_reasons = telemetry.get("trajectory_end_reasons") or {}

    row = {
        "run_dir": str(run_dir),
        "scene": scene,
        "uav_num": uav_num,
        "method": method,
        "instance": instance_from_name(name),
        "communication_threshold_m": manifest.get("communication_threshold_m", ""),
        "activation_script": manifest.get("activation_script", ""),
        "prct_enable_retry_suppression": manifest.get(
            "prct_enable_retry_suppression", ""
        ),
        "prct_enable_peer_takeover": manifest.get("prct_enable_peer_takeover", ""),
        "pilot_completed": manifest.get("pilot_completed", ""),
        "completion_observed_all": manifest.get("completion_observed_all", ""),
        "finish_count": finish_count,
        "makespan_s_proxy": makespan,
        "astar_failure_diagnostic_count": int_or(
            astar.get("failure_diagnostic_count")
        ),
        "astar_open_set_exhausted_count": int_or(
            astar.get("termination_counts", {}).get("open_set_exhausted")
        ),
        "astar_repeated_goal_count": repeated_goals,
        "astar_max_goal_failures": max_goal_failures,
        "trajectory_plan_failure_count": int_or(
            planning.get("trajectory_plan_failure_count")
        ),
        "trajectory_plan_astar_failure_count": int_or(
            audit.get("astar_failure_count")
        ),
        "explore_failure_count": int_or(planning.get("explore_failure_count")),
        "explore_success_count": int_or(planning.get("explore_success_count")),
        "lkh_failure_count": lkh_failures,
        "lkh_success_count": lkh_success,
        "lkh_duration_p95_wall_s": max(lkh_durations) if lkh_durations else None,
        "allocation_assignment_count": int_or(
            event_counts.get("allocation_assignment")
        ),
        "allocation_candidate_set_count": int_or(
            telemetry.get("allocation", {}).get("candidate_set_count")
        ),
        "takeover_sent": int_or(audit.get("peer_takeover_goal_sent_count")),
        "takeover_received": int_or(audit.get("peer_takeover_goal_received_count")),
        "takeover_executed": int_or(audit.get("peer_takeover_goal_executed_count")),
        "takeover_dropped": int_or(audit.get("peer_takeover_goal_dropped_count")),
        "takeover_targets": int_or(audit.get("peer_takeover_target_count")),
        "receipt_sent_count": int_or(audit.get("peer_takeover_receipt_sent_count")),
        "receipt_received_count": int_or(audit.get("peer_takeover_receipt_count")),
        "receipt_accepted": int_or(
            audit.get("receipt_status_counts", {}).get("ACCEPTED")
        ),
        "receipt_completed": int_or(
            audit.get("receipt_status_counts", {}).get("COMPLETED")
        ),
        "receipt_rejected": int_or(
            audit.get("receipt_status_counts", {}).get("REJECTED")
        ),
        "receipt_aborted": int_or(
            audit.get("receipt_status_counts", {}).get("ABORTED")
        ),
        "receipt_stale": int_or(audit.get("receipt_status_counts", {}).get("STALE")),
        "handoff_fallback_count": int_or(audit.get("peer_handoff_fallback_count")),
        "peer_reachability_query_count": int_or(
            audit.get("peer_local_map_reachability_query_count")
        ),
        "wait_events_count": int_or(audit.get("handoff_wait_events_count")),
        "wait_transitions_count": int_or(audit.get("wait_handoff_transition_count")),
        "wait_exit_count": int_or(audit.get("wait_handoff_exit_count")),
        "wait_duration_total_wall_s": float_or(
            audit.get("wait_duration_total_wall_s"), 0.0
        ),
        "wait_duration_p50_wall_s": float_or(audit.get("wait_duration_p50_wall_s")),
        "wait_duration_p95_wall_s": float_or(audit.get("wait_duration_p95_wall_s")),
        "retry_suppression_register": int_or(
            audit.get("prct_retry_suppression_register_count")
        ),
        "retry_suppression_skip": int_or(
            audit.get("prct_retry_suppression_skip_count")
        ),
        "takeover_suppressed": int_or(audit.get("prct_takeover_suppressed_count")),
        "trajectory_end_frontier_covered": int_or(
            trajectory_end_reasons.get("frontier_covered")
        ),
        "trajectory_end_periodic_replan": int_or(
            trajectory_end_reasons.get("periodic_replan")
        ),
        "event_trajectory_start": int_or(event_counts.get("trajectory_start")),
        "event_trajectory_end": int_or(event_counts.get("trajectory_end")),
        "json_parse_error_count": len(telemetry.get("json_parse_errors") or [])
        + len(audit.get("errors") or []),
    }
    return row
def rmst(values, timeout):
    if not values:
        return None
    return math.sqrt(sum(v * v for v in values) / len(values))


def effective_makespan(value, timeout):
    if value is None:
        return timeout
    return min(value, timeout)


def summary_stats(values):
    if not values:
        return {
            "n": 0,
            "mean": None,
            "median": None,
            "std": None,
            "p90": None,
            "min": None,
            "max": None,
        }
    return {
        "n": len(values),
        "mean": statistics.mean(values),
        "median": statistics.median(values),
        "std": statistics.stdev(values) if len(values) > 1 else 0.0,
        "p90": percentile(values, 0.90),
        "min": min(values),
        "max": max(values),
    }


def bootstrap_median_ci(diffs, seed=42, iterations=2000):
    rng = random.Random(seed)
    medians = []
    for _ in range(iterations):
        sample = [rng.choice(diffs) for _ in diffs]
        medians.append(statistics.median(sample))
    return [percentile(medians, 0.025), percentile(medians, 0.975)]


def build_paired(scene_uav_map, timeout):
    paired = []
    for (scene, uav_num), instances in sorted(scene_uav_map.items()):
        for baseline in ("b0", "b1", "b2"):
            pairs = []
            for instance, methods in sorted(instances.items()):
                b3 = methods.get("b3")
                base = methods.get(baseline)
                if b3 is None or base is None:
                    continue
                b3_eff = effective_makespan(b3, timeout)
                base_eff = effective_makespan(base, timeout)
                pairs.append(
                    {
                        "instance": instance,
                        "b3": b3_eff,
                        baseline: base_eff,
                        "diff": b3_eff - base_eff,
                        "pct_improvement": (
                            (base_eff - b3_eff) / base_eff * 100.0
                            if base_eff
                            else None
                        ),
                    }
                )
            if not pairs:
                continue
            diffs = [p["diff"] for p in pairs]
            pct = [
                p["pct_improvement"]
                for p in pairs
                if p["pct_improvement"] is not None
            ]
            wins = sum(1 for p in pairs if p["b3"] < p[baseline])
            losses = sum(1 for p in pairs if p["b3"] > p[baseline])
            paired.append(
                {
                    "scene": scene,
                    "uav_num": uav_num,
                    "comparison": "b3_vs_" + baseline,
                    "n_pairs": len(pairs),
                    "b3_wins": wins,
                    "b3_losses": losses,
                    "ties": len(pairs) - wins - losses,
                    "median_diff_s": statistics.median(diffs),
                    "mean_diff_s": statistics.mean(diffs),
                    "median_pct_improvement": (
                        statistics.median(pct) if pct else None
                    ),
                    "bootstrap_median_diff_ci95": bootstrap_median_ci(diffs),
                    "b3_rmst_s": rmst([p["b3"] for p in pairs], timeout),
                    "base_rmst_s": rmst([p[baseline] for p in pairs], timeout),
                    "b3_p90_s": percentile([p["b3"] for p in pairs], 0.90),
                    "base_p90_s": percentile([p[baseline] for p in pairs], 0.90),
                    "b3_unfinished": sum(1 for p in pairs if p["b3"] >= timeout),
                    "base_unfinished": sum(
                        1 for p in pairs if p[baseline] >= timeout
                    ),
                    "pairs": pairs,
                }
            )
    return paired
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(
            "/home/c2dev/c2_explorer_reproduction/logs/reachability_retry/formal"
        ),
    )
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--out-csv", type=Path, required=True)
    parser.add_argument("--out-json", type=Path, required=True)
    args = parser.parse_args()

    rows = []
    for run_dir in sorted(args.root.glob("*/*/b*")):
        if not run_dir.is_dir():
            continue
        if not (run_dir / "telemetry_summary.json").exists():
            continue
        if not (run_dir / "peer_takeover_audit.json").exists():
            continue
        row = make_run_row(run_dir)
        if row["method"] and row["scene"]:
            rows.append(row)

    if not rows:
        raise SystemExit("no formal PRCT rows found under " + str(args.root))

    by_config = defaultdict(list)
    scene_uav_map = defaultdict(lambda: defaultdict(dict))
    for row in rows:
        key = (row["scene"], row["uav_num"])
        by_config[key].append(row)
        effective = row["makespan_s_proxy"]
        if effective is None or row["finish_count"] < int(row["uav_num"]):
            effective = args.timeout
        scene_uav_map[key][row["instance"]][row["method"]] = effective

    config_stats = []
    for (scene, uav_num), group in sorted(by_config.items()):
        for method in METHODS:
            method_rows = [r for r in group if r["method"] == method]
            if not method_rows:
                continue
            finished = [
                r["makespan_s_proxy"]
                for r in method_rows
                if r["makespan_s_proxy"] is not None
                and r["finish_count"] >= int(r["uav_num"])
            ]
            censored = [
                args.timeout
                if r["makespan_s_proxy"] is None
                or r["finish_count"] < int(r["uav_num"])
                else r["makespan_s_proxy"]
                for r in method_rows
            ]
            expected_uav = int(uav_num)
            finish_count = sum(
                1 for r in method_rows if r["finish_count"] >= expected_uav
            )
            config_stats.append(
                {
                    "scene": scene,
                    "uav_num": uav_num,
                    "method": method,
                    "n": len(method_rows),
                    "finish_count": finish_count,
                    "finish_rate": finish_count / len(method_rows),
                    "makespan_finished": summary_stats(finished),
                    "makespan_censored_or_finished": summary_stats(censored),
                    "rmst_s": rmst(censored, args.timeout),
                    "astar_failure_mean": statistics.mean(
                        [r["astar_failure_diagnostic_count"] for r in method_rows]
                    ),
                    "astar_failure_total": sum(
                        r["astar_failure_diagnostic_count"] for r in method_rows
                    ),
                    "traj_failure_total": sum(
                        r["trajectory_plan_failure_count"] for r in method_rows
                    ),
                    "lkh_failure_total": sum(
                        r["lkh_failure_count"] for r in method_rows
                    ),
                    "takeover_sent_total": sum(
                        r["takeover_sent"] for r in method_rows
                    ),
                    "takeover_executed_total": sum(
                        r["takeover_executed"] for r in method_rows
                    ),
                    "wait_total_s": sum(
                        r["wait_duration_total_wall_s"] for r in method_rows
                    ),
                    "fallback_total": sum(
                        r["handoff_fallback_count"] for r in method_rows
                    ),
                    "query_total": sum(
                        r["peer_reachability_query_count"] for r in method_rows
                    ),
                    "suppression_register_total": sum(
                        r["retry_suppression_register"] for r in method_rows
                    ),
                }
            )

    paired = build_paired(scene_uav_map, args.timeout)

    with args.out_csv.open("w", newline="", encoding="utf-8") as handle:
        fieldnames = list(rows[0].keys())
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)

    output = {
        "schema_version": 1,
        "scope": "formal PRCT-C2 paired runs; local FINISH rule only",
        "timeout_s": args.timeout,
        "row_count": len(rows),
        "rows": rows,
        "config_stats": config_stats,
        "paired_comparisons": paired,
        "limitations": [
            "No official C2 random seed protocol was released; run_001/002/003 are instance labels, not upstream seeds.",
            "FINISH is the local FSM observation rule used by the harness, not an upstream global coverage completion certificate.",
            "Makespan is a wall-time proxy from INIT to FINISH; it is not the paper's exact makespan unless verified.",
            "Coverage, collision, and communication-disconnect counts are not yet extracted from raw telemetry/rosbags.",
        ],
    }
    args.out_json.write_text(
        json.dumps(output, indent=2, sort_keys=True) + chr(10), encoding="utf-8"
    )
    print("rows=" + str(len(rows)))
    print("configs=" + str(len(config_stats)))
    print("paired=" + str(len(paired)))
    print("csv=" + str(args.out_csv))
    print("json=" + str(args.out_json))


if __name__ == "__main__":
    main()
