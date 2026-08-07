#!/usr/bin/env bash
set -euo pipefail

log=/home/c2dev/c2_explorer_reproduction/logs/seed_search_open_plan_uav3_20260807.log
pid_file=/home/c2dev/c2_explorer_reproduction/logs/seed_search_open_plan_uav3.pid
rm -f "$log" "$pid_file"
nohup bash /home/c2dev/c2_explorer_reproduction/scripts/search_b0_fixed_seed.sh open_plan_office 3 180 1 10 5.0 > "$log" 2>&1 &
echo $! > "$pid_file"
echo "started pid=$(cat "$pid_file")"
