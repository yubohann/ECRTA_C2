#!/usr/bin/python3
"""Audit PRCT-C2 active peer takeover events from per-drone JSONL telemetry."""

import argparse
import json
import math
import os
from collections import Counter, defaultdict
from pathlib import Path


def finite(value):
    return isinstance(value, (int, float)) and math.isfinite(value)


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


def load_events(run_dir):
    events = []
    errors = []
    for path in sorted(Path(run_dir).glob("telemetry_drone_*.jsonl")):
        try:
            for line_number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                try:
                    events.append(json.loads(raw))
                except json.JSONDecodeError as exc:
                    errors.append(str(path) + ":" + str(line_number) + ": " + str(exc))
        except OSError as exc:
            errors.append(str(path) + ": " + str(exc))
    return events, errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-dir", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    if args.out.exists():
        parser.error("refusing to overwrite existing output: " + str(args.out))

    events, errors = load_events(args.run_dir)
    sent = [e for e in events if e.get("event") == "peer_takeover_goal_sent"]
    received = [e for e in events if e.get("event") == "peer_takeover_goal_received"]
    dropped = [e for e in events if e.get("event") == "peer_takeover_goal_dropped"]
    executed = [e for e in events if e.get("event") == "peer_takeover_goal_executed"]
    receipt_sent = [e for e in events if e.get("event") == "peer_takeover_receipt_sent"]
    receipt_received = [e for e in events if e.get("event") == "peer_takeover_receipt"]
    handoff_receipts = [e for e in events if e.get("event") == "peer_handoff_receipt"]
    handoff_receipt_complete = [
        e for e in events if e.get("event") == "peer_handoff_receipt_complete"
    ]
    handoff_fallbacks = [e for e in events if e.get("event") == "peer_handoff_fallback"]
    reachability_queries = [
        e for e in events if e.get("event") == "peer_local_map_reachability_query"
    ]
    takeover_suppressed = [e for e in events if e.get("event") == "prct_takeover_suppressed"]
    retry_suppression_register = [
        e for e in events if e.get("event") == "prct_retry_suppression_register"
    ]
    retry_suppression_skip = [
        e for e in events if e.get("event") == "prct_retry_suppression_skip"
    ]
    failure_chain_reset = [
        e for e in events if e.get("event") == "prct_failure_chain_reset"
    ]
    wait_events = [
        e
        for e in events
        if e.get("event") == "explore_result" and e.get("reason") == "peer_handoff_wait"
    ]
    takeover_targets = [
        e
        for e in events
        if e.get("event") == "target_selection" and e.get("target_source") == "peer_takeover"
    ]
    transitions = [e for e in events if e.get("event") == "fsm_transition"]
    handoff_to = [e for e in transitions if e.get("to") == "WAIT_HANDOFF"]
    handoff_away = [e for e in transitions if e.get("from") == "WAIT_HANDOFF"]
    finishes = [e for e in transitions if e.get("to") == "FINISH"]
    traj_failures = [
        e
        for e in events
        if e.get("event") == "traj_result"
        and e.get("success") is False
        and e.get("reason") == "astar_fail"
    ]

    receipt_status_counts = Counter(e.get("status", "missing") for e in receipt_sent)
    receipt_received_status_counts = Counter(
        e.get("status", "missing") for e in receipt_received
    )
    owner_handoff_receipt_status_counts = Counter(
        e.get("status", "missing") for e in handoff_receipts
    )
    handoff_receipt_complete_status_counts = Counter(
        e.get("status", "missing") for e in handoff_receipt_complete
    )
    handoff_fallback_reason_counts = Counter(
        e.get("reason", "missing") for e in handoff_fallbacks
    )

    per_drone = defaultdict(
        lambda: {
            "sent": 0,
            "received": 0,
            "dropped": 0,
            "executed": 0,
            "takeover_targets": 0,
            "receipts_sent": 0,
            "receipts_received": 0,
            "queries": 0,
            "takeover_suppressed": 0,
            "waits": 0,
            "finish": False,
            "traj_failures": 0,
        }
    )

    for e in sent:
        drone_id = e.get("drone_id")
        if isinstance(drone_id, int):
            per_drone[drone_id]["sent"] += 1
    for e in received:
        drone_id = e.get("drone_id")
        if isinstance(drone_id, int):
            per_drone[drone_id]["received"] += 1
    for e in dropped:
        drone_id = e.get("drone_id")
        if isinstance(drone_id, int):
            per_drone[drone_id]["dropped"] += 1
    for e in executed:
        drone_id = e.get("drone_id")
        if isinstance(drone_id, int):
            per_drone[drone_id]["executed"] += 1
    for e in receipt_sent:
        drone_id = e.get("drone_id")
        if isinstance(drone_id, int):
            per_drone[drone_id]["receipts_sent"] += 1
    for e in receipt_received:
        drone_id = e.get("drone_id")
        if isinstance(drone_id, int):
            per_drone[drone_id]["receipts_received"] += 1
    for e in reachability_queries:
        drone_id = e.get("drone_id")
        if isinstance(drone_id, int):
            per_drone[drone_id]["queries"] += 1
    for e in takeover_suppressed:
        drone_id = e.get("drone_id")
        if isinstance(drone_id, int):
            per_drone[drone_id]["takeover_suppressed"] += 1
    for e in takeover_targets:
        drone_id = e.get("drone_id")
        if isinstance(drone_id, int):
            per_drone[drone_id]["takeover_targets"] += 1
    for e in wait_events:
        drone_id = e.get("drone_id")
        if isinstance(drone_id, int):
            per_drone[drone_id]["waits"] += 1
    for e in finishes:
        drone_id = e.get("drone_id")
        if isinstance(drone_id, int):
            per_drone[drone_id]["finish"] = True
    for e in traj_failures:
        drone_id = e.get("drone_id")
        if isinstance(drone_id, int):
            per_drone[drone_id]["traj_failures"] += 1

    init_times = {
        e.get("drone_id"): e.get("wall_time_s")
        for e in events
        if e.get("event") == "initialize" and isinstance(e.get("drone_id"), int)
    }
    finish_times = {
        e.get("drone_id"): e.get("wall_time_s")
        for e in finishes
        if isinstance(e.get("drone_id"), int)
    }
    makespan_by_drone = {}
    for drone_id, start in init_times.items():
        end = finish_times.get(drone_id)
        if finite(start) and finite(end):
            makespan_by_drone[drone_id] = max(0.0, end - start)
    makespan_s = max(makespan_by_drone.values()) if makespan_by_drone else None

    wait_durations = []
    entry_times = sorted(e.get("wall_time_s") for e in handoff_to if finite(e.get("wall_time_s")))
    for entry_time in entry_times:
        exits = [
            e.get("wall_time_s")
            for e in handoff_away
            if finite(e.get("wall_time_s")) and e.get("wall_time_s") >= entry_time
        ]
        if exits:
            wait_durations.append(min(exits) - entry_time)

    result = {
        "schema_version": 2,
        "scope": "active peer takeover mechanism audit; not a paper-level performance claim",
        "run_dir": str(args.run_dir.resolve()),
        "status": "audit-complete" if not errors else "audit-failed",
        "errors": errors,
        "event_counts": dict(Counter(e.get("event", "missing") for e in events)),
        "peer_takeover_goal_sent_count": len(sent),
        "peer_takeover_goal_received_count": len(received),
        "peer_takeover_goal_dropped_count": len(dropped),
        "peer_takeover_goal_executed_count": len(executed),
        "peer_takeover_receipt_sent_count": len(receipt_sent),
        "peer_takeover_receipt_count": len(receipt_received),
        "peer_handoff_receipt_count": len(handoff_receipts),
        "peer_handoff_receipt_complete_count": len(handoff_receipt_complete),
        "peer_handoff_fallback_count": len(handoff_fallbacks),
        "peer_handoff_published_count": len(sent),
        "peer_local_map_reachability_query_count": len(reachability_queries),
        "prct_takeover_suppressed_count": len(takeover_suppressed),
        "prct_retry_suppression_register_count": len(retry_suppression_register),
        "prct_retry_suppression_skip_count": len(retry_suppression_skip),
        "prct_failure_chain_reset_count": len(failure_chain_reset),
        "receipt_status_counts": dict(receipt_status_counts),
        "receipt_received_status_counts": dict(receipt_received_status_counts),
        "owner_handoff_receipt_status_counts": dict(owner_handoff_receipt_status_counts),
        "handoff_receipt_complete_status_counts": dict(
            handoff_receipt_complete_status_counts
        ),
        "handoff_fallback_reason_counts": dict(handoff_fallback_reason_counts),
        "handoff_wait_events_count": len(wait_events),
        "wait_handoff_transition_count": len(handoff_to),
        "wait_handoff_exit_count": len(handoff_away),
        "wait_duration_count": len(wait_durations),
        "wait_duration_p50_wall_s": percentile(wait_durations, 0.50),
        "wait_duration_p95_wall_s": percentile(wait_durations, 0.95),
        "wait_duration_total_wall_s": sum(wait_durations) if wait_durations else 0.0,
        "peer_takeover_target_count": len(takeover_targets),
        "finish_count": len({e.get("drone_id") for e in finishes}),
        "astar_failure_count": len(traj_failures),
        "makespan_s_proxy": makespan_s,
        "makespan_by_drone_s_proxy": makespan_by_drone,
        "per_drone": {str(k): v for k, v in sorted(per_drone.items())},
    }
    args.out.write_text(
        json.dumps(result, indent=2, sort_keys=True) + os.linesep, encoding="utf-8"
    )
    return 0 if not errors else 2


if __name__ == "__main__":
    raise SystemExit(main())
