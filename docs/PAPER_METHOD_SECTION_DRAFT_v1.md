# Method Section (draft v1, based on audited code)

## 3. Method

### 3.1 Problem Setup and the Execution-Failure Chain

We build directly on C2-Explorer. The C2 pipeline at each robot is:

```
local LiDAR map  →  hierarchical grid (HGrid) splits unknown space into
                   connected components (task units) with centers
  →  ACVRP/LKH assigns task sequences under communication components
  →  local frontier-based goal selection (top-N viewpoint cost)
  →  A* path planning → B-spline execution
```

In the official C2 baseline, a failure chain emerges at the goal-selection /
planning boundary: the local planner repeatedly selects the same
(frontier, viewpoint) target, runs A* with the same inflated map, and receives
`open_set_exhausted` for the same (start, goal) pair hundreds of times within a
single episode. In our frozen baseline this repeats up to 407-596 times per
episode (open-plan office / 2-3 UAV), keeps the robot waiting, and inflates the
tail of the team makespan. The ACVRP/LKH allocator is fast (ACVRP p95 < 40 ms)
and not the bottleneck; the failure chain is.

We model each failed planning attempt as an execution-failure evidence tuple

```
e = (t, drone_id, frontier_id, goal, map_version, reason)
```

with `reason ∈ {open_set_exhausted, ...}`. All three proposed mechanisms consume
this evidence at different layers, without changing maps, sensors, dynamics,
communication, the ACVRP/LKH formulation, termination conditions or metrics.

### 3.2 REACH: Execution-Aware Allocation Costs (allocation layer)

REACH feeds failure evidence back into the assignment cost matrix that the C2
allocator uses. Let `rho(f)` be the failure evidence of frontier task `f`,
compressed as the (decaying) count of `open_set_exhausted` attempts on
`(f, map_version)` normalized by the number of A* attempts on `f`. The nominal
C2 allocation cost `C_nominal(f)` is replaced by

```
C_alloc(f) = C_nominal(f) * (1 + lambda * rho(f))
```

where `lambda = reach_risk_weight` (default 0.25). The correction is applied
both to the ACVRP edge costs and to the local ATSP tour costs; only
`(map_version, frontier_id, owner)`-keyed, fresh evidence is admitted, so
evidence expires when the map changes. REACH is conservative: when no evidence
exists (`rho = 0`) the cost reduces exactly to the original C2 cost, and the
allocation still solves the identical ACVRP/LKH problem.

Design rationale: the *cause* of the failure chain is that the allocator and
the goal selector ignore execution feasibility; REACH makes the allocator
avoid (or deprioritize) task units that provably fail at the planning layer.
Unlike predictive "execution fidelity" models (VORL-EXPLORE [arXiv:2603.07973],
which estimate navigability), REACH uses only measured failure evidence and
adds no learned component.

### 3.3 SVR: Allocation Reuse Under Stable Task Semantics (task layer)

SVR targets the redundant LKH recomputation between allocation rounds. After
each allocation the host stores a compact digest of the candidate task
semantics:

```
digest = (drone_num, blocked_centers, centers: (grid_id, center_id, center_type,
         hull_size, position_rounded)) + candidate snapshot
```

On the next allocation request, if the digest is identical
(`exact_identity`) or matches within `svr_reuse_match_radius_m` for every
candidate center (`stable_overlap`), the previous allocation is reused and the
LKH solve is skipped. A reallocation-gate event is logged with the overlap
count, solver overhead estimate and reallocation cost estimate
(`svr_reallocation_cost_m`, `svr_solver_cost_s`). If any part of the task
semantics changed, the original allocator is invoked with the same candidate
generation as C2, i.e., SVR never invents tasks or alters the assignment
problem.

### 3.4 STEER: Confirmed-Blocked Goal Steering (goal-selection layer)

STEER governs the local goal selection when A* fails, following the
hold-confirm-switch discipline of DAIB-Explorer [GitHub YYY0702/DAIB-Explorer]:

1. **Goal hold**: once a goal (frontier, viewpoint) is set, it is kept for
   `steer_goal_min_hold_s` (3 s) regardless of intermediate failures
   (`goal_set` timestamps recorded per frontier id and per rounded goal
   coordinate + map version).
2. **Confirmation**: a target is only considered failed after
   `prct_repeat_threshold` (3) repeat failures on the same
   `(frontier_id, goal, map_version)`; failures are counted from the A*
   diagnostic events, not from a single attempt.
3. **View rotation within a frontier**: when the top-cost viewpoint of a
   single-frontier selection is cooled, the planner skips it
   (`steer_viewpoint_cooled`) and evaluates the next viewpoint among the
   top-N candidates; if every viewpoint of the frontier is cooled, the
   frontier enters a bounded cooldown (`steer_all_views_cooled`, 5 s) and the
   robot falls back to the original C2 decision path.
4. **Switch margin**: when replacing a cooled first candidate, the
   replacement must satisfy `marginal_cost <= steer_switch_margin_` (0.2);
   otherwise the original candidate list is kept and the robot re-attempts the
   original path (margin-rejected event), preventing flapping.
5. **Evidence-gated release**: cooldowns are released early if the map version
   or the goal evidence hash changes (the environment is dynamic w.r.t. the
   local map).

STEER changes no allocation and no trajectory generation; it only changes the
order in which the existing C2 viewpoint candidates are tried after confirmed
planning failures.

### 3.5 Relationship Between the Three Layers

| layer | mechanism | failure evidence | decision changed |
|---|---|---|---|
| allocation | REACH | rho(f) risk factor | ACVRP cost matrix |
| task semantics | SVR | candidate digest | skip redundant LKH solve |
| goal selection | STEER | (frontier,goal,map) repeats | viewpoint order + cooldown |

All three are independent switches (method_mode), enabling a clean ablation
B0 (none), B1 (pure retry suppression), REACH, SVR, STEER, and any combination
as isolated mechanism comparisons. This paper evaluates them as three
alternative mechanisms plus the B1 suppression baseline on the same frozen C2
platform.

## 4. Experimental Protocol

### 4.1 Platform and Settings

- WSL2 Ubuntu 20.04.3 LTS, ROS Noetic, RTX 4090, WSLg; official C2 repo
  commit fd1c76a; LKH 3.0.6; NLopt 2.7.1.
- Disclosure: two platform-only patches (OpenGL 4.6→3.3 for WSLg; initial RPM
  zeroing) — neither touches allocation, planning, communication or metrics.
- Three official maps frozen; this paper's formal comparison uses the two
  high-failure cells identified in the baseline audit:
  `open_plan_office / 2 UAV` and `cubicle_office / 4 UAV`, 5 m communication,
  180 s window.
- Instances are repeated trials of the same frozen binary with fixed
  `LKH_SEED`; run-to-run variance of the MARSIM sim is treated as
  uncontrolled and reported (classification: repeated instances, not
  seed-indexed trials).

### 4.2 Metrics

- makespan (last-drone FINISH wall time; unfinished trials counted at 180 s)
- FINISH rate
- A* failure counts (open_set_exhausted) and their distribution
- trajectory planning failures
- mechanism events (REACH risk edges, SVR reuse hits, STEER view skips /
  all-cooled / switches / margin rejections)
- coverage proxy (exploration end reasons), collision warnings, LKH p95
- bootstrap 95% CI and Wilcoxon signed-rank on paired indices.

### 4.3 Planned Results Tables (filled in v2)

| metric (cubicle_office/4UAV/5m/180s) | B0 | B1 | REACH | SVR | STEER |
|---|---|---|---|---|---|
| makespan median (s) | | | | | |
| FINISH rate | | | | | |
| A* failure median | | | | | |
| trajectory failure sum | | | | | |

| metric (open_plan_office/2UAV/5m/180s) | B0 | B1 | REACH | SVR | STEER |
|---|---|---|---|---|---|
| makespan median (s) | | | | | |
| FINISH rate | | | | | |
| A* failure median | | | | | |

| paired diff vs B1 (bootstrap 95% CI, Wilcoxon p) | REACH | SVR | STEER |
|---|---|---|---|
| cubicle/4 makespan diff (s) | | | |
| open/2 makespan diff (s) | | | |
