#!/usr/bin/env bash
set -euo pipefail

log=/home/c2dev/c2_explorer_reproduction/logs/three_method_seed2_pilot_v3b_20260807.log
pid_file=/home/c2dev/c2_explorer_reproduction/logs/three_method_seed2_pilot_v3b.pid
rm -f "$log" "$pid_file"
nohup env LKH_SEED=2 PRCT_RUN_FULL_DURATION=true SVR_REUSE_MATCH_RADIUS_M=5.0 REACH_CENTER_MATCH_RADIUS_M=5.0 \
  bash /home/c2dev/c2_explorer_reproduction/scripts/run_three_method_batch.sh open_plan_office 3 1 180 5.0 candidate_seed2_pilot_v3b > "$log" 2>&1 &
echo $! > "$pid_file"
echo "started pid=$(cat "$pid_file")"
