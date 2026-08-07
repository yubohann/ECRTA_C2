#!/usr/bin/env bash
set -eo pipefail

activation_script="${C2_PILOT_ACTIVATION_SCRIPT:-/home/c2dev/c2_explorer_reproduction/scripts/activate_reachability_retry.sh}"
export ROS_DISTRO="${ROS_DISTRO:-noetic}"
source "$activation_script"
export C2_PILOT_ACTIVATION_SCRIPT="$activation_script"
set -u

if (( $# < 3 )); then
  echo "usage: run_prct_batch.sh <open_plan_office|cubicle_office|octa_maze> <drone_num> <count> [duration_s=180] [communication_threshold_m=5.0]" >&2
  exit 64
fi

scene="$1"
drone_num="$2"
count="$3"
duration_s="${4:-180}"
communication_threshold="${5:-5.0}"
runner="$ECRTA_ROOT/scripts/run_scene_pilot.sh"

if ! [[ "$count" =~ ^[1-9][0-9]*$ ]]; then
  echo "count must be a positive integer" >&2
  exit 64
fi

for method in b0 b1 b2 b3; do
  case "$method" in
    b0)
      candidates=0
      peers=0
      suppress=false
      takeover=false
      ;;
    b1)
      candidates=0
      peers=0
      suppress=true
      takeover=false
      ;;
    b2)
      candidates=3
      peers=3
      suppress=true
      takeover=false
      ;;
    b3)
      candidates=3
      peers=3
      suppress=true
      takeover=true
      ;;
  esac

  for ((i = 1; i <= count; i++)); do
    run_id="${method}_${scene}_uav${drone_num}_run_$(printf '%03d' "$i")"
    target_dir="$ECRTA_LOG_ROOT/formal/$scene/uav_${drone_num}/${run_id}"
    if [[ -d "$target_dir" ]]; then
      echo "skip existing: $target_dir"
      continue
    fi
    echo "start: $run_id scene=$scene uav=$drone_num suppress=$suppress takeover=$takeover"
    PRCT_RUN_ROOT=formal \
      "$runner" "$scene" "$drone_num" "$duration_s" "$run_id" \
      "$communication_threshold" "$candidates" "$peers" "$suppress" \
      "3" "5.0" "$takeover" "0.25" "2.0" "2.0"
    echo "done: $run_id"
  done
done