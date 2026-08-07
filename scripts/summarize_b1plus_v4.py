#!/usr/bin/env python3
"""Summarize B1+ v4 paired batch results from status.tsv and telemetry_summary.json.

Usage:
  python3 summarize_b1plus_v4.py STATUS_TSV [DURATION_S] [OUTPUT_PREFIX]

The batch is not seed-indexed; repeated instance labels are audit labels only.
"""

import argparse
import csv
import json
import re
import statistics
from pathlib import Path


def read_status(path):
    rows = []
    with open(path, encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f, delimiter="	")
        for row in reader:
            rows.append(row)
    return rows


def read_summary(run_dir):
    p = Path(run_dir) / "telemetry_summary.json"
    if not p.exists():
        return {}
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def numeric(value, default=None):
    if value is None or value == "":
        return default
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def get_event_count(summary, event):
    counts = summary.get("event_counts", {})
    if not isinstance(counts, dict):
        return 0
    return int(counts.get(event, 0))


def run_metrics(row, duration_s):
    run_dir = row.get("run_dir", "")
    summary = read_summary(run_dir)
    method = row.get("method", "?")
    index = row.get("index", "?")
    drone_num = 3
    m = re.search(r"uav(d+)", row.get("run_id", ""))
    if m:
        drone_num = int(m.group(1))

    finish_count = None
    finish_ids = summary.get("finish_drone_ids")
    if isinstance(finish_ids, list):
        finish_count = len(finish_ids)
    else:
        finish_count = numeric(row.get("finish_count"), 0)

    makespan = numeric(summary.get("local_finish_makespan_wall_s"))
    if makespan is None:
        makespan = numeric(row.get("makespan_s_proxy"))

    finished = finish_count is not None and int(finish_count) >= drone_num
    if makespan is not None and not finished and makespan > duration_s:
        makespan = None

    astar = summary.get("astar", {})
    if not isinstance(astar, dict):
        astar = {}
    planning = summary.get("planning", {})
    if not isinstance(planning, dict):
        planning = {}
    lkh = summary.get("lkh", {})
    if not isinstance(lkh, dict):
        lkh = {}

    lkh_failures = 0
    for problem in ("ACVRP", "ATSP-frontier", "ATSP-grid"):
        block = lkh.get(problem, {})
        if isinstance(block, dict):
            lkh_failures += int(block.get("failure", 0))

    return {
        "method": method,
        "index": int(index) if str(index).isdigit() else index,
        "run_id": row.get("run_id", ""),
        "run_exit": row.get("run_exit", ""),
        "audit_status": row.get("audit_status", ""),
        "finish_count": finish_count,
        "drone_num": drone_num,
        "finished": bool(finished),
        "makespan_s": makespan,
        "astar_fail": int(astar.get("failure_diagnostic_count", 0)),
        "traj_fail": int(planning.get("trajectory_plan_failure_count", 0)),
        "lkh_fail": lkh_failures,
        "prct_register": get_event_count(summary, "prct_retry_suppression_register"),
        "prct_skip": get_event_count(summary, "prct_retry_suppression_skip"),
        "prct_filter": get_event_count(summary, "prct_candidate_filter"),
        "prct_release": get_event_count(summary, "prct_quarantine_release"),
        "prct_release_frontier": get_event_count(summary, "prct_quarantine_release_frontier"),
        "prct_fallback": get_event_count(summary, "prct_all_cooled_fallback"),
        "run_dir": run_dir,
    }


def quantile(values, q):
    if not values:
        return None
    s = sorted(values)
    if len(s) == 1:
        return s[0]
    pos = q * (len(s) - 1)
    lo = int(pos)
    hi = min(lo + 1, len(s) - 1)
    frac = pos - lo
    return s[lo] + (s[hi] - s[lo]) * frac


def aggregate(metrics, duration_s):
    by_method = {}
    for m in metrics:
        by_method.setdefault(m["method"], []).append(m)
    config = {}
    for method, rows in sorted(by_method.items()):
        times = [r["makespan_s"] for r in rows if r["makespan_s"] is not None]
        finished = [r for r in rows if r["finished"]]
        censored_times = [r["makespan_s"] if r["makespan_s"] is not None else duration_s for r in rows]
        config[method] = {
            "n": len(rows),
            "finish_count": len(finished),
            "finish_rate": len(finished) / len(rows) if rows else None,
            "makespan_mean_s": statistics.mean(times) if times else None,
            "makespan_median_s": statistics.median(times) if times else None,
            "makespan_std_s": statistics.stdev(times) if len(times) > 1 else None,
            "rmst_s": statistics.mean(censored_times) if censored_times else None,
            "p90_s": quantile(censored_times, 0.90),
            "astar_fail_total": sum(r["astar_fail"] for r in rows),
            "traj_fail_total": sum(r["traj_fail"] for r in rows),
            "lkh_fail_total": sum(r["lkh_fail"] for r in rows),
            "prct_register_total": sum(r["prct_register"] for r in rows),
            "prct_skip_total": sum(r["prct_skip"] for r in rows),
            "prct_filter_total": sum(r["prct_filter"] for r in rows),
            "prct_release_total": sum(r["prct_release"] for r in rows),
            "prct_release_frontier_total": sum(r["prct_release_frontier"] for r in rows),
            "prct_fallback_total": sum(r["prct_fallback"] for r in rows),
        }
    return config


def paired(metrics, method_a, method_b):
    by_index = {}
    for m in metrics:
        by_index.setdefault(m["index"], {})[m["method"]] = m
    pairs = []
    for index in sorted(by_index):
        a = by_index[index].get(method_a)
        b = by_index[index].get(method_b)
        if a is None or b is None:
            continue
        if a["makespan_s"] is None or b["makespan_s"] is None:
            continue
        pairs.append({
            "index": index,
            "a": method_a,
            "b": method_b,
            "a_makespan_s": a["makespan_s"],
            "b_makespan_s": b["makespan_s"],
            "diff_s": b["makespan_s"] - a["makespan_s"],
            "rel_pct": (b["makespan_s"] - a["makespan_s"]) / a["makespan_s"] * 100.0,
            "a_finished": a["finished"],
            "b_finished": b["finished"],
        })
    if not pairs:
        return None
    diffs = [p["diff_s"] for p in pairs]
    rels = [p["rel_pct"] for p in pairs]
    return {
        "n_pairs": len(pairs),
        "wins_b_over_a": sum(1 for p in pairs if p["diff_s"] < 0),
        "losses_b_over_a": sum(1 for p in pairs if p["diff_s"] > 0),
        "mean_diff_s": statistics.mean(diffs),
        "median_diff_s": statistics.median(diffs),
        "median_rel_pct": statistics.median(rels),
        "pairs": pairs,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("status_tsv")
    ap.add_argument("duration_s", type=float, default=180.0, nargs="?")
    ap.add_argument("output_prefix", default="PRCT_C2_B1PLUS_V4", nargs="?")
    args = ap.parse_args()

    rows = read_status(args.status_tsv)
    metrics = [run_metrics(r, args.duration_s) for r in rows]
    config = aggregate(metrics, args.duration_s)
    comparisons = {}
    for a, b in (("b1", "b1plus"), ("b0", "b1plus"), ("b0", "b1")):
        p = paired(metrics, a, b)
        if p is not None:
            comparisons[f"{b}_vs_{a}"] = p

    out = {
        "source": args.status_tsv,
        "duration_s": args.duration_s,
        "config_stats": config,
        "paired_comparisons": {k: {kk: vv for kk, vv in v.items() if kk != "pairs"} for k, v in comparisons.items()},
        "pairs": {k: v["pairs"] for k, v in comparisons.items()},
    }
    with open(args.output_prefix + ".json", "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=2)

    with open(args.output_prefix + ".csv", "w", encoding="utf-8", newline="") as f:
        fields = list(metrics[0].keys()) if metrics else []
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(metrics)

    print("CONFIG_STATS")
    for method, stats in config.items():
        print(method, json.dumps(stats, ensure_ascii=False))
    print("PAIRED")
    for key, comp in comparisons.items():
        print(key, json.dumps({k: v for k, v in comp.items() if k != "pairs"}, ensure_ascii=False))


if __name__ == "__main__":
    main()
