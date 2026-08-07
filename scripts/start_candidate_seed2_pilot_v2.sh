#!/usr/bin/env bash
set -euo pipefail

log=/home/c2dev/c2_explorer_reproduction/logs/three_method_seed2_pilot_v2_20260807.log
pid_file=/home/c2dev/c2_explorer_reproduction/logs/three_method_seed2_pilot_v2.pid
rm -f "$log" "$pid_file"
nohup env LKH_SEED=2 PRCT_RUN_FULL_DURATION=true bash /home/c2dev/c2_explorer_reproduction/scripts/run_three_method_batch.sh open_plan_office 3 1 180 5.0 candidate_seed2_pilot_v2 > "$log" 2>&1 &
echo $! > "$pid_file"
echo "started pid=$(cat "$pid_file")"
