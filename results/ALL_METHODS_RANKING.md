# ALL_METHODS_RANKING

日期：2026-08-07
实例：open_plan_office / 3 UAV / 5 m / LKH_SEED=2 / duration=180s
批次：candidate_seed2_pilot_v2
状态：pilot 结果，不是正式统计结论。

## 结果

| method | FINISH | makespan proxy (s) | A* failures | 备注 |
|---|---:|---:|---:|---|
| B0 | 2/3 | 67.45 | 596 | 单一不可达目标反复重试 |
| B1 | 3/3 | 54.76 | 2 | 当前 pilot 第一 |
| REACH-C2 | 3/3 | 68.90 | 3 | 风险代价调整了少量边，但分配路径更慢 |
| SVR-C2 | 3/3 | 69.98 | 8 | digest 复用 0 次，每次都重跑分配器 |
| STEER-C2 | 3/3 | 59.92 | 3 | 目标保持/切换未充分触发 |

## 机制审计

- B0：`failures.jsonl` 已补齐，596 次失败集中在同一 `frontier_id=7` 的不可达视图。
- B1：失败计数降到 2，`prct_retry_suppression_register=2`，`prct_candidate_filter=1`，收益来自冷却/去重。
- REACH：`reach_cost_adjustment` 事件 172 次，`risk_adjusted_edges` 大多为 0，只有 3 次调整 6 条边；当前风险参数下不足以稳定改变分配。
- SVR：4 次 `svr_reallocation_gate` 全部 `original_allocator_invoked`，`svr_reuse=0`；候选集合每次变化就重跑，没有实现稳定性/净收益判定。
- STEER：`command_events.jsonl` 只有 195 次 `goal_set`，没有 `goal_switch`；switch 分支仍被 `prct_backoff_enabled_` 旧开关挡住，目标切换逻辑实际未参与。

## 下一步

1. 修 SVR：增加 previous-allocation 相似度稳定性 gate，避免候选集微变即重跑。
2. 修 STEER：`methodSteerActive()` 时绕过旧的 `prct_backoff_enabled_` 门槛，让确认阻塞后切换真正生效。
3. 重跑同一实例为 `candidate_seed2_pilot_v3`，继续同时优化 B1/REACH/SVR/STEER。

-## v3 实现修正（运行中）
-
-已于 2026-08-07 修改并重新构建：
-
- STEER：`prctFilterCooledTargets` 允许 `methodSteerActive()` 进入确认阻塞切换分支；margin 拒绝时会继续过滤冷却目标，而不是返回原候选集。
- SVR：新增候选集快照与 stable-overlap gate，按 grid/center/type/hull/位置判断是否复用上一次分配；移除证据哈希导致每次地图微变都重算的问题。
- REACH：失败 frontier 改为映射到最近任务中心，而不是固定半径内扫描；对 allocation matrix 和 local tour matrix 同时施加乘性风险因子与可解释加性惩罚。
- runner：新增 `reach_center_match_radius_m`、`svr_reuse_match_radius_m`，写入 manifest 并进入 method_check。
- 当前状态：`candidate_seed2_pilot_v3` 正在 open_plan_office/3 UAV/5m/LKH_SEED=2 上运行，结果只作为机制验证，不作为正式统计。
-
-待 v3 完成后更新：goal_switch、svr_reuse、reach_center_links、risk_adjusted_edges、FINISH/makespan 与五方法对比。
- 当前状态：`candidate_seed2_pilot_v3` 正在 open_plan_office/3 UAV/5m/LKH_SEED=2 上运行，结果只作为机制验证，不作为正式统计。
- 当前状态：`candidate_seed2_pilot_v3` 首次因新参数未接入 launch XML，method_check 失败，完整保留该失败批次；修正 launch 后以 `candidate_seed2_pilot_v3b` 重新运行同一实例，结果只作为机制验证，不作为正式统计。
