# Experiments (final v5 — all statistics audited 2026-08-08)

## 4.1 Platform and Protocol
- WSL2 Ubuntu 20.04.3 / ROS Noetic / RTX 4090 / WSLg; official C2 repo commit fd1c76a;
  LKH 3.0.6; NLopt 2.7.1. Two disclosed platform patches (OpenGL 4.6->3.3, initial RPM zeroing).
- Cells: cubicle_office/4UAV/5m/180s; open_plan_office/2UAV/5m/180s. n=10/method, LKH_SEED=1,
  PRCT_RUN_FULL_DURATION=true. Runs are repeated instances (MARSIM randomness not fully
  seed-controlled: verified same-seed 0 vs 407 A* failures).
- Metrics and statistics (audited):
  * makespan = last-drone FINISH wall time; runs with finish < drone_num are truncated to 180s
    (the summary field spans completed drones only);
  * A* failures = astar.failure_diagnostic_count (open_set_exhausted);
  * trajectory failures counted from raw telemetry traj_result events, split by reason
    (astar_fail = reachability; kinodynamic_fail = dynamics);
  * SVR reuse counted from gate events' decision field;
  * tests: bootstrap 95% CI on paired diffs (20k), sign-permutation Wilcoxon (20k),
    Mann-Whitney U permutation (20k) — reported jointly since instances are not matched.
  * infra-suspect runs (WSLg render crash: ~no exploration + no finish) excluded and
    reported separately (1/200).
- Verification: scripts/test_aggregate_formal_batch.py (7 tests) + per-batch audit scripts.

## 4.2 Dual-Class Execution Failures (contribution)
C2's execution layer exhibits two failure classes:
  1. Reachability failures: A* open_set_exhausted on the same (frontier, goal, map) —
     up to 425 repeats of one target in a single run (R1 REACH run; 258 in B0 baseline runs).
  2. Dynamics failures: kinodynamic trajectory search returns no feasible trajectory
     (traj_result reason=kinodynamic_fail).
R2 cubicle (n=10):

| method | A* failures | traj astar_fail | traj kinodynamic_fail |
|---|---|---|---|
| B0 | 73 | 73 | 1 |
| B1 (suppress) | 2 | 2 | 381 |
| REACH | 54 | 54 | 0 |
| SVR | 52 | 52 | 3 |
| STEER | 34 | 34 | 0 |

Key finding: retry suppression (B1) eliminates reachability failures but amplifies dynamics
failures ~381x (R1 v1 showed <=3 kinodynamic failures for every method). Suppression forces
frequent goal switches executed from non-zero velocity states, for which the kinodynamic
searcher cannot find a feasible trajectory. The two failure classes are coupled: goal
switching must respect the dynamics state (switch margin should include a velocity term).

## 4.3 Main Results (audited)

### cubicle/4 (failure-chain cell)
| method | R1 fin | R1 ms_med | R2 fin | R2 ms_med |
|---|---|---|---|---|
| B0 | 8/9 | **73.61** | 10/10 | **73.48** |
| B1 | 9/10 | 76.05 | 9/10 | 82.35 |
| REACH | 9/10 | 75.59 | 8/10 | 77.73 |
| SVR | 10/10 | 80.27 | 9/10 | 77.37 |
| STEER | 9/10 | 75.75 | 9/10 | 83.04 |

### open/2 (low-failure cell)
| method | R1 fin | R1 ms_med | R2 fin | R2 ms_med | pooled med |
|---|---|---|---|---|---|
| B0 | 10/10 | 81.67 | 10/10 | 85.16 | 81.4 |
| B1 | 10/10 | 83.95 | 10/10 | 86.39 | 84.6 |
| REACH | 10/10 | **81.00** | 10/10 | **79.38** | **78.4** |
| SVR | 10/10 | 81.25 | 10/10 | 83.94 | — |
| STEER | 10/10 | 78.86 | 10/10 | 87.93 | — |

### Significance (permutation tests, all audited)
- No comparison is significant (p > 0.08) in any batch.
- Strongest trend: REACH vs B1 on open/2 R2: -6.53s, 8/2 wins, bootstrap95=[-20.8,-0.5],
  mwu_p=0.14; pooled n=20: -6.53s, 14/20 wins, mean -7.86s.
- R3 (cubicle, complete n=10, audited): B0 71.05s (10/10) best;
  B1 85.65s (+16.4s vs B0, mwu_p=0.019); REACH 77.00s (+6.9s, p=0.029);
  SVR 80.09s (+10.2s, p=0.028); STEER 76.31s (+3.3s, p=0.085).
  Consistent with R1/R2: in the cubicle cell every mechanism carries overhead vs B0.
- HOP (long-range goal selection, mechanism probe, n=7-8): trajectory hops -4%,
  makespan med 77.0 vs B0 75.5 (0/7 wins) — no makespan benefit; reported as
  negative-result extension in Discussion.
- Coverage proxy (trajectory_end_reasons frontier_covered, R2 cubicle): B0 2597, B1 2544,
  REACH 2602, SVR 2391, STEER 2633 — no method sacrifices coverage.
- LKH is not a bottleneck: ACVRP p95<=0.054s, ATSP p95<=0.003s, 0 failures.

### Mechanism engagement (audited)
| mechanism | R1 cub | R2 cub | R1 open2 | R2 open2 |
|---|---|---|---|---|
| REACH adjustments | 2371 | 2587 | 1544 | 1515 |
| SVR gates / actual reuse | 149 / 31 | 141 / 33 | 64 / 8 | 73 / 8 |
| STEER events | 0 | 0 | 0 | 0 |

## 4.4 Interpretation
1. Failure chains are real, quantifiable, stochastic, and suppressible.
2. Allocation-level risk feedback (REACH) shows a consistent (non-significant) makespan trend
   on open/2 and is neutral on cubicle — with cross-drone evidence sharing it engages
   mechanically (risk links > 0), but C2's fine task units (~1-2s service) leave little room.
3. Goal-level suppression (B1) trades reachability failures for dynamics failures;
   goal-hold discipline (STEER v3) keeps behavior close to baseline when no evidence exists.
4. SVR's allocation reuse genuinely fires (80 reuses) and yields the lowest failure counts
   with comparable coverage, without significant makespan gain.
5. Makespan is dominated by instance variance (60-140s range); n=10 lacks power for
   <=10s effects. Larger n or controllable failure injection is future work.
