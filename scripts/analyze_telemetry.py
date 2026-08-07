#!/usr/bin/python3
"""Validate and summarize C2 run telemetry.

This emits pilot observations only. It never labels output paper-comparable or
infers task-completion metrics that the upstream paper did not define.
"""

import argparse
import json
import math
from collections import Counter, defaultdict
from pathlib import Path


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


def numeric(value):
    return isinstance(value, (int, float)) and math.isfinite(value)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()

    files = sorted(args.run_dir.glob("telemetry_drone_*.jsonl"))
    summary = {
        "schema_version": 5,
        "paper_comparable": False,
        "status": "pilot-observation-only",
        "run_dir": str(args.run_dir),
        "telemetry_files": [str(path) for path in files],
        "json_parse_errors": [],
        "event_counts": Counter(),
        "per_drone": {},
        "finish_drone_ids": [],
        "finish_wall_time_by_drone": {},
        "local_finish_makespan_wall_s": None,
        "trajectory_end_reasons": Counter(),
        "allocation": {
            "candidate_set_count": 0,
            "successful_result_count": 0,
            "failed_result_count": 0,
            "assignment_event_count": 0,
            "nominal_tour_cost_count": 0,
            "nominal_tour_cost_p50": None,
            "nominal_tour_cost_p95": None,
            "cost_unit": "c2_allocation_cost; not seconds",
        },
        "allocation_seq_audit": {
            "events_with_positive_local_seq": 0,
            "events_with_zero_or_missing_seq": 0,
            "positive_local_seq_values": [],
            "scope": "node-local telemetry only; not a global task identifier",
        },
        "planning": {
            "explore_success_count": 0,
            "explore_failure_count": 0,
            "explore_failure_reasons": Counter(),
            "trajectory_plan_success_count": 0,
            "trajectory_plan_failure_count": 0,
            "trajectory_plan_failure_reasons": Counter(),
            "trajectory_plan_failure_duration_count": 0,
            "trajectory_plan_failure_duration_p50_wall_s": None,
            "trajectory_plan_failure_duration_p95_wall_s": None,
        },
        "astar": {
            "failure_diagnostic_count": 0,
            "termination_counts": Counter(),
            "elapsed_ros_s": [],
            "elapsed_wall_s": [],
            "expanded_nodes": [],
            "discovered_nodes": [],
            "candidate_neighbors": [],
            "node_pool_utilization": [],
            "start_goal_raw_state_counts": Counter(),
            "goal_coordinate_counts": Counter(),
            "target_source_counts": Counter(),
            "failure_target_source_counts": Counter(),
        },
        "trajectory_pairing": {
            "matched_segments": 0,
            "unmatched_start_count": 0,
            "unmatched_end_count": 0,
            "duration_ratio_count": 0,
            "execution_to_planned_duration_p50": None,
            "execution_to_planned_duration_p95": None,
            "execution_to_planned_duration_max": None,
            "scope": "trajectory segments only; not a task-level travel/service metric",
        },
        "task_identity": {
            "allocation_task_event_count": 0,
            "target_selection_count": 0,
            "target_match_kind_counts": Counter(),
            "target_selection_with_stable_center_id": 0,
            "target_selection_with_grid_id": 0,
            "trajectory_start_with_stable_center_id": 0,
            "trajectory_end_with_stable_center_id": 0,
            "scope": "identity telemetry only; not a task-level execution-time label",
        },
        "lkh": defaultdict(lambda: {"success": 0, "failure": 0, "durations_wall_s": []}),
        "limitations": [
            "No upstream machine-readable seed protocol was released.",
            "FINISH state transitions are a local observation rule, not the paper's stated global completion metric.",
            "Task-level travel/service boundaries are not uniquely recoverable from the current telemetry.",
            "Trajectory duration ratios measure interrupted local trajectory segments, not C2 allocation-cost calibration.",
        ],
    }

    trajectory_starts = {}
    trajectory_ends = []
    target_selections = {}
    initialize_wall_by_drone = {}
    nominal_tour_costs = []
    trajectory_plan_failure_durations = []
    allocation_seq_values = set()
    allocation_seq_events = {
        "allocation_request",
        "allocation_candidate_set",
        "allocation_assignment",
        "allocation_result",
        "target_selection",
        "traj_request",
        "traj_result",
        "trajectory_start",
        "trajectory_end",
    }

    for path in files:
        drone_events = Counter()
        drone_id = None
        with path.open(encoding="utf-8") as stream:
            for line_number, line in enumerate(stream, start=1):
                try:
                    event = json.loads(line)
                except json.JSONDecodeError as error:
                    summary["json_parse_errors"].append(
                        {"file": str(path), "line": line_number, "error": str(error)}
                    )
                    continue
                drone_id = event.get("drone_id", drone_id)
                name = event.get("event", "missing_event")
                summary["event_counts"][name] += 1
                drone_events[name] += 1
                if name in allocation_seq_events:
                    allocation_seq = event.get("allocation_seq")
                    if isinstance(allocation_seq, int) and allocation_seq > 0:
                        summary["allocation_seq_audit"]["events_with_positive_local_seq"] += 1
                        allocation_seq_values.add(allocation_seq)
                    else:
                        summary["allocation_seq_audit"]["events_with_zero_or_missing_seq"] += 1
                if name == "fsm_transition" and event.get("to") == "FINISH":
                    summary["finish_drone_ids"].append(event.get("drone_id"))
                    drone_key = event.get("drone_id")
                    wall_time = event.get("wall_time_s")
                    if drone_key is not None and numeric(wall_time):
                        summary["finish_wall_time_by_drone"][str(drone_key)] = wall_time
                if name == "initialize":
                    drone_key = event.get("drone_id")
                    wall_time = event.get("wall_time_s")
                    if drone_key is not None and numeric(wall_time):
                        initialize_wall_by_drone[str(drone_key)] = wall_time
                if name == "trajectory_end":
                    summary["trajectory_end_reasons"][event.get("reason", "unknown")] += 1
                    trajectory_ends.append(event)
                    if isinstance(event.get("task_center_id"), int) and event.get("task_center_id") >= 0:
                        summary["task_identity"]["trajectory_end_with_stable_center_id"] += 1
                if name == "trajectory_start":
                    plan_seq = event.get("plan_seq")
                    drone_key = event.get("drone_id")
                    if isinstance(plan_seq, int) and drone_key is not None:
                        trajectory_starts[(drone_key, plan_seq)] = event
                    if isinstance(event.get("task_center_id"), int) and event.get("task_center_id") >= 0:
                        summary["task_identity"]["trajectory_start_with_stable_center_id"] += 1
                if name == "target_selection":
                    expected_plan_seq = event.get("expected_plan_seq")
                    drone_key = event.get("drone_id")
                    source = event.get("target_source", "unknown")
                    if isinstance(expected_plan_seq, int) and drone_key is not None:
                        target_selections[(drone_key, expected_plan_seq)] = source
                    summary["astar"]["target_source_counts"][source] += 1
                    identity = summary["task_identity"]
                    identity["target_selection_count"] += 1
                    identity["target_match_kind_counts"][event.get("task_match_kind", "missing")] += 1
                    if isinstance(event.get("task_center_id"), int) and event.get("task_center_id") >= 0:
                        identity["target_selection_with_stable_center_id"] += 1
                    if isinstance(event.get("task_grid_id"), int) and event.get("task_grid_id") >= 0:
                        identity["target_selection_with_grid_id"] += 1
                if name == "allocation_task":
                    summary["task_identity"]["allocation_task_event_count"] += 1
                if name == "allocation_candidate_set":
                    summary["allocation"]["candidate_set_count"] += 1
                if name == "allocation_result":
                    if event.get("success"):
                        summary["allocation"]["successful_result_count"] += 1
                    else:
                        summary["allocation"]["failed_result_count"] += 1
                if name == "allocation_assignment":
                    summary["allocation"]["assignment_event_count"] += 1
                    nominal_cost = event.get("nominal_tour_cost")
                    if numeric(nominal_cost):
                        nominal_tour_costs.append(nominal_cost)
                if name == "explore_result":
                    if event.get("success"):
                        summary["planning"]["explore_success_count"] += 1
                    else:
                        summary["planning"]["explore_failure_count"] += 1
                        summary["planning"]["explore_failure_reasons"][
                            event.get("reason", "unknown")
                        ] += 1
                if name == "traj_result":
                    if event.get("success"):
                        summary["planning"]["trajectory_plan_success_count"] += 1
                    else:
                        summary["planning"]["trajectory_plan_failure_count"] += 1
                        summary["planning"]["trajectory_plan_failure_reasons"][
                            event.get("reason", "unknown")
                        ] += 1
                        duration = event.get("duration_wall_s")
                        if numeric(duration):
                            trajectory_plan_failure_durations.append(duration)
                if name == "astar_search_diagnostic":
                    astar = summary["astar"]
                    astar["failure_diagnostic_count"] += 1
                    astar["termination_counts"][event.get("termination", "unknown")] += 1
                    for field in (
                        "elapsed_ros_s",
                        "elapsed_wall_s",
                        "expanded_nodes",
                        "discovered_nodes",
                        "candidate_neighbors",
                    ):
                        value = event.get(field)
                        if numeric(value):
                            astar[field].append(value)
                    capacity = event.get("node_pool_capacity")
                    used = event.get("node_pool_used")
                    if numeric(capacity) and numeric(used) and capacity > 0:
                        astar["node_pool_utilization"].append(used / capacity)
                    raw_state = (
                        event.get("start_occupancy"),
                        event.get("end_occupancy"),
                        event.get("start_inflated_occupancy"),
                        event.get("end_inflated_occupancy"),
                        event.get("start_in_box"),
                        event.get("end_in_box"),
                    )
                    astar["start_goal_raw_state_counts"][str(raw_state)] += 1
                    source = target_selections.get(
                        (event.get("drone_id"), event.get("plan_seq")), "unmatched_or_legacy"
                    )
                    astar["failure_target_source_counts"][source] += 1
                    coordinates = (
                        event.get("start_x"),
                        event.get("start_y"),
                        event.get("start_z"),
                        event.get("goal_x"),
                        event.get("goal_y"),
                        event.get("goal_z"),
                    )
                    if all(numeric(value) for value in coordinates):
                        rounded = tuple(round(value, 3) for value in coordinates)
                        astar["goal_coordinate_counts"][str(rounded)] += 1
                if name == "lkh_result":
                    bucket = summary["lkh"][event.get("problem", "unknown")]
                    if event.get("success"):
                        bucket["success"] += 1
                    else:
                        bucket["failure"] += 1
                    duration = event.get("duration_wall_s")
                    if isinstance(duration, (int, float)) and math.isfinite(duration):
                        bucket["durations_wall_s"].append(duration)
        summary["per_drone"][str(drone_id) if drone_id is not None else path.name] = dict(drone_events)

    summary["event_counts"] = dict(summary["event_counts"])
    summary["finish_drone_ids"] = sorted({item for item in summary["finish_drone_ids"] if item is not None})
    finish_times = list(summary["finish_wall_time_by_drone"].values())
    init_times = list(initialize_wall_by_drone.values())
    if finish_times and init_times:
        summary["local_finish_makespan_wall_s"] = max(finish_times) - min(init_times)
    summary["trajectory_end_reasons"] = dict(summary["trajectory_end_reasons"])
    summary["task_identity"]["target_match_kind_counts"] = dict(
        summary["task_identity"]["target_match_kind_counts"]
    )
    summary["allocation_seq_audit"]["positive_local_seq_values"] = sorted(allocation_seq_values)
    ratios = []
    matched_keys = set()
    unmatched_ends = 0
    for event in trajectory_ends:
        plan_seq = event.get("plan_seq")
        drone_key = event.get("drone_id")
        key = (drone_key, plan_seq)
        start = trajectory_starts.get(key)
        if start is None:
            unmatched_ends += 1
            continue
        matched_keys.add(key)
        planned_duration = event.get("planned_duration_s", start.get("planned_duration_s"))
        executed_duration = event.get("executed_s")
        if numeric(planned_duration) and numeric(executed_duration) and planned_duration > 0:
            ratios.append(executed_duration / planned_duration)
    pairing = summary["trajectory_pairing"]
    pairing["matched_segments"] = len(matched_keys)
    pairing["unmatched_start_count"] = len(set(trajectory_starts) - matched_keys)
    pairing["unmatched_end_count"] = unmatched_ends
    pairing["duration_ratio_count"] = len(ratios)
    pairing["execution_to_planned_duration_p50"] = percentile(ratios, 0.50)
    pairing["execution_to_planned_duration_p95"] = percentile(ratios, 0.95)
    pairing["execution_to_planned_duration_max"] = max(ratios) if ratios else None
    summary["allocation"]["nominal_tour_cost_count"] = len(nominal_tour_costs)
    summary["allocation"]["nominal_tour_cost_p50"] = percentile(nominal_tour_costs, 0.50)
    summary["allocation"]["nominal_tour_cost_p95"] = percentile(nominal_tour_costs, 0.95)
    summary["planning"]["explore_failure_reasons"] = dict(
        summary["planning"]["explore_failure_reasons"]
    )
    summary["planning"]["trajectory_plan_failure_reasons"] = dict(
        summary["planning"]["trajectory_plan_failure_reasons"]
    )
    summary["planning"]["trajectory_plan_failure_duration_count"] = len(
        trajectory_plan_failure_durations
    )
    summary["planning"]["trajectory_plan_failure_duration_p50_wall_s"] = percentile(
        trajectory_plan_failure_durations, 0.50
    )
    summary["planning"]["trajectory_plan_failure_duration_p95_wall_s"] = percentile(
        trajectory_plan_failure_durations, 0.95
    )
    astar = summary["astar"]
    astar["termination_counts"] = dict(astar["termination_counts"])
    astar["start_goal_raw_state_counts"] = dict(astar["start_goal_raw_state_counts"])
    astar["goal_coordinate_counts"] = dict(astar["goal_coordinate_counts"])
    astar["target_source_counts"] = dict(astar["target_source_counts"])
    astar["failure_target_source_counts"] = dict(astar["failure_target_source_counts"])
    for field in (
        "elapsed_ros_s",
        "elapsed_wall_s",
        "expanded_nodes",
        "discovered_nodes",
        "candidate_neighbors",
        "node_pool_utilization",
    ):
        values = astar.pop(field)
        astar[f"{field}_count"] = len(values)
        astar[f"{field}_p50"] = percentile(values, 0.50)
        astar[f"{field}_p95"] = percentile(values, 0.95)
        astar[f"{field}_max"] = max(values) if values else None
    lkh_summary = {}
    for problem, bucket in summary["lkh"].items():
        durations = bucket.pop("durations_wall_s")
        bucket["duration_count"] = len(durations)
        bucket["duration_p50_wall_s"] = percentile(durations, 0.50)
        bucket["duration_p95_wall_s"] = percentile(durations, 0.95)
        bucket["duration_max_wall_s"] = max(durations) if durations else None
        lkh_summary[problem] = bucket
    summary["lkh"] = lkh_summary
    summary["all_expected_finished_by_local_rule"] = len(summary["finish_drone_ids"]) == len(files)

    output = args.out or args.run_dir / "telemetry_summary.json"
    output.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0 if not summary["json_parse_errors"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
