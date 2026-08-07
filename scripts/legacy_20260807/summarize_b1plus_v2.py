#!/usr/bin/env python3
import json
import sys
from pathlib import Path
from statistics import mean, median

status_path = Path(sys.argv[1])
rows = []
for line in status_path.read_text(encoding="utf-8").splitlines()[1:]:
    if not line.strip():
        continue
    fields = line.split("	")
    if len(fields) < 8:
        continue
    method, index, run_id, run_exit, audit_status, finish_count, makespan, run_dir = fields[:8]
    audit_path = Path(run_dir) / "peer_takeover_audit.json"
    d = {}
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
        "run_dir": run_dir,
    })

rows.sort(key=lambda r: (r["index"], r["method"]))
print("idx	method	makespan	finish	A*fail	register	skip	reset")
for r in rows:
    print("{}	{}	{}	{}	{}	{}	{}	{}".format(
        r["index"], r["method"], r["makespan"], r["finish"],
        r["astar_failures"], r["register"], r["skip"], r["reset"]))

pairs = {}
for r in rows:
    pairs.setdefault(r["index"], {})[r["method"]] = r

print()
print("instance	B0	B1	B1+	B1plus-B1	B1plus-B0")
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
    print("{}	{}	{}	{}	{}	{}".format(
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

