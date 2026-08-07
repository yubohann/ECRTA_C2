#!/usr/bin/env python3
"""Measure peer reachability certificate latency from recorded telemetry.

Only audits logs; it does not change any run or make performance claims.
"""

import argparse
import json
import statistics
from collections import defaultdict
from pathlib import Path


def iter_events(root):
    for path in sorted(root.rglob("telemetry_drone_*.jsonl")):
        try:
            for line in path.read_text(encoding="utf-8").splitlines():
                try:
                    yield path, json.loads(line)
                except json.JSONDecodeError:
                    continue
        except OSError:
            continue


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()

    queries = {}
    latencies = []
    for path, event in iter_events(args.root):
        if event.get("event") == "peer_local_map_reachability_query":
            drone = event.get("drone_id")
            request_id = event.get("request_id")
            if isinstance(drone, int) and isinstance(request_id, int):
                queries[(drone, request_id)] = (event, path)
        elif event.get("event") == "peer_local_map_reachability_probe":
            drone = event.get("drone_id")
            request_id = event.get("request_id")
            query_event, query_path = queries.get((drone, request_id), (None, None))
            if query_event is None:
                continue
            q = query_event.get("wall_time_s")
            p = event.get("wall_time_s")
            if not isinstance(q, (int, float)) or not isinstance(p, (int, float)):
                continue
            latency = p - q
            latencies.append((latency, event, query_event, query_path))

    if not latencies:
        raise SystemExit("no query/probe pairs found under " + str(args.root))

    values = sorted(latency for latency, *_ in latencies)
    n = len(values)
    print(f"query_probe_pairs={n}")
    print(f"min_s={values[0]:.6f}")
    print(f"p50_s={statistics.median(values):.6f}")
    print(f"p90_s={values[int(n * 0.90)]:.6f}")
    print(f"p95_s={values[int(n * 0.95)]:.6f}")
    print(f"p99_s={values[int(n * 0.99)]:.6f}")
    print(f"max_s={values[-1]:.6f}")

    by_group = defaultdict(list)
    for latency, event, query_event, query_path in latencies:
        parts = query_path.parts
        scene = parts[-4] if len(parts) >= 4 else "unknown"
        method = parts[-2] if len(parts) >= 2 else "unknown"
        by_group[(scene, method)].append(latency)
    print("group p95:")
    for key in sorted(by_group):
        vals = sorted(by_group[key])
        print(f"  {key[0]}/{key[1]} n={len(vals)} p95={vals[int(len(vals) * 0.95)]:.6f}")


if __name__ == "__main__":
    main()
