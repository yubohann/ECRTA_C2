#!/usr/bin/env bash
set -Eeo pipefail

# Unified three-method paired batch: B0 / B1 / REACH-C2 / SVR-C2 / STEER-C2.
# Repeated instances are audit labels, not official seeds.

source /home/c2dev/c2_explorer_reproduction/scripts/activate_reachability_retry.sh

usage() {
  echo "usage: $0 <scene> <drone_num> <count> [duration_s=180] [communication_threshold_m=5.0] [batch_id=three_method]" >&2
  exit 64
}

scene="$1"
drone_num="$2"
count="$3"
duration_s="${4:-180}"
communication_threshold="${5:-5.0}"
batch_id="${6:-three_method}"

case "$scene" in open_plan_office|cubicle_office|octa_maze) ;; *) usage ;; esac
[[ "$drone_num" =~ ^[0-9]+$ && "$drone_num" -ge 2 && "$drone_num" -le 4 ]] || usage
[[ "$count" =~ ^[1-9][0-9]*$ ]] || usage
[[ "$duration_s" =~ ^[0-9]+$ && "$duration_s" -gt 0 ]] || usage
[[ "$communication_threshold" =~ ^([0-9]+([.][0-9]+)?|inf)$ ]] || usage

runner="$ECRTA_ROOT/scripts/run_scene_pilot.sh"
run_full_duration="${PRCT_RUN_FULL_DURATION:-false}"
comm_label=$(printf '%s' "$communication_threshold" | tr '.' 'p')
batch_root="$ECRTA_LOG_ROOT/formal_three_method/$scene/uav_$drone_num/comm_${comm_label}m/duration_${duration_s}s/batch_$batch_id"
if [[ -e "$batch_root" ]]; then
  echo "refusing to overwrite existing batch: $batch_root" >&2
  exit 73
fi
mkdir -p "$batch_root"

{
  date -Is
  printf 'scene=%s\ndrone_num=%s\nduration_s=%s\ncount=%s\ncommunication_threshold_m=%s\n' \
    "$scene" "$drone_num" "$duration_s" "$count" "$communication_threshold"
  printf 'classification=repeated_instances_not_seed_indexed_trials\n'
  printf 'method_set=B0,B1,REACH-C2,SVR-C2,STEER-C2\n'
  printf 'workspace=%s\n' "$ECRTA_WORKSPACE"
  sha256sum "$runner"
  sha256sum "$ECRTA_WORKSPACE/src/swarm_exploration/exploration_manager/launch/${scene}.launch"
  sha256sum "$ECRTA_WORKSPACE/src/swarm_exploration/exploration_manager/src/c2_exploration_manager.cpp"
} > "$batch_root/batch_manifest.txt"

status_file="$batch_root/status.tsv"
printf 'method\tindex\trun_id\trun_exit\taudit_status\tfinish_count\tmakespan_s_proxy\trun_dir\n' > "$status_file"

for ((i = 1; i <= count; i++)); do
  for method in b0 b1 reach svr steer; do
    case "$method" in
      b0)
        method_mode=baseline
        suppress=false
        ;;
      b1)
        method_mode=suppress
        suppress=true
        ;;
      reach)
        method_mode=reach
        suppress=true
        ;;
      svr)
        method_mode=svr
        suppress=true
        ;;
      steer)
        method_mode=steer
        suppress=true
        ;;
    esac
    run_id="three_${batch_id}_${method}_${scene}_uav${drone_num}_run_$(printf '%03d' "$i")"
    run_dir="$ECRTA_LOG_ROOT/formal_three_method/$scene/uav_$drone_num/$run_id"
    if [[ -d "$run_dir" ]]; then
      echo "skip existing: $run_dir"
      continue
    fi
    echo "start: $run_id method=$method_mode scene=$scene uav=$drone_num"
    set +e
    PRCT_RUN_FULL_DURATION="$run_full_duration" PRCT_RUN_ROOT=formal_three_method METHOD_MODE="$method_mode" "$runner" "$scene" "$drone_num" "$duration_s" "$run_id" \
      "$communication_threshold" "0" "0" "$suppress" \
      "3" "5.0" "false" "0.25" "2.0" "2.0" "false" \
      "1.0" "0.5" "0.5" "0.5" "2.0" "2.0" "3.0" "1.0" "3" "0.3" "0.6" "30.0" "3" "120.0" \
      "5.0" "30.0" "2.0" "false" \
      "0.2" "false" "20.0"
    run_exit=$?
    set -e

    audit_status="missing"
    finish_count=""
    makespan=""
    if [[ -f "$run_dir/peer_takeover_audit.json" ]]; then
      audit_status=$(/usr/bin/python3 - "$run_dir/peer_takeover_audit.json" <<'PY'
import json, sys
from pathlib import Path
d = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
print(d.get("status", "missing"))
PY
      )
      finish_count=$(grep -o '"finish_count": [0-9]*' "$run_dir/peer_takeover_audit.json" | grep -o '[0-9]*' || true)
      makespan=$(/usr/bin/python3 - "$run_dir/peer_takeover_audit.json" <<'PY'
import json, sys
from pathlib import Path
d = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
print(d.get("makespan_s_proxy", ""))
PY
      )
    fi
    if [[ -z "$finish_count" && -f "$run_dir/telemetry_summary.json" ]]; then
      finish_count=$(/usr/bin/python3 - "$run_dir/telemetry_summary.json" <<'PY'
import json, sys
from pathlib import Path
d = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
print(d.get("finished_drone_count", d.get("finish_count", "")))
PY
      )
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$method" "$i" "$run_id" "$run_exit" "$audit_status" "$finish_count" "$makespan" "$run_dir" \
      >> "$status_file"
    echo "done: $run_id"
  done
done

echo "batch_root=$batch_root"
echo "status=$status_file"