# ROUND-2 (v2) 修复设计——基于 ROUND-1 机制审计（2026-08-08）

## R1 机制审计结论（ROUND-1 数据，cubicle/4 × LKH_SEED=1）

| run | finish | makespan | A*失败 | 机制事件 | 归因 |
|---|---|---|---|---|---|
| B0 r1 | 0/4 | 180(截断) | 0 | — | **基础设施失败**：4× boost::lock_error 渲染崩溃，非算法样本 |
| B0 r2 | 4/4 | 73.6 | 0 | — | 无失败链的正常实例 |
| B1 r1 | 4/4 | 110.5 | 28 | suppression 生效 | 抑制有效 |
| REACH r1 | 3/4 | 77.0 | 428 | **risk_center_links=0** | 见缺陷① |
| SVR r1 | 4/4 | 94.1 | 0 | — | 无失败实例 |
| STEER r1 | 3/4 | 84.2 | 182 | **仅 marginal_gate_no_alternative** | 见缺陷② |

失败链结构（REACH r1）：431 次失败中 425 次为同一目标 (fid=24, 4.86,-3.72, mv=60)；STEER r1：182 次全部同一目标 (fid=10, 9.9,-4.3)。**单机单目标重复失败**是核心现象。

## 三个实现缺陷（R3 归因）

**缺陷① REACH 证据不跨机、键控不匹配**
- `reach_allocation_cost_adjustment` 在 host（drone 1）内存执行，`methodExecutionRisk` 用 host 本地的 frontier_id/point/map_version 键查冷却表；失败证据在 drone 2 本地 → 键永远不匹配 → risk_center_links=0。
- 即使同机，A* 失败目标为**视点坐标**（非 ed_->points_[fid]），且键含高频变化的 map_version，精确匹配几乎不可能。
- 修复（v2）：新增 `/c2_reach_evidence` 话题 + `ReachEvidence.msg`，每架机 1Hz 广播自身证据板（round(goal,0.5m)→count）；host 端 `methodCenterRisk(center)` 按**中心邻近**（reach_center_evidence_radius_m=5m）聚合证据折算 ACVRP 代价。跨机、免 frontier-id 匹配。
- 证据窗口 30s（reach_evidence_window_s），配 `reach_cooldown_s=30`（替换原来的 ∞ 冷却，避免永久抑制导致死等）。

**缺陷② all-cooled fallback 回路**
- 当全部候选被冷却时，fallback 循环**无视冷却**重选原目标 → A* 失败 → 再冷却 → 再 fallback……形成 ~2.5 次/秒的失败回路（REACH r1 的 428 次、STEER r1 的 182 次的主要来源）。
- 修复（v2）：fallback 循环跳过当前冷却中的目标；若无可选目标且存在冷却目标 → 记录 `steer_all_cooled_hold` 并返回 FAIL（不触发 A*），由 FSM 循环等待冷却到期/地图变化。该行为与 DAIB-Explorer 的 blocked-hold 一致。

**缺陷③ REACH/SVR 无限冷却**
- `methodCooldownS()` 对 reach/svr 返回 ∞：结合缺陷②修复后会导致目标永久抑制、无人机死等。
- 修复（v2）：改为 `reach_cooldown_s_`（默认 30s）。

## 代码改动清单（v2，已准备于 Windows 暂存副本）

1. `msg/ReachEvidence.msg`（新增）+ CMakeLists 注册
2. `c2_exploration_manager.h`：证据板成员、pub/sub/timer、5 个新方法声明、`#include <map>`
3. `c2_exploration_manager.cpp`：
   - `methodCooldownS()` 有限冷却
   - 新增 `reachEvidenceKey/BoardPrune/Publish/Receive/methodCenterRisk`
   - init 段注册 pub/sub/timer + 3 个新参数
   - `registerPrctFailure` 更新本地证据板
   - `reach_allocation_cost_adjustment` 改为中心级风险
   - fallback 循环修复（跳过冷却 + all_cooled_hold）
   - `allocation_candidate_set` 遥测增加 center_positions（供验证）
4. 三个 launch XML + `run_scene_pilot.sh`：新参数透传（REACH_COOLDOWN_S / REACH_CENTER_EVIDENCE_RADIUS_M / REACH_EVIDENCE_WINDOW_S）

## ROUND-2 批次协议（同格子）

- cubicle_office/4 UAV/5m/180s × LKH_SEED=1 × N=10，五模式，`batch_formal_r2_20260808`
- open_plan_office/2 UAV/5m/180s × LKH_SEED=1 × N=10，`batch_formal_r2_open2_20260808`
- 通过门槛：REACH 的 risk_center_links>0 且 risk_adjusted_edges>0（机制必须真正影响分配）；STEER 的 steer_all_cooled_hold 出现且 A* 失败数较 R1 下降；B1/REACH/SVR/STEER 的 A* 失败中位数显著低于 B0 或与其可比

## 风险

- 若 center 邻近证据仍为 0（C2 中心与执行目标解耦，unmatched_frontier 现象），REACH 如实记为负结果，论文主方法收敛为 B1+STEER 的"确认-保持-切换"。
- 基础设施崩溃（渲染 lock_error）按 infra_suspect 排除并单独报告。
