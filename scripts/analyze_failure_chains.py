#!/usr/bin/env python3
"""Analyze failure chains and mechanism events for one or more run dirs.

Usage:
  analyze_failure_chains.py <run_dir> [run_dir...]
"""
import collections
import json
import sys
from pathlib import Path


def analyze(run_dir: Path):
    print(f"\n===== {run_dir} =====")
    summary_p = run_dir / "telemetry_summary.json"
    d = {}
    if summary_p.is_file():
        try:
            d = json.loads(summary_p.read_text(encoding="utf-8"))
        except Exception:
            pass
    if d:
        ec = d.get("event_counts", {})
        print(
            f"status={d.get('status')} finish={len(d.get('finish_drone_ids', []) or [])} "
            f"makespan={d.get('local_finish_makespan_wall_s')}"
        )
        print(
            f"astar_fail={d.get('astar', {}).get('failure_diagnostic_count')} "
            f"astar_diag={ec.get('astar_search_diagnostic')} "
            f"traj_fail={ec.get('trajectory_failure')}"
        )
        me = {k: v for k, v in ec.items()
              if any(s in k for s in ("reach", "svr", "steer", "prct", "goal_"))}
        if me:
            print("mechanism events:", json.dumps(me, ensure_ascii=False))

    failures = run_dir / "failures.jsonl"
    if not failures.is_file():
        print("no failures.jsonl")
        return
    chains = collections.Counter()
    by_drone = collections.Counter()
    by_reason = collections.Counter()
    total = 0
    for line in failures.read_text(encoding="utf-8").splitlines():
        try:
            f = json.loads(line)
        except Exception:
            continue
        total += 1
        drone = f.get("drone_id")
        key = (drone, f.get("frontier_id"),
               round(f.get("goal_x", 0), 1), round(f.get("goal_y", 0), 1))
        chains[key] += 1
        by_drone[drone] += 1
        by_reason[f.get("reason")] += 1
    print(f"total failures={total} by_reason={dict(by_reason)} by_drone={dict(by_drone)}")
    top = chains.most_common(10)
    print("top repeat chains (drone,frontier,gx,gy): count")
    for k, v in top:
        print(f"  {k}: {v}")
    longest = max(chains.values()) if chains else 0
    print(f"longest same-target chain={longest} "
          f"distinct_targets={len(chains)}")


def main():
    for p in sys.argv[1:]:
        analyze(Path(p))


if __name__ == "__main__":
    main()
