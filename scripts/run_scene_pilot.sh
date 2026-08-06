#!/usr/bin/env bash
set -eo pipefail
activation_script="${C2_PILOT_ACTIVATION_SCRIPT:-/home/c2dev/c2_explorer_reproduction/scripts/activate_reachability_retry.sh}"
if [[ ! -f "$activation_script" ]]; then
  echo "missing activation script: $activation_script" >&2
  exit 66
fi
source "$activation_script"
set -u

if (( $# < 1 )); then
  echo "usage: run_scene_pilot.sh <open_plan_office|cubicle_office|octa_maze> [drone_num] [duration_s] [run_id] [communication_threshold_m] [reachability_shadow_max_candidates] [reachability_peer_shadow_max_peers] [prct_enable_retry_suppression] [prct_repeat_threshold] [prct_cooldown_s] [prct_enable_peer_takeover] [prct_peer_cert_wait_s] [prct_peer_handoff_timeout_s] [prct_peer_state_max_age_s] [c3_enable_marginal_gate] [c3_benefit_margin_s] [c3_trust_threshold] [c3_load_weight] [c3_handoff_overhead_s] [c3_trust_penalty_s] [c3_nominal_speed_m_s] [c3_owner_fallback_penalty_s] [c3_owner_stuck_alpha] [c3_min_repeat_count] [c3_owner_repeat_cost_s] [c3_peer_cert_grace_s] [c3_takeover_cooldown_s] [c3_max_takeover_attempts]" >&2
  exit 64
fi

scene="$1"
drone_num="${2:-4}"
duration_s="${3:-45}"
run_id="${4:-pilot_$(date -u +%Y%m%dT%H%M%SZ)}"
communication_threshold="${5:-5.0}"
reachability_shadow_max_candidates="${6:-0}"
reachability_peer_shadow_max_peers="${7:-0}"
prct_enable_retry_suppression="${8:-false}"
prct_repeat_threshold="${9:-3}"
prct_cooldown_s="${10:-5.0}"
prct_enable_peer_takeover="${11:-false}"
prct_peer_cert_wait_s="${12:-0.25}"
prct_peer_handoff_timeout_s="${13:-2.0}"
prct_peer_state_max_age_s="${14:-2.0}"
c3_enable_marginal_gate="${15:-true}"
c3_benefit_margin_s="${16:-1.0}"
c3_trust_threshold="${17:-0.5}"
c3_load_weight="${18:-0.5}"
c3_handoff_overhead_s="${19:-0.5}"
c3_trust_penalty_s="${20:-2.0}"
c3_nominal_speed_m_s="${21:-2.0}"
c3_owner_fallback_penalty_s="${22:-3.0}"
c3_owner_stuck_alpha="${23:-1.0}"
c3_min_repeat_count="${24:-3}"
c3_owner_repeat_cost_s="${25:-0.3}"
c3_peer_cert_grace_s="${26:-0.6}"
c3_takeover_cooldown_s="${27:-30.0}"
c3_max_takeover_attempts="${28:-3}"
c3_takeover_completed_cooldown_s="${29:-120.0}"

if ! [[ "$reachability_shadow_max_candidates" =~ ^[0-9]+$ ]]; then
  echo "reachability_shadow_max_candidates must be a non-negative integer" >&2
  exit 64
fi
if ! [[ "$reachability_peer_shadow_max_peers" =~ ^[0-9]+$ ]]; then
  echo "reachability_peer_shadow_max_peers must be a non-negative integer" >&2
  exit 64
fi
if [[ "$prct_enable_retry_suppression" != "true" && "$prct_enable_retry_suppression" != "false" ]]; then
  echo "prct_enable_retry_suppression must be true or false" >&2
  exit 64
fi
if ! [[ "$prct_repeat_threshold" =~ ^[1-9][0-9]*$ ]]; then
  echo "prct_repeat_threshold must be a positive integer" >&2
  exit 64
fi
if ! [[ "$c3_min_repeat_count" =~ ^[1-9][0-9]*$ ]]; then
  echo "c3_min_repeat_count must be a positive integer" >&2
  exit 64
fi
if ! [[ "$c3_max_takeover_attempts" =~ ^[1-9][0-9]*$ ]]; then
  echo "c3_max_takeover_attempts must be a positive integer" >&2
  exit 64
fi
if ! [[ "$c3_takeover_completed_cooldown_s" =~ ^[0-9]+(.[0-9]+)?$ ]]; then
  echo "c3_takeover_completed_cooldown_s must be a non-negative number" >&2
  exit 64
fi
if ! [[ "$prct_cooldown_s" =~ ^[0-9]+(.[0-9]+)?$ ]]; then
  echo "prct_cooldown_s must be a non-negative number" >&2
  exit 64
fi
if [[ "$prct_enable_peer_takeover" != "true" && "$prct_enable_peer_takeover" != "false" ]]; then
  echo "prct_enable_peer_takeover must be true or false" >&2
  exit 64
fi
if ! [[ "$prct_peer_cert_wait_s" =~ ^[0-9]+(.[0-9]+)?$ ]]; then
  echo "prct_peer_cert_wait_s must be a non-negative number" >&2
  exit 64
fi
if ! [[ "$prct_peer_handoff_timeout_s" =~ ^[0-9]+(.[0-9]+)?$ ]]; then
  echo "prct_peer_handoff_timeout_s must be a non-negative number" >&2
  exit 64
fi
if ! [[ "$prct_peer_state_max_age_s" =~ ^[0-9]+(.[0-9]+)?$ ]]; then
  echo "prct_peer_state_max_age_s must be a non-negative number" >&2
  exit 64
fi
if [[ "$c3_enable_marginal_gate" != "true" && "$c3_enable_marginal_gate" != "false" ]]; then
  echo "c3_enable_marginal_gate must be true or false" >&2
  exit 64
fi
for c3_numeric_param in "$c3_benefit_margin_s" "$c3_trust_threshold" "$c3_load_weight" "$c3_handoff_overhead_s" "$c3_trust_penalty_s" "$c3_nominal_speed_m_s" "$c3_owner_fallback_penalty_s" "$c3_owner_stuck_alpha" "$c3_owner_repeat_cost_s" "$c3_peer_cert_grace_s" "$c3_takeover_cooldown_s" "$c3_takeover_completed_cooldown_s"; do
  if ! [[ "$c3_numeric_param" =~ ^[0-9]+(.[0-9]+)?$ ]]; then
    echo "all c3 numeric params must be non-negative numbers: $c3_numeric_param" >&2
    exit 64
  fi
done
if ! awk -v v="$c3_trust_threshold" 'BEGIN { exit !(v >= 0.0 && v <= 1.0) }'; then
  echo "c3_trust_threshold must be between 0.0 and 1.0" >&2
  exit 64
fi
if [[ "$c3_enable_marginal_gate" == "true" && "$prct_enable_retry_suppression" == "true" ]]; then
  echo "C3 mode requires prct_enable_retry_suppression=false; B1 suppression hides long failure chains" >&2
  exit 64
fi
if [[ "$c3_enable_marginal_gate" == "true" && "$prct_enable_peer_takeover" != "true" ]]; then
  echo "C3 mode requires prct_enable_peer_takeover=true" >&2
  exit 64
fi

case "$scene" in
  open_plan_office) launch_file="open_plan_office.launch" ;;
  cubicle_office) launch_file="cubicle_office.launch" ;;
  octa_maze) launch_file="octa_maze.launch" ;;
  *) echo "unknown scene: $scene" >&2; exit 64 ;;
esac

run_root="${PRCT_RUN_ROOT:-pilot}"
run_dir="$ECRTA_LOG_ROOT/${run_root}/$scene/uav_${drone_num}/${run_id}"
mkdir -p "$run_dir/config_snapshot"
export ECRTA_TELEMETRY_DIR="$run_dir"
resource_pid=""
rosbag_pid=""
observers_stopped=0
cleanup_started=0
lkh_resource_dir="$ECRTA_WORKSPACE/src/swarm_exploration/utils/lkh_mtsp_solver/resource"

date -Is > "$run_dir/start_time.txt"
printf 'scene=%s\ndrone_num=%s\nduration_s=%s\nlaunch_file=%s\nworkspace=%s\n' \
  "$scene" "$drone_num" "$duration_s" "$launch_file" "$ECRTA_WORKSPACE" > "$run_dir/run_manifest.txt"
printf 'activation_script=%s\n' "$activation_script" >> "$run_dir/run_manifest.txt"
printf 'communication_threshold_m=%s\n' "$communication_threshold" >> "$run_dir/run_manifest.txt"
printf 'reachability_shadow_max_candidates=%s\n' "$reachability_shadow_max_candidates" >> "$run_dir/run_manifest.txt"
printf 'reachability_peer_shadow_max_peers=%s\n' "$reachability_peer_shadow_max_peers" >> "$run_dir/run_manifest.txt"
printf 'prct_enable_retry_suppression=%s\nprct_repeat_threshold=%s\nprct_cooldown_s=%s\n' \
  "$prct_enable_retry_suppression" "$prct_repeat_threshold" "$prct_cooldown_s" >> "$run_dir/run_manifest.txt"
printf 'prct_enable_peer_takeover=%s\nprct_peer_cert_wait_s=%s\nprct_peer_handoff_timeout_s=%s\nprct_peer_state_max_age_s=%s\n' \
  "$prct_enable_peer_takeover" "$prct_peer_cert_wait_s" "$prct_peer_handoff_timeout_s" \
  "$prct_peer_state_max_age_s" >> "$run_dir/run_manifest.txt"
printf 'c3_enable_marginal_gate=%s\nc3_benefit_margin_s=%s\nc3_trust_threshold=%s\nc3_load_weight=%s\nc3_handoff_overhead_s=%s\nc3_trust_penalty_s=%s\nc3_nominal_speed_m_s=%s\nc3_owner_fallback_penalty_s=%s\nc3_owner_stuck_alpha=%s\n' "$c3_enable_marginal_gate" "$c3_benefit_margin_s" "$c3_trust_threshold" "$c3_load_weight" "$c3_handoff_overhead_s" "$c3_trust_penalty_s" "$c3_nominal_speed_m_s" "$c3_owner_fallback_penalty_s" "$c3_owner_stuck_alpha" >> "$run_dir/run_manifest.txt"
printf 'c3_min_repeat_count=%s\nc3_owner_repeat_cost_s=%s\nc3_peer_cert_grace_s=%s\nc3_takeover_cooldown_s=%s\nc3_max_takeover_attempts=%s\nc3_takeover_completed_cooldown_s=%s\n' "$c3_min_repeat_count" "$c3_owner_repeat_cost_s" "$c3_peer_cert_grace_s" "$c3_takeover_cooldown_s" "$c3_max_takeover_attempts" "$c3_takeover_completed_cooldown_s" >> "$run_dir/run_manifest.txt"
printf 'telemetry_dir=%s\n' "$ECRTA_TELEMETRY_DIR" >> "$run_dir/run_manifest.txt"
cp "$ECRTA_WORKSPACE/src/swarm_exploration/exploration_manager/launch/$launch_file" "$run_dir/config_snapshot/"
cp "$ECRTA_WORKSPACE/src/swarm_exploration/exploration_manager/launch/single_drone_exploration.xml" "$run_dir/config_snapshot/"

capture_pre_shutdown_artifacts() {
  if [[ -f "$run_dir/roslaunch.log" && ! -f "$run_dir/launch_pre_shutdown.log" ]]; then
    cp "$run_dir/roslaunch.log" "$run_dir/launch_pre_shutdown.log"
  fi
  if [[ -d "$lkh_resource_dir" ]]; then
    mkdir -p "$run_dir/lkh_resource"
    cp -a "$lkh_resource_dir/." "$run_dir/lkh_resource/"
    sha256sum "$lkh_resource_dir"/* > "$run_dir/lkh_resource_sha256.txt" 2>&1 || true
  fi
  if [[ -f "$run_dir/launch_pre_shutdown.log" ]]; then
    grep -n -E 'ERROR|FATAL|Segmentation fault|boost::lock_error|terminate called' \
      "$run_dir/launch_pre_shutdown.log" > "$run_dir/runtime_error_scan.txt" || true
  fi
}

stop_observers() {
  if [[ "$observers_stopped" == 1 ]]; then
    return
  fi
  observers_stopped=1
  if [[ -n "$rosbag_pid" ]]; then
    kill -INT "$rosbag_pid" 2>/dev/null || true
    wait "$rosbag_pid" 2>/dev/null || true
    rosbag_pid=""
  fi
  if [[ -n "$resource_pid" ]]; then
    kill -TERM "$resource_pid" 2>/dev/null || true
    wait "$resource_pid" 2>/dev/null || true
    resource_pid=""
  fi
  if [[ -d "$run_dir/rosbag" ]]; then
    find "$run_dir/rosbag" -maxdepth 1 -name '*.bag' -type f -print0 | \
      xargs -0 -r -n 1 rosbag info > "$run_dir/rosbag_info.txt" 2>&1 || true
  fi
}

start_resource_monitor() {
  (
    while kill -0 "$launch_pid" 2>/dev/null; do
      printf 'sample_begin_wall=%s\n' "$(date -Is)"
      ps -eo pid=,ppid=,pcpu=,pmem=,rss=,comm=,args= | \
        awk 'match($0, /(roslaunch|rosmaster|exploration_node|quadrotor_dynamics|pointcloud_render|opengl_render|LKH)/) {print}'
      ps -eo pcpu=,rss=,args= | awk '
        match($0, /(roslaunch|rosmaster|exploration_node|quadrotor_dynamics|pointcloud_render|opengl_render|LKH)/) {
          cpu += $1; rss += $2; count += 1
        }
        END {printf "aggregate_processes=%d aggregate_cpu_pct=%.3f aggregate_rss_kb=%.0f\n", count, cpu, rss}'
      nvidia-smi --query-compute-apps=pid,process_name,used_memory --format=csv,noheader 2>&1 || true
      printf 'sample_end\n'
      sleep 1
    done
  ) > "$run_dir/resource_samples.log" 2>&1 &
  resource_pid=$!
}

count_finished_drones_from_telemetry() {
  /usr/bin/python3 - "$run_dir"/telemetry_drone_*.jsonl <<'PY'
import json
import sys
from pathlib import Path

finished = set()
for raw_path in sys.argv[1:]:
    path = Path(raw_path)
    if not path.is_file():
        continue
    for line in path.read_text(encoding="utf-8").splitlines():
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        if event.get("event") == "fsm_transition" and event.get("to") == "FINISH":
            drone_id = event.get("drone_id")
            if isinstance(drone_id, int):
                finished.add(drone_id)
print(len(finished))
PY
}

cleanup() {
  if [[ "$cleanup_started" == 1 ]]; then
    return
  fi
  cleanup_started=1
  capture_pre_shutdown_artifacts
  stop_observers
  if [[ -n "${launch_pid:-}" ]]; then
    kill -INT "$launch_pid" 2>/dev/null || true
    wait "$launch_pid" 2>/dev/null || true
  fi
  if [[ -f "$run_dir/roslaunch.log" ]]; then
    grep -n -E 'ERROR|FATAL|Segmentation fault|boost::lock_error|terminate called' \
      "$run_dir/roslaunch.log" > "$run_dir/post_shutdown_error_scan.txt" || true
  fi
}
trap cleanup EXIT INT TERM

launch_args=("drone_num:=$drone_num" "communication_threshold:=$communication_threshold")
launch_args+=("prct_enable_retry_suppression:=$prct_enable_retry_suppression" \
  "prct_repeat_threshold:=$prct_repeat_threshold" "prct_cooldown_s:=$prct_cooldown_s")
launch_args+=("prct_enable_peer_takeover:=$prct_enable_peer_takeover" \
  "prct_peer_cert_wait_s:=$prct_peer_cert_wait_s" \
  "prct_peer_handoff_timeout_s:=$prct_peer_handoff_timeout_s" \
  "prct_peer_state_max_age_s:=$prct_peer_state_max_age_s")
launch_args+=("c3_enable_marginal_gate:=$c3_enable_marginal_gate" "c3_benefit_margin_s:=$c3_benefit_margin_s" "c3_trust_threshold:=$c3_trust_threshold" "c3_load_weight:=$c3_load_weight" "c3_handoff_overhead_s:=$c3_handoff_overhead_s" "c3_trust_penalty_s:=$c3_trust_penalty_s" "c3_nominal_speed_m_s:=$c3_nominal_speed_m_s" "c3_owner_fallback_penalty_s:=$c3_owner_fallback_penalty_s" "c3_owner_stuck_alpha:=$c3_owner_stuck_alpha")
launch_args+=("c3_min_repeat_count:=$c3_min_repeat_count" "c3_owner_repeat_cost_s:=$c3_owner_repeat_cost_s" "c3_peer_cert_grace_s:=$c3_peer_cert_grace_s" "c3_takeover_cooldown_s:=$c3_takeover_cooldown_s" "c3_max_takeover_attempts:=$c3_max_takeover_attempts" "c3_takeover_completed_cooldown_s:=$c3_takeover_completed_cooldown_s")
if (( reachability_shadow_max_candidates > 0 )); then
  launch_args+=("reachability_shadow_max_candidates:=$reachability_shadow_max_candidates")
fi
if (( reachability_peer_shadow_max_peers > 0 )); then
  launch_args+=("reachability_peer_shadow_max_peers:=$reachability_peer_shadow_max_peers")
fi
roslaunch exploration_manager "$launch_file" "${launch_args[@]}" > "$run_dir/roslaunch.log" 2>&1 &
launch_pid=$!
printf 'launch_pid=%s\nlaunch_command=roslaunch exploration_manager %s drone_num:=%s communication_threshold:=%s reachability_shadow_max_candidates:=%s reachability_peer_shadow_max_peers:=%s prct_enable_retry_suppression:=%s prct_repeat_threshold:=%s prct_cooldown_s:=%s prct_enable_peer_takeover:=%s prct_peer_cert_wait_s:=%s prct_peer_handoff_timeout_s:=%s prct_peer_state_max_age_s:=%s c3_max_takeover_attempts:=%s\n' \
  "$launch_pid" "$launch_file" "$drone_num" "$communication_threshold" \
  "$reachability_shadow_max_candidates" "$reachability_peer_shadow_max_peers" \
  "$prct_enable_retry_suppression" "$prct_repeat_threshold" "$prct_cooldown_s" \
  "$prct_enable_peer_takeover" "$prct_peer_cert_wait_s" "$prct_peer_handoff_timeout_s" \
  "$prct_peer_state_max_age_s" "$c3_max_takeover_attempts" >> "$run_dir/run_manifest.txt"
printf 'c3_launch_args=c3_enable_marginal_gate:=%s c3_benefit_margin_s:=%s c3_trust_threshold:=%s c3_load_weight:=%s c3_handoff_overhead_s:=%s c3_trust_penalty_s:=%s c3_nominal_speed_m_s:=%s c3_owner_fallback_penalty_s:=%s c3_owner_stuck_alpha:=%s c3_min_repeat_count:=%s c3_owner_repeat_cost_s:=%s c3_peer_cert_grace_s:=%s c3_takeover_cooldown_s:=%s c3_max_takeover_attempts:=%s c3_takeover_completed_cooldown_s:=%s\n' "$c3_enable_marginal_gate" "$c3_benefit_margin_s" "$c3_trust_threshold" "$c3_load_weight" "$c3_handoff_overhead_s" "$c3_trust_penalty_s" "$c3_nominal_speed_m_s" "$c3_owner_fallback_penalty_s" "$c3_owner_stuck_alpha" "$c3_min_repeat_count" "$c3_owner_repeat_cost_s" "$c3_peer_cert_grace_s" "$c3_takeover_cooldown_s" "$c3_max_takeover_attempts" "$c3_takeover_completed_cooldown_s" >> "$run_dir/run_manifest.txt"

ready=0
for attempt in $(seq 1 30); do
  rosnode list > "$run_dir/rosnode_list_${attempt}.txt" 2>&1 || true
  rostopic info /move_base_simple/goal > "$run_dir/trigger_topic_${attempt}.txt" 2>&1 || true
  node_count=$(grep -c '/quad_.*exploration_node_' "$run_dir/rosnode_list_${attempt}.txt" || true)
  subscriber_count=$(grep -c '/quad_.*exploration_node_' "$run_dir/trigger_topic_${attempt}.txt" || true)
  printf 'attempt=%s node_count=%s trigger_subscriber_count=%s\n' "$attempt" "$node_count" "$subscriber_count" >> "$run_dir/readiness.log"
  if [[ "$node_count" -ge "$drone_num" && "$subscriber_count" -ge "$drone_num" ]]; then
    ready=1
    break
  fi
  sleep 1
done
printf 'ready=%s\n' "$ready" >> "$run_dir/run_manifest.txt"
rosnode list > "$run_dir/rosnode_list_ready.txt" 2>&1 || true
rostopic list > "$run_dir/rostopic_list_ready.txt" 2>&1 || true
rosparam list > "$run_dir/rosparam_list_ready.txt" 2>&1 || true
rostopic info /move_base_simple/goal > "$run_dir/trigger_topic_ready.txt" 2>&1 || true

# Verify the launch argument reached every exploration node before triggering.
threshold_check_file="$run_dir/communication_threshold_check.tsv"
printf 'drone_id\texpected\tactual\tmatch\n' > "$threshold_check_file"
threshold_check_failed=0
for drone_id in $(seq 1 "$drone_num"); do
  param_path="/quad_${drone_id}/exploration_node_${drone_id}/communication/connection_threshold"
  actual_value=$(rosparam get "$param_path" 2>/dev/null | tr -d '[:space:]' || true)
  match=0
  if [[ "$communication_threshold" == "inf" ]]; then
    [[ "$actual_value" == "inf" || "$actual_value" == ".inf" ]] && match=1
  else
    [[ "$actual_value" == "$communication_threshold" || \
       "$actual_value" == "${communication_threshold}.0" ]] && match=1
  fi
  printf '%s\t%s\t%s\t%s\n' "$drone_id" "$communication_threshold" \
    "$actual_value" "$match" >> "$threshold_check_file"
  if [[ "$match" != 1 ]]; then threshold_check_failed=1; fi
done
printf 'communication_threshold_check_failed=%s\n' "$threshold_check_failed" >> "$run_dir/run_manifest.txt"
if [[ "$threshold_check_failed" == 1 ]]; then
  printf 'threshold_check_failed=1\n' >> "$run_dir/run_manifest.txt"
  exit 76
fi

prct_check_file="$run_dir/prct_check.tsv"
printf 'param\tvalue\tmatch\n' > "$prct_check_file"
prct_check_failed=0
for prct_pair in "prct_enable_retry_suppression:$prct_enable_retry_suppression" \
                 "prct_repeat_threshold:$prct_repeat_threshold" \
                 "prct_cooldown_s:$prct_cooldown_s" \
                 "prct_enable_peer_takeover:$prct_enable_peer_takeover" \
                 "prct_peer_cert_wait_s:$prct_peer_cert_wait_s" \
                 "prct_peer_handoff_timeout_s:$prct_peer_handoff_timeout_s" \
                 "prct_peer_state_max_age_s:$prct_peer_state_max_age_s"; do
  prct_name="${prct_pair%%:*}"
  prct_expected="${prct_pair#*:}"
  prct_actual=$(rosparam get "/${prct_name}" 2>/dev/null | tr -d '[:space:]' || true)
  prct_match=0
  [[ "$prct_actual" == "$prct_expected" || "$prct_actual" == "${prct_expected}.0" ]] && prct_match=1
  printf '%s\t%s\t%s\t%s\n' "$prct_name" "$prct_expected" "$prct_actual" "$prct_match" >> "$prct_check_file"
  if [[ "$prct_match" != 1 ]]; then prct_check_failed=1; fi
done
printf 'prct_check_failed=%s\n' "$prct_check_failed" >> "$run_dir/run_manifest.txt"
if [[ "$prct_check_failed" == 1 ]]; then
  printf 'prct_check_failed=1\n' >> "$run_dir/run_manifest.txt"
  exit 79
fi
c3_check_file="$run_dir/c3_check.tsv"
printf 'param\tvalue\tmatch\n' > "$c3_check_file"
c3_check_failed=0
for c3_pair in "c3_enable_marginal_gate:$c3_enable_marginal_gate" "c3_benefit_margin_s:$c3_benefit_margin_s" "c3_trust_threshold:$c3_trust_threshold" "c3_load_weight:$c3_load_weight" "c3_handoff_overhead_s:$c3_handoff_overhead_s" "c3_trust_penalty_s:$c3_trust_penalty_s" "c3_nominal_speed_m_s:$c3_nominal_speed_m_s" "c3_owner_fallback_penalty_s:$c3_owner_fallback_penalty_s" "c3_owner_stuck_alpha:$c3_owner_stuck_alpha" "c3_min_repeat_count:$c3_min_repeat_count" "c3_owner_repeat_cost_s:$c3_owner_repeat_cost_s" "c3_max_takeover_attempts:$c3_max_takeover_attempts" "c3_takeover_completed_cooldown_s:$c3_takeover_completed_cooldown_s"; do
  c3_name="${c3_pair%%:*}"
  c3_expected="${c3_pair#*:}"
  c3_actual=$(rosparam get "/$c3_name" 2>/dev/null | tr -d '[:space:]' || true)
  c3_actual_norm=$(printf '%s' "$c3_actual" | tr '[:upper:]' '[:lower:]')
  c3_match=0
  if [[ "$c3_name" == "c3_enable_marginal_gate" ]]; then
    if [[ "$c3_expected" == "true" ]]; then
      [[ "$c3_actual_norm" == "true" || "$c3_actual_norm" == "1" ]] && c3_match=1
    else
      [[ "$c3_actual_norm" == "false" || "$c3_actual_norm" == "0" ]] && c3_match=1
    fi
  else
    [[ "$c3_actual" == "$c3_expected" || "$c3_actual" == "${c3_expected}.0" || "$c3_actual_norm" == "$c3_expected" || "$c3_actual_norm" == "${c3_expected}.0" ]] && c3_match=1
  fi
  printf '%s\t%s\t%s\t%s\n' "$c3_name" "$c3_expected" "$c3_actual" "$c3_match" >> "$c3_check_file"
  if [[ "$c3_match" != 1 ]]; then c3_check_failed=1; fi
done
printf 'c3_check_failed=%s\n' "$c3_check_failed" >> "$run_dir/run_manifest.txt"
if [[ "$c3_check_failed" == 1 ]]; then
  printf 'c3_check_failed=1\n' >> "$run_dir/run_manifest.txt"
  exit 80
fi

if (( reachability_shadow_max_candidates > 0 )); then
  shadow_check_file="$run_dir/reachability_shadow_check.tsv"
  printf 'drone_id\texpected\tactual\tmatch\n' > "$shadow_check_file"
  shadow_check_failed=0
  for drone_id in $(seq 1 "$drone_num"); do
    param_path="/quad_${drone_id}/exploration_node_${drone_id}/exploration/reachability_shadow_max_candidates"
    actual_value=$(rosparam get "$param_path" 2>/dev/null | tr -d '[:space:]' || true)
    match=0
    [[ "$actual_value" == "$reachability_shadow_max_candidates" ]] && match=1
    printf '%s\t%s\t%s\t%s\n' "$drone_id" "$reachability_shadow_max_candidates" \
      "$actual_value" "$match" >> "$shadow_check_file"
    if [[ "$match" != 1 ]]; then shadow_check_failed=1; fi
  done
  printf 'reachability_shadow_check_failed=%s\n' "$shadow_check_failed" >> "$run_dir/run_manifest.txt"
  if [[ "$shadow_check_failed" == 1 ]]; then
    printf 'reachability_shadow_check_failed=1\n' >> "$run_dir/run_manifest.txt"
    exit 77
  fi
fi

if (( reachability_peer_shadow_max_peers > 0 )); then
  peer_shadow_check_file="$run_dir/reachability_peer_shadow_check.tsv"
  printf 'drone_id\texpected\tactual\tmatch\n' > "$peer_shadow_check_file"
  peer_shadow_check_failed=0
  for drone_id in $(seq 1 "$drone_num"); do
    param_path="/quad_${drone_id}/exploration_node_${drone_id}/exploration/reachability_peer_shadow_max_peers"
    actual_value=$(rosparam get "$param_path" 2>/dev/null | tr -d '[:space:]' || true)
    match=0
    [[ "$actual_value" == "$reachability_peer_shadow_max_peers" ]] && match=1
    printf '%s\t%s\t%s\t%s\n' "$drone_id" "$reachability_peer_shadow_max_peers" \
      "$actual_value" "$match" >> "$peer_shadow_check_file"
    if [[ "$match" != 1 ]]; then peer_shadow_check_failed=1; fi
  done
  printf 'reachability_peer_shadow_check_failed=%s\n' "$peer_shadow_check_failed" >> "$run_dir/run_manifest.txt"
  if [[ "$peer_shadow_check_failed" == 1 ]]; then
    printf 'peer_shadow_check_failed=1\n' >> "$run_dir/run_manifest.txt"
    exit 78
  fi
fi

# Preserve the same trigger used by the existing smoke procedure; it is not a completion claim.
if [[ "$ready" != 1 ]]; then
  printf 'trigger_skipped=missing_exploration_subscribers\n' >> "$run_dir/run_manifest.txt"
  exit 75
fi

mkdir -p "$run_dir/rosbag"
bag_topics=(
  /move_base_simple/goal
  /planning/swarm_traj
  /swarm_expl/drone_state
  /swarm_expl/meeting_opt
  /swarm_expl/meeting_opt_res
)
for drone_id in $(seq 1 "$drone_num"); do
  bag_topics+=("/quad_${drone_id}/planning/pos_cmd_${drone_id}")
done
rosbag record --output-prefix "$run_dir/rosbag/selected_topics" "${bag_topics[@]}" \
  > "$run_dir/rosbag/record.log" 2>&1 &
rosbag_pid=$!
start_resource_monitor
printf 'rosbag_topics=%s\n' "${bag_topics[*]}" >> "$run_dir/run_manifest.txt"

# Subscribers exist before the FSM has completed its two-second INIT period.
sleep 3
rostopic pub -1 /move_base_simple/goal geometry_msgs/PoseStamped \
  '{header: {frame_id: world}, pose: {position: {x: 0.0, y: 0.0, z: 1.0}, orientation: {w: 1.0}}}' \
  > "$run_dir/trigger.log" 2>&1 || true

# A local observation rule only: capture a pre-shutdown snapshot once every expected
# UAV has logged FINISH. It is not asserted to be the paper's completion definition.
finish_count=0
log_finish_count=0
completion_observed_all=0
end_seconds=$((SECONDS + duration_s))
while (( SECONDS < end_seconds )); do
  sleep 1
  log_finish_count=$(grep -E -o 'Drone [0-9]+ from [A-Z_]+ to FINISH' \
    "$run_dir/roslaunch.log" 2>/dev/null | sort -u | wc -l || true)
  finish_count=$(count_finished_drones_from_telemetry)
  printf 'elapsed_s=%s telemetry_finish_count=%s log_finish_count=%s\n' \
    "$SECONDS" "$finish_count" "$log_finish_count" >> "$run_dir/completion_watch.log"
  if [[ "$finish_count" -ge "$drone_num" ]]; then
    completion_observed_all=1
    break
  fi
done
stop_observers
capture_pre_shutdown_artifacts
printf 'finish_count_pre_shutdown=%s\ncompletion_observed_all=%s\n' \
  "$finish_count" "$completion_observed_all" >> "$run_dir/run_manifest.txt"
printf 'finish_count_source=telemetry_fsm_transition\nlog_finish_count_pre_shutdown=%s\n' \
  "$log_finish_count" >> "$run_dir/run_manifest.txt"

rosnode list > "$run_dir/rosnode_list_end.txt" 2>&1 || true
rostopic list > "$run_dir/rostopic_list_end.txt" 2>&1 || true
date -Is > "$run_dir/end_time.txt"
printf 'pilot_completed=1\n' >> "$run_dir/run_manifest.txt"
if [[ -d "$run_dir" ]]; then
  /usr/bin/python3 "$ECRTA_ROOT/scripts/analyze_telemetry.py" "$run_dir" --out "$run_dir/telemetry_summary.json" || true
  /usr/bin/python3 "$ECRTA_ROOT/scripts/audit_peer_handoff_active.py" --run-dir "$run_dir" --out "$run_dir/peer_takeover_audit.json" || true
fi
printf 'run_dir=%s\n' "$run_dir"
