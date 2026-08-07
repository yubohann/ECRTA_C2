#!/usr/bin/env python3
"""Summarize C3 v8.1 formal paired batches (B0/B1/C3)."""

import argparse
import csv
import json
import math
import statistics
from pathlib import Path


def read_json(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return {}


def finite(v):
    return isinstance(v, (int, float)) and math.isfinite(v)


def fnum(v):
    return v if finite(v) else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--batch-root", type=Path, required=True)
    parser.add_argument("--out-csv", type=Path, required=True)
    parser.add_argument("--out-json", type=Path, required=True)
    args = parser.parse_args()

    status = args.batch_root / "status.tsv"
    rows = []
    if status.exists():
        with status.open(encoding="utf-8", newline="") as handle:
            for rec in csv.DictReader(handle, delimiter="\t"):
                run_dir = Path(rec.get("run_dir", ""))
                audit = read_json(run_dir / "peer_takeover_audit.json")
                telemetry = read_json(run_dir / "telemetry_summary.json")
                astar = telemetry.get("astar") or {}
                planning = telemetry.get("planning") or {}
                lkh = telemetry.get("lkh") or {}
                lkh_failures = 0
                for value in lkh.values():
                    if isinstance(value, dict):
                        lkh_failures += int(value.get("failure") or 0)
                rows.append({
                    "method": rec.get("method", ""),
                    "instance": rec.get("index", ""),
                    "run_id": rec.get("run_id", ""),
                    "audit_status": rec.get("audit_status", ""),
                    "run_exit": rec.get("run_exit", ""),
                    "finish_count": int(rec.get("finish_count") or 0),
                    "makespan_s_proxy": fnum(audit.get("makespan_s_proxy")),
                    "astar_failure_count": int(audit.get("astar_failure_count") or 0),
                    "astar_diagnostic_count": int(astar.get("failure_diagnostic_count") or 0),
                    "retry_suppression_register": int(audit.get("prct_retry_suppression_register_count") or 0),
                    "retry_suppression_skip": int(audit.get("prct_retry_suppression_skip_count") or 0),
                    "takeover_sent": int(audit.get("peer_takeover_goal_sent_count") or 0),
                    "takeover_executed": int(audit.get("peer_takeover_goal_executed_count") or 0),
                    "takeover_received": int(audit.get("peer_takeover_goal_received_count") or 0),
                    "handoff_complete": int(audit.get("peer_handoff_receipt_complete_count") or 0),
                    "handoff_fallback": int(audit.get("peer_handoff_fallback_count") or 0),
                    "wait_duration_total_wall_s": fnum(audit.get("wait_duration_total_wall_s")) or 0.0,
                    "trajectory_plan_failure_count": int(planning.get("trajectory_plan_failure_count") or 0),
                    "explore_failure_count": int(planning.get("explore_failure_count") or 0),
                    "lkh_failure_count": lkh_failures,
                })

    with args.out_csv.open("w", encoding="utf-8", newline="") as handle:
        if rows:
            writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(rows)

    by_method = {}
    for row in rows:
        by_method.setdefault(row["method"], []).append(row)
    by_instance = {}
    for row in rows:
        by_instance.setdefault(row["instance"], {})[row["method"]] = row

    paired = []
    for instance, methods in sorted(by_instance.items()):
        b0 = methods.get("b0") or {}
        b1 = methods.get("b1") or {}
        c3 = methods.get("c3") or {}
        def diff(a, b):
            av = (a or {}).get("makespan_s_proxy")
            bv = (b or {}).get("makespan_s_proxy")
            return None if (not finite(av) or not finite(bv)) else av - bv
        paired.append({
            "instance": instance,
            "b0_makespan": fnum((b0 or {}).get("makespan_s_proxy")),
            "b1_makespan": fnum((b1 or {}).get("makespan_s_proxy")),
            "c3_makespan": fnum((c3 or {}).get("makespan_s_proxy")),
            "c3_minus_b0": diff(c3, b0),
            "c3_minus_b1": diff(c3, b1),
            "b1_minus_b0": diff(b1, b0),
            "c3_finish": (c3 or {}).get("finish_count"),
            "b1_finish": (b1 or {}).get("finish_count"),
        })

    def median(values):
        vals = [v for v in values if finite(v)]
        return statistics.median(vals) if vals else None

    summary = {
        "batch_root": str(args.batch_root.resolve()),
        "rows": rows,
        "paired": paired,
        "paired_median_c3_minus_b0": median([p["c3_minus_b0"] for p in paired]),
        "paired_median_c3_minus_b1": median([p["c3_minus_b1"] for p in paired]),
        "paired_median_b1_minus_b0": median([p["b1_minus_b0"] for p in paired]),
        "finish_rate_by_method": {
            method: sum(r["finish_count"] for r in method_rows) / max(1, 3 * len(method_rows))
            for method, method_rows in by_method.items()
        },
        "mean_by_method": {
            method: {
                "makespan_mean": statistics.mean([r["makespan_s_proxy"] for r in method_rows if finite(r["makespan_s_proxy"])])
                if any(finite(r["makespan_s_proxy"]) for r in method_rows) else None,
                "astar_failure_mean": statistics.mean([r["astar_failure_count"] for r in method_rows]),
                "takeover_sent_mean": statistics.mean([r["takeover_sent"] for r in method_rows]),
                "wait_total_mean_s": statistics.mean([r["wait_duration_total_wall_s"] for r in method_rows]),
                "lkh_failure_mean": statistics.mean([r["lkh_failure_count"] for r in method_rows]),
            }
            for method, method_rows in by_method.items()
        },
        "note": "Intermediate audit only. Instances are repeated labels, not official seeds."
    }
    args.out_json.write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("wrote", args.out_csv)
    print("wrote", args.out_json)


if __name__ == "__main__":
    main()

