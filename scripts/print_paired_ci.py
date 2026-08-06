#!/usr/bin/env python3
import json
from pathlib import Path

p = Path("/home/c2dev/c2_explorer_reproduction/PRCT_C2_STATS.json")
data = json.loads(p.read_text(encoding="utf-8"))
print("CONFIG_STATS")
for item in data["config_stats"]:
    print(item["scene"], item["uav_num"], item["method"], "n=" + str(item["n"]), "finish=" + str(item["finish_count"]), "rmst=" + str(round(item["rmst_s"], 2)) if item["rmst_s"] is not None else "rmst=None")
print()
print("PAIRED")
for item in data["paired_comparisons"]:
    ci = [None if x is None else round(x, 2) for x in item["bootstrap_median_diff_ci95"]]
    print(item["scene"], item["uav_num"], item["comparison"], "n=" + str(item["n_pairs"]), "wins=" + str(item["b3_wins"]), "losses=" + str(item["b3_losses"]), "ties=" + str(item["ties"]), "median=" + str(round(item["median_diff_s"], 2)), "ci95=" + str(ci), "pct=" + str(round(item["median_pct_improvement"], 2)) if item["median_pct_improvement"] is not None else "pct=None")
