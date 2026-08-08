#!/usr/bin/env python3
"""Aggregate a three-method formal batch: per-method metrics + paired stats.

Usage:
  aggregate_formal_batch.py <batch_root_dir> [--csv]
  batch_root_dir = .../formal_three_method/<scene>/uav_<n>/comm_5p0m/duration_180s/batch_<id>
"""
import json
import math
import random
import sys
from pathlib import Path


def load_summary(run_dir: Path):
    p = run_dir / "telemetry_summary.json"
    if not p.is_file():
        return None
    try:
        d = json.loads(p.read_text(encoding="utf-8"))
    except Exception:
        return {"parse_error": True}
    return d


def stats_key(d, method, run_dir=None):
    if d is None:
        return {"method": method, "missing": True}
    if d.get("parse_error"):
        return {"method": method, "parse_error": True}
    ec = d.get("event_counts", {})
    astar = d.get("astar", {})
    lkh = d.get("lkh", {})
    lkh_fail = sum(
        lkh.get(k, {}).get("failure", 0) or 0 for k in ("ACVRP", "ATSP-frontier", "ATSP-grid")
    ) if isinstance(lkh, dict) else -1
    me = {}
    if run_dir is not None:
        mf = Path(run_dir) / "method_events.jsonl"
        if mf.is_file():
            for line in mf.read_text(encoding="utf-8").splitlines():
                if not line.strip():
                    continue
                try:
                    ev = json.loads(line)
                except Exception:
                    continue
                if isinstance(ev, dict) and ev.get("event"):
                    me[ev["event"]] = me.get(ev["event"], 0) + 1
    return {
        "method": method,
        "run": d.get("run_dir", "?"),
        "status": d.get("status", "?"),
        "finish": len(d.get("finish_drone_ids", []) or []),
        "makespan": d.get("local_finish_makespan_wall_s"),
        "astar_fail": astar.get("failure_diagnostic_count", 0) or 0,
        "traj_fail": ec.get("trajectory_failure", 0) or 0,
        "lkh_fail": lkh_fail,
        "json_err": d.get("json_parse_errors", 0) or 0,
        "reach_adjust": me.get("reach_cost_adjustment", 0) or 0,
        "reach_alloc_adjust": me.get("reach_allocation_cost_adjustment", 0) or 0,
        "svr_gate": me.get("svr_reallocation_gate", 0) or 0,
        "svr_reuse": me.get("svr_reuse", 0) or 0,
        "steer_view_skip": me.get("goal_view_skip", 0) or 0,
        "steer_all_cooled": me.get("goal_view_all_cooled", 0) or 0,
        "steer_switch": me.get("goal_switch", 0) or 0,
        "steer_margin_rej": me.get("steer_switch_margin_rejected", 0) or 0,
        "steer_hold": (me.get("steer_all_cooled_hold", 0) or 0) + (me.get("steer_all_cooled_wait", 0) or 0),
        "astar_diag": ec.get("astar_search_diagnostic", 0) or 0,
        "all_cooled_fallback": me.get("prct_all_cooled_fallback", 0) or 0,
        "traj_request": ec.get("traj_request", 0) or 0,
        "fsm_transition": ec.get("fsm_transition", 0) or 0,
    }


def bootstrap_ci(diffs, n=10000, seed=7):
    rng = random.Random(seed)
    means = []
    m = len(diffs)
    if m == 0:
        return (float("nan"), float("nan"))
    for _ in range(n):
        s = sum(rng.choice(diffs) for _ in range(m)) / m
        means.append(s)
    means.sort()
    return (means[int(0.025 * n)], means[int(0.975 * n)])


def wilcoxon(a, b, n_perm=20000, seed=11):
    """Two-sided signed-rank p via Monte-Carlo sign permutation (tie-safe).

    The normal approximation is unreliable at n<=20 with heavy ties, so we
    use the exact conditional distribution estimated by sign flipping.
    """
    diffs = [x - y for x, y in zip(a, b) if x != y]
    n_ = len(diffs)
    if n_ == 0:
        return 1.0
    ranks = {}
    sorted_diffs = sorted(diffs)
    i = 0
    while i < n_:
        j = i
        while j + 1 < n_ and sorted_diffs[j + 1] == sorted_diffs[i]:
            j += 1
        avg = (i + 1 + j + 1) / 2.0
        for k in range(i, j + 1):
            ranks[sorted_diffs[k]] = avg
        i = j + 1
    w_obs = sum(ranks[v] for v in diffs if v > 0)
    rng = random.Random(seed)
    abs_ranks = [ranks[v] for v in diffs]
    # Monte-Carlo null: random sign flips
    count = 0
    for _ in range(n_perm):
        w = 0.0
        for r in abs_ranks:
            if rng.random() < 0.5:
                w += r
        if abs(w - n_ * (n_ + 1) / 4.0) >= abs(w_obs - n_ * (n_ + 1) / 4.0):
            count += 1
    p = (count + 1) / (n_perm + 1)
    return p


def mann_whitney_u(a, b, n_perm=20000, seed=13):
    """Two-sided unpaired Mann-Whitney U p via Monte-Carlo permutation.

    The runs are repeated instances, not matched trials, so an unpaired test
    is the more conservative default; the paired test is reported as well.
    """
    import statistics

    def u_stat(x, y):
        x = sorted(x)
        y = sorted(y)
        i = j = 0
        u = 0.0
        while i < len(x) and j < len(y):
            if x[i] < y[j]:
                u += len(y) - j
                i += 1
            else:
                j += 1
        return u

    n_a, n_b = len(a), len(b)
    if n_a == 0 or n_b == 0:
        return 1.0
    obs = u_stat(a, b)
    pooled = list(a) + list(b)
    rng = random.Random(seed)
    ge = 0
    le = 0
    for _ in range(n_perm):
        rng.shuffle(pooled)
        uu = u_stat(pooled[:n_a], pooled[n_a:])
        if uu >= obs:
            ge += 1
        if uu <= obs:
            le += 1
    # two-sided: 2 * min(one-sided p values), with continuity
    p = 2.0 * min(ge, le) / (n_perm + 1)
    return min(1.0, p)


def main():
    batch_root = Path(sys.argv[1])
    methods = ["b0", "b1", "reach", "svr", "steer"]
    drone_num = 4
    import re
    m = re.search(r"uav_(\d+)", str(batch_root))
    if m:
        drone_num = int(m.group(1))
    runs = {}
    status_file = batch_root / "status.tsv"
    if status_file.is_file():
        for line in status_file.read_text(encoding="utf-8").splitlines()[1:]:
            parts = line.split("\t")
            if len(parts) < 8:
                continue
            method = parts[0]
            run_dir = Path(parts[7])
            if not run_dir.is_dir():
                continue
            d = load_summary(run_dir)
            runs.setdefault(method, []).append(stats_key(d, method, run_dir))
    else:
        # fallback: glob sibling run dirs (pre-status layout)
        for run_dir in sorted(batch_root.parent.parent.glob("three_*")):
            if not run_dir.is_dir():
                continue
            name = run_dir.name
            method = None
            for m in methods:
                if f"_{m}_" in name:
                    method = m
                    break
            if method is None:
                continue
            d = load_summary(run_dir)
            runs.setdefault(method, []).append(stats_key(d, method, run_dir))

    # Unfinished-but-valid runs count as truncated makespan (180s window).
    for m, rows in runs.items():
        for r in rows:
            if r.get("makespan") is None and not r.get("missing"):
                r["makespan"] = 180.0
            # Infra-quality flag: too little activity to be a valid sample
            # (e.g., WSLg render crash -> instant FINISH or no exploration).
            r["infra_suspect"] = (
                r.get("astar_diag", 0) == 0
                and r.get("traj_request", 0) < 50
                and r.get("finish", 0) == 0
            )

    # Valid samples only; infra rate reported separately.
    valid = {}
    for m, rows in runs.items():
        valid[m] = [r for r in rows
                    if not r.get("infra_suspect") and not r.get("missing")
                    and not r.get("parse_error")]
    infra_counts = {m: sum(1 for r in rows if r.get("infra_suspect"))
                    for m, rows in runs.items()}
    print(f"infra_suspect (render crash, excluded): {infra_counts}")

    print(f"batch: {batch_root}")
    print(f"{'method':8s} {'n':>3s} {'finish_frac':>12s} {'makespan_med':>13s} "
          f"{'makespan_mean':>13s} {'astar_fail_med':>13s} {'astar_fail_sum':>14s} "
          f"{'traj_fail_sum':>13s} {'json_err':>8s} {'reach_adj':>9s} {'svr_gate':>9s} "
          f"{'svr_reuse':>9s} {'steer_skip':>10s} {'steer_allcool':>13s} {'steer_switch':>12s} "
          f"{'infra_suspect':>13s}")
    medians = {}
    makespan_lists = {}
    for m in methods:
        rows = valid.get(m, [])
        ms = sorted(r["makespan"] for r in rows
                    if r.get("makespan") is not None)
        af = sorted(r["astar_fail"] for r in rows)
        med = ms[len(ms) // 2] if ms else float("nan")
        mean = sum(ms) / len(ms) if ms else float("nan")
        medians[m] = med
        makespan_lists[m] = ms
        n_finish = sum(1 for r in rows if r.get("finish", 0) == drone_num)
        infra = infra_counts.get(m, 0)
        print(
            f"{m:8s} {len(rows):3d} {n_finish}/{len(rows)}"
            f"{n_finish / len(rows):>11.2f} {med:13.2f} {mean:13.2f} "
            f"{af[len(af) // 2]:13d} {sum(r['astar_fail'] for r in rows):14d} "
            f"{sum(r['traj_fail'] for r in rows):13d} "
            f"{sum(r['json_err'] for r in rows):8d} "
            f"{sum(r['reach_adjust'] for r in rows):9d} "
            f"{sum(r['svr_gate'] for r in rows):9d} "
            f"{sum(r['svr_reuse'] for r in rows):9d} "
            f"{sum(r['steer_view_skip'] for r in rows):10d} "
            f"{sum(r['steer_all_cooled'] for r in rows):13d} "
            f"{sum(r['steer_switch'] for r in rows):12d} "
            f"{infra:13d}"
        )

    def paired_block(title, ref):
        print(f"\npaired: {title} (makespan, truncated 180s if missing):")
        ref_rows = valid.get(ref, [])
        for m in methods:
            if m == ref:
                continue
            rows = valid.get(m, [])
            n = min(len(rows), len(ref_rows))
            if n == 0:
                continue
            diffs = [rows[i]["makespan"] - ref_rows[i]["makespan"] for i in range(n)]
            lo, hi = bootstrap_ci(diffs)
            p_pair = wilcoxon([r["makespan"] for r in rows[:n]],
                              [r["makespan"] for r in ref_rows[:n]])
            p_unp = mann_whitney_u([r["makespan"] for r in rows[:n]],
                                   [r["makespan"] for r in ref_rows[:n]])
            wins = sum(1 for x in diffs if x < 0)
            losses = sum(1 for x in diffs if x > 0)
            print(f"{m:8s} vs {ref}: n={n:2d} wins={wins:2d} losses={losses:2d} "
                  f"median_diff={sorted(diffs)[n // 2]:+7.2f}s "
                  f"bootstrap95=[{lo:+7.2f},{hi:+7.2f}] "
                  f"wilcoxon_perm_p={p_pair:.4f} mwu_perm_p={p_unp:.4f}")

    paired_block("method vs B1", "b1")
    paired_block("method vs B0", "b0")


if __name__ == "__main__":
    main()
