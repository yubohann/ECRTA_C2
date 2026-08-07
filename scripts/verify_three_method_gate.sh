#!/usr/bin/env bash
set -Eeo pipefail

root="${1:-/home/c2dev/c2_explorer_reproduction}"
workspace="${2:-$root/workspace/reachability_retry_c2_method}"
runner="$root/scripts/run_scene_pilot.sh"
batch="$root/scripts/run_three_method_batch.sh"
search="$root/scripts/search_b0_fixed_seed.sh"
source_cpp="$workspace/src/swarm_exploration/exploration_manager/src/c2_exploration_manager.cpp"
source_h="$workspace/src/swarm_exploration/exploration_manager/include/exploration_manager/c2_exploration_manager.h"

fail() {
  echo "VERIFY_FAIL: $1" >&2
  exit 1
}

for f in "$runner" "$batch" "$search"; do
  [[ -f "$f" ]] || fail "missing $f"
  bash -n "$f"
done
[[ -f "$source_cpp" ]] || fail "missing $source_cpp"
[[ -f "$source_h" ]] || fail "missing $source_h"

grep -q 'prct_enable_peer_takeover.*== "true"' "$runner" || fail "runner peer takeover hard gate missing"
grep -q 'c3_enable_marginal_gate.*== "true"' "$runner" || fail "runner C3 hard gate missing"
grep -q 'c3_enable_marginal_gate="${15:-false}"' "$runner" || fail "runner C3 default is not false"
grep -q 'METHOD_MODE must be baseline|suppress|reach|svr|steer' "$runner" || fail "runner method mode whitelist missing"

if grep -E 'peer_takeover=true|c3_enable_marginal_gate=true' "$batch" "$search"; then
  fail "new batch/search still enables old protocol flags"
fi

for scene in open_plan_office cubicle_office octa_maze; do
  launch_xml="$workspace/src/swarm_exploration/exploration_manager/launch/${scene}.launch"
  [[ -f "$launch_xml" ]] || fail "missing launch XML: $launch_xml"
  grep -q '<arg name="prct_enable_peer_takeover" default="false"' "$launch_xml" || fail "$scene launch XML peer takeover default is not false"
  grep -q '<arg name="c3_enable_marginal_gate" default="false"' "$launch_xml" || fail "$scene launch XML C3 default is not false"
  grep -q 'reach_center_match_radius_m' "$launch_xml" || fail "$scene launch XML missing reach_center_match_radius_m"
  grep -q 'svr_reuse_match_radius_m' "$launch_xml" || fail "$scene launch XML missing svr_reuse_match_radius_m"
  grep -q '<param name="reach_center_match_radius_m"' "$launch_xml" || fail "$scene launch XML does not publish reach_center_match_radius_m"
  grep -q '<param name="svr_reuse_match_radius_m"' "$launch_xml" || fail "$scene launch XML does not publish svr_reuse_match_radius_m"
done

grep -q 'REACH_CENTER_MATCH_RADIUS_M' "$runner" || fail "runner missing REACH_CENTER_MATCH_RADIUS_M"
grep -q 'SVR_REUSE_MATCH_RADIUS_M' "$runner" || fail "runner missing SVR_REUSE_MATCH_RADIUS_M"
grep -q 'reach_center_match_radius_m:=$reach_center_match_radius_m' "$runner" || fail "runner missing reach_center_match_radius_m launch arg"
grep -q 'svr_reuse_match_radius_m:=$svr_reuse_match_radius_m' "$runner" || fail "runner missing svr_reuse_match_radius_m launch arg"

grep -q 'prct_enable_peer_takeover_ = false' "$source_cpp" "$source_h" || fail "source peer takeover default not false"
grep -q 'c3_enable_marginal_gate_ = false' "$source_cpp" "$source_h" || fail "source C3 default not false"
grep -q 'method_mode_ = "baseline"' "$source_cpp" "$source_h" || fail "source method mode default not baseline"
grep -E 'method_mode|reach_center_match_radius_m|svr_reuse_match_radius_m' "$source_cpp" "$source_h" >/dev/null || fail "source does not reference new method params"

legacy_names=(
  aggregate_prct_formal.py
  audit_c3_offline.py
  audit_formal_reachability.py
  audit_peer_handoff_active.py
  measure_cert_latency.py
  run_b1plus_batch.sh
  run_c3_formal_batch.sh
  run_prct_batch.sh
  summarize_b1plus_v2.py
  summarize_b1plus_v3.py
  summarize_b1plus_v4.py
  summarize_c3_run.py
  summarize_c3v81_batch.py
  summarize_gates.py
  summarize_prct_stats.py
)
for legacy_name in "${legacy_names[@]}"; do
  if grep -q "$legacy_name" "$runner" "$batch" "$search" "$source_cpp" "$source_h"; then
    fail "legacy script/function reference found: $legacy_name"
  fi
  if [[ -f "$root/scripts/$legacy_name" ]]; then
    fail "legacy script still active in scripts dir: $legacy_name"
  fi
done

for d in "${@:3}"; do
  [[ -f "$d/run_manifest.txt" ]] || continue
  grep -q 'prct_enable_peer_takeover=false' "$d/run_manifest.txt" || fail "manifest enables peer takeover: $d"
  grep -q 'c3_enable_marginal_gate=false' "$d/run_manifest.txt" || fail "manifest enables C3: $d"
  if [[ -f "$d/rostopic_list_ready.txt" ]]; then
    if grep -q '/peer_takeover_' "$d/rostopic_list_ready.txt"; then
      fail "old peer takeover topics appeared: $d"
    fi
  fi
done

echo VERIFY_OK
