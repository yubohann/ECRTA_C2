# B1+ v5 Pilot Evidence 20260807

## Scope

These are mechanism pilots on the C2 fixed benchmark, not paper-level statistical evidence. Repeated instance labels are not official seeds; completion is the local FINISH observation rule used by the existing audit pipeline.

## Runs

| run | scene | UAV | comm | A* failures | register | no_alternative | repeated same-goal failures | makespan proxy | FINISH |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| v5_pilot_open3x5_001 | open_plan_office | 3 | 5 m | 30 | 30 | 0 | 29 | 68.06 s | 3/3 |
| v5_pilot_open3x5_002 | open_plan_office | 3 | 5 m | 0 | 0 | 0 | 0 | 71.93 s | 3/3 |
| v5_pilot_open3x5_003 | open_plan_office | 3 | 5 m | 1 | 1 | 1 | 0 | 68.24 s | 3/3 |

## Reading

Run 001 used the first v5 fallback behavior: when the only task-constrained candidate was locked, it returned the original candidate and repeated A* on the same goal 29 times. Run 003 used the corrected behavior: no acceptable alternative produced an empty candidate set, the existing global non-cooled fallback selected another reachable target, and the failed goal was attempted only once.

Run 002 did not exercise the mechanism, so its makespan difference carries no attribution.

## Artifacts

Logs and telemetry are retained under:

/home/c2dev/c2_explorer_reproduction/logs/reachability_retry/pilot_v5/open_plan_office/uav_3/v5_pilot_open3x5_001

and the corresponding 002 and 003 subdirectories.

No result in this directory is sufficient to claim an end-to-end improvement.
