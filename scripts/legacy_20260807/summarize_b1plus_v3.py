#!/usr/bin/env python3
"""Summarize a B0/B1/B1+ v3 status.tsv into paired tables."""

import json
import sys
from pathlib import Path
from statistics import mean, median


def count_telemetry_event(run_dir, event):
    count = 0
    for path in Path(run_dir).glob("telemetry_drone_*.jsonl"):
        count += path.read_text(encoding="utf-8", errors="ignore").count(
            '"event":"' + event + '"'
        )
    return count


def main():
    status_path = Path(sys.argv[1])
    rows = []
    for line in status_path.read_text(encoding="utf-8").splitlines()[1:]:
        if not line.strip():
            continue
        fields = line.split("\t")
        if len(fields) < 8:
            continue
        method, index, run_id, run_exit, audit_status, finish_count, makespan, run_dir = fields[:8]
        d = {}
        audit_path = Path(run_dir) / "peer_takeover_audit.json"
        if audit_path.exists():
            d = json.loads(audit_path.read_text(encoding="utf-8"))
        rows.append({
            "method": method,
            "index": int(index),
            "makespan": float(makespan) if makespan else None,
            "finish": d.get("finish_count"),
            "astar_failures": d.get("astar_failure_count", 0),
            "register": d.get("prct_retry_suppression_register_count", 0),
            "skip": d.get("prct_retry_suppression_skip_count", 0),
            "reset": d.get("prct_failure_chain_reset_count", 0),
            "quarantine_register": count_telemetry_event(run_dir, "prct_retry_suppression_register"),
            "quarantine_skip": count_telemetry_event(run_dir, "prct_retry_suppression_skip"),
            "all_cooled_fallback": count_telemetry_event(run_dir, "prct_all_cooled_fallback"),
            "run_dir": run_dir,
        })

    rows.sort(key=lambda r: (r["index"], r["method"]))
    print("idx\tmethod\tmakespan\tfinish\tA*fail\tregister\tskip\tquarantine_reg\tquarantine_skip\tall_cooled_fallback")
    for r in rows:
        print("{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}".format(
            r["index"], r["method"], r["makespan"], r["finish"],
            r["astar_failures"], r["register"], r["skip"],
            r["quarantine_register"], r["quarantine_skip"],
            r["all_cooled_fallback"]))

    pairs = {}
    for r in rows:
        pairs.setdefault(r["index"], {})[r["method"]] = r

    print()
    print("instance\tB0\tB1\tB1+\tB1plus-B1\tB1plus-B0")
    diffs_b1 = []
    diffs_b0 = []
    for idx in sorted(pairs):
        p = pairs[idx]
        b0 = p.get("b0", {}).get("makespan")
        b1 = p.get("b1", {}).get("makespan")
        b1p = p.get("b1plus", {}).get("makespan")
        if b0 is not None and b1 is not None and b1p is not None:
            diffs_b1.append(b1p - b1)
            diffs_b0.append(b1p - b0)
        print("{}\t{}\t{}\t{}\t{}\t{}".format(
            idx,
            "" if b0 is None else round(b0, 2),
            "" if b1 is None else round(b1, 2),
            "" if b1p is None else round(b1p, 2),
            "" if b1p is None or b1 is None else round(b1p - b1, 2),
            "" if b1p is None or b0 is None else round(b1p - b0, 2)))

    print()
    if diffs_b1:
        print("paired B1plus-B1: mean={:.2f} median={:.2f} n={}".format(
            mean(diffs_b1), median(diffs_b1), len(diffs_b1)))
    if diffs_b0:
        print("paired B1plus-B0: mean={:.2f} median={:.2f} n={}".format(
            mean(diffs_b0), median(diffs_b0), len(diffs_b0)))


if __name__ == "__main__":
    main()
