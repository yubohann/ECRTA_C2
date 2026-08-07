#!/usr/bin/env bash
set -Eeo pipefail

# B1+ paired pressure batch: B0 / B1 / B1+ v5 under identical scene/drone/duration.
# Repeated instances are audit labels, not official seeds.

source /home/c2dev/c2_explorer_reproduction/scripts/activate_reachability_retry.sh

usage() {
  echo "usage: $0 <scene> <drone_num> <count> [duration_s=180] [communication_threshold_m=5.0] [batch_id=b1plus] [prct_local_evidence_radius_m=0.2] [prct_evict_on_first_failure=true] [prct_eviction_max_extra_cost=20.0]" >&2
  exit 64
}

scene="$1"
drone_num="$2"
count="$3"
duration_s="${4:-180}"
communication_threshold="${5:-5.0}"
batch_id="${6:-b1plus}"
prct_local_evidence_radius_m="${7:-0.2}"
prct_evict_on_first_failure="${8:-true}"
prct_eviction_max_extra_cost="${9:-20.0}"

case "$scene" in open_plan_office|cubicle_office|octa_maze) ;; *) usage ;; esac
[[ "$drone_num" =~ ^[0-9]+$ && "$drone_num" -ge 2 && "$drone_num" -le 4 ]] || usage
[[ "$count" =~ ^[1-9][0-9]*$ ]] || usage
[[ "$duration_s" =~ ^[0-9]+$ && "$duration_s" -gt 0 ]] || usage
[[ "$communication_threshold" =~ ^([0-9]+([.][0-9]+)?|inf)$ ]] || usage
[[ "$prct_local_evidence_radius_m" =~ ^[0-9]+([.][0-9]+)?$ ]] || usage
if [[ "$prct_evict_on_first_failure" != "true" && "$prct_evict_on_first_failure" != "false" ]]; then
  echo "prct_evict_on_first_failure must be true or false" >&2
  usage
fi
if ! [[ "$prct_eviction_max_extra_cost" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
  echo "prct_eviction_max_extra_cost must be a non-negative number" >&2
  usage
fi

runner="$ECRTA_ROOT/scripts/run_scene_pilot.sh"
comm_label=$(printf '%s' "$communication_threshold" | tr '.' 'p')
batch_root="$ECRTA_LOG_ROOT/formal_b1plus/$scene/uav_$drone_num/comm_${comm_label}m/duration_${duration_s}s/batch_$batch_id"
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
  printf 'method_set=B0,B1,B1+ v5 (first-failure evidence lock plus marginal replacement gate)\n'
  printf 'prct_evict_on_first_failure=%s\nprct_eviction_max_extra_cost=%s\n' \
    "$prct_evict_on_first_failure" "$prct_eviction_max_extra_cost"
  printf 'workspace=%s\n' "$ECRTA_WORKSPACE"
  sha256sum "$runner"
  sha256sum "$ECRTA_WORKSPACE/src/swarm_exploration/exploration_manager/launch/${scene}.launch"
  sha256sum "$ECRTA_WORKSPACE/src/swarm_exploration/exploration_manager/src/c2_exploration_manager.cpp"
} > "$batch_root/batch_manifest.txt"

status_file="$batch_root/status.tsv"
printf 'method\tindex\trun_id\trun_exit\taudit_status\tfinish_count\tmakespan_s_proxy\trun_dir\n' > "$status_file"

for ((i = 1; i <= count; i++)); do
  for method in b0 b1 b1plus; do
    case "$method" in
      b0)
        candidates=0; peers=0; suppress=false; takeover=false; c3=false
        ;;
      b1)
        candidates=0; peers=0; suppress=true; takeover=false; c3=false; backoff_enabled=false
        ;;
      b1plus)
        candidates=0; peers=0; suppress=true; takeover=false; c3=false; backoff_enabled=true
        ;;
    esac
    backoff_initial=5.0
    backoff_max=30.0
    backoff_factor=2.0
    if [[ "$method" == "b0" ]]; then backoff_enabled=false; fi
    evict_on_first=false
    if [[ "$method" == "b1plus" ]]; then evict_on_first="$prct_evict_on_first_failure"; fi

    run_id="b1plus_${batch_id}_${method}_${scene}_uav${drone_num}_run_$(printf '%03d' "$i")"
    run_dir="$ECRTA_LOG_ROOT/formal_b1plus/$scene/uav_$drone_num/$run_id"
    if [[ -d "$run_dir" ]]; then
      echo "skip existing: $run_dir"
      continue
    fi
    echo "start: $run_id method=$method scene=$scene uav=$drone_num backoff=5/30/2 enabled=$backoff_enabled evict_first=$evict_on_first"
    set +e
    PRCT_RUN_ROOT=formal_b1plus "$runner" "$scene" "$drone_num" "$duration_s" "$run_id" \
      "$communication_threshold" "$candidates" "$peers" "$suppress" \
      "3" "5.0" "$takeover" "0.25" "2.0" "2.0" "$c3" \
      "1.0" "0.5" "0.5" "0.5" "2.0" "2.0" "3.0" "1.0" "3" "0.3" "0.6" "30.0" "3" "120.0" \
      "$backoff_initial" "$backoff_max" "$backoff_factor" "$backoff_enabled" \
      "$prct_local_evidence_radius_m" "$evict_on_first" "$prct_eviction_max_extra_cost"
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
