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

for legacy_ref in 'run_b1plus_batch' 'run_c3_formal_batch' 'run_prct_batch' 'audit_peer_handoff_active' 'aggregate_prct_formal' 'summarize_prct_stats'; do
  if grep -q "$legacy_ref" "$runner" "$batch" "$search"; then
    fail "new protocol still references legacy script: $legacy_ref"
  fi
done

if grep -E 'peer_takeover=true|c3_enable_marginal_gate=true' "$batch" "$search"; then
  fail "new batch/search still enables old protocol flags"
fi

grep -q 'prct_enable_peer_takeover_ = false' "$source_cpp" "$source_h" || fail "source peer takeover default not false"
grep -q 'c3_enable_marginal_gate_ = false' "$source_cpp" "$source_h" || fail "source C3 default not false"
grep -q 'method_mode_ = "baseline"' "$source_cpp" "$source_h" || fail "source method mode default not baseline"

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
