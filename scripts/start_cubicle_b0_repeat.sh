#!/usr/bin/env bash
set -eo pipefail
source /home/c2dev/c2_explorer_reproduction/scripts/activate_reachability_retry.sh
set -u
runner="$ECRTA_ROOT/scripts/run_scene_pilot.sh"
log="$ECRTA_LOG_ROOT/three_method_cubicle_b0_repeat_20260808.log"
pid_file="$ECRTA_LOG_ROOT/three_method_cubicle_b0_repeat_20260808.pid"
rm -f "$log" "$pid_file"
nohup env PRCT_RUN_FULL_DURATION=true LKH_SEED=1 PRCT_RUN_ROOT=formal_three_method METHOD_MODE=baseline \
  "$runner" cubicle_office 4 180 cubicle_b0_repeat_20260808 5.0 0 0 false 3 5.0 false 0.25 2.0 2.0 false \
  1.0 0.5 0.5 0.5 2.0 2.0 3.0 1.0 3 0.3 0.6 30.0 3 120.0 5.0 30.0 2.0 false 0.2 false 20.0 > "$log" 2>&1 &
echo $! > "$pid_file"
echo "started pid=$(cat "$pid_file")"
