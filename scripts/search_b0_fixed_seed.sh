#!/usr/bin/env bash
set -Eeo pipefail

source /home/c2dev/c2_explorer_reproduction/scripts/activate_reachability_retry.sh

usage() {
  echo "usage: $0 <scene> <drone_num> <duration_s> <seed_start> <seed_end> [communication_threshold_m=5.0]" >&2
  exit 64
}

scene="$1"
drone_num="$2"
duration_s="$3"
seed_start="$4"
seed_end="$5"
communication_threshold="\${6:-5.0}"

case "$scene" in open_plan_office|cubicle_office|octa_maze) ;; *) usage ;; esac
[[ "$drone_num" =~ ^[0-9]+$ && "$drone_num" -ge 2 && "$drone_num" -le 4 ]] || usage
[[ "$duration_s" =~ ^[0-9]+$ && "$duration_s" -gt 0 ]] || usage
[[ "$seed_start" =~ ^[1-9][0-9]*$ && "$seed_end" =~ ^[1-9][0-9]*$ ]] || usage
[[ "$seed_end" -ge "$seed_start" ]] || usage
[[ "$communication_threshold" =~ ^([0-9]+([.][0-9]+)?|inf)$ ]] || usage

runner="$ECRTA_ROOT/scripts/run_scene_pilot.sh"
comm_label=$(printf '%s' "$communication_threshold" | tr '.' 'p')
comm_dir="comm_\${comm_label}m"
search_root="$ECRTA_LOG_ROOT/seed_search/$scene/uav_$drone_num/$comm_dir"
mkdir -p "$search_root"
rm -f "$search_root/candidate.txt"
csv="$search_root/b0_seed_search.csv"
printf 'lkh_seed,run_id,run_exit,finish_count,makespan_s_proxy,astar_failure_count,lkh_request_count,lkh_failure_count\n' > "$csv"

for seed in $(seq "$seed_start" "$seed_end"); do
  run_id="b0_fixed_seed_${seed}_${comm_label}m"
  run_dir="$ECRTA_LOG_ROOT/seed_search/$scene/uav_$drone_num/$run_id"
  if [[ -d "$run_dir" ]]; then
    echo "skip existing: $run_dir"
    continue
  fi
  echo "start seed=$seed run=$run_id"
  set +e
  LKH_SEED="$seed" PRCT_RUN_ROOT=seed_search METHOD_MODE=baseline "$runner" "$scene" "$drone_num" "$duration_s" "$run_id" "$communication_threshold" "0" "0" "false" "3" "5.0" "false" "0.25" "2.0" "2.0" "false" "1.0" "0.5" "0.5" "0.5" "2.0" "2.0" "3.0" "1.0" "3" "0.3" "0.6" "30.0" "3" "120.0" "5.0" "30.0" "2.0" "false" "0.2" "false" "20.0"
  run_exit=$?
  set -e

  summary="$run_dir/telemetry_summary.json"
  metrics=$(/usr/bin/python3 - "$summary" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
if not path.is_file():
    print(",,,,,,")
    sys.exit(0)
d = json.loads(path.read_text(encoding="utf-8"))
astar = d.get("astar", {})
lkh = d.get("lkh", {})
event_counts = d.get("event_counts", {})
finish_ids = d.get("finish_drone_ids", [])
makespan = d.get("local_finish_makespan_wall_s", "")
if not isinstance(makespan, (int, float)):
    makespan = ""
astar_fail = astar.get("failure_diagnostic_count", 0)
lkh_request = event_counts.get("lkh_request", 0)
lkh_fail = 0
for section in ("ACVRP", "ATSP-frontier", "ATSP-grid"):
    lkh_fail += int(lkh.get(section, {}).get("failure", 0))
print(str(len(finish_ids)) + "," + str(makespan) + "," + str(astar_fail) + "," + str(lkh_request) + "," + str(lkh_fail))
PY
)
  printf '%s,%s,%s,%s\n' "$seed" "$run_id" "$run_exit" "$metrics" >> "$csv"
  echo "done seed=$seed exit=$run_exit metrics=$metrics"

  astar_fail_count=$(printf '%s' "$metrics" | cut -d, -f3)
  finish_count=$(printf '%s' "$metrics" | cut -d, -f1)
  if [[ "$run_exit" == 0 && "$finish_count" -lt "$drone_num" ]]; then
    echo "candidate found: not all drones FINISH for seed=$seed run=$run_id"
    printf 'candidate_seed=%s\nrun_dir=%s\n' "$seed" "$run_dir" > "$search_root/candidate.txt"
    break
  fi
  if [[ "$run_exit" == 0 && "$astar_fail_count" -ge 50 ]]; then
    echo "candidate found: astar_failure_count=$astar_fail_count for seed=$seed run=$run_id"
    printf 'candidate_seed=%s\nrun_dir=%s\n' "$seed" "$run_dir" > "$search_root/candidate.txt"
    break
  fi
done

echo "search_csv=$csv"
