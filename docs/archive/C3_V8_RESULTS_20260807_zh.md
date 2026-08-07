# C3 v8 机制验证结果

## 1. 结论

C3 v8 修复了 v7 的根因：owner 收到 COMPLETED 后不再反复攻击同一 frontier。

## 2. pilot 汇总

| 实例 | 场景 | UAV | FINISH | A*失败 | takeover | exhausted | makespan proxy(s) | 审计 |
|---|---|---:|---:|---:|---:|---:|---:|---|
| c3_open3_v8_001 | open_plan_office | 3 | 3/3 | 11 | 1 sent/1 completed | 0 | 78.90 | audit-failed（遥测双括号，已修） |
| c3_open3_v8_002 | open_plan_office | 3 | 3/3 | 6 | 0 | 0 | 58.28 | audit-failed（同上） |
| c3_open3_v8_003 | open_plan_office | 3 | 3/3 | 0 | 0 | 0 | 75.58 | audit-complete |

## 3. v8_001 关键事件

- takeover：1 次发布，1 次收到，1 次执行，1 次 COMPLETED
- completed invalidation：1 次 `c3_takeover_completed_invalidate`
- `c3_takeover_exhausted`：0
- 等待 handoff 共 1.57 s
- 轨迹失败：11 次，保留在审计中

这验证了核心修复：takeover COMPLETED 后，owner 不再把同一 frontier 重复选回。

## 4. 修复内容（v8）

1. `isC3TakeoverCompleted()`：收到 `COMPLETED` 后对同一 frontier 做长冷却，默认 120 s。
2. 目标过滤：`isPrctTakeoverCooled()` 先检查 completed invalidation，再检查 keyed cooldown。
3. 参数：`c3_takeover_completed_cooldown_s` 已进入三个 launch 和 `run_scene_pilot.sh` 第 29 个参数。
4. 遥测：修正了 v8 早期 patch 引入的双 `}}`，当前审计 JSON 通过。

## 5. 未写论文结论

以上只是机制 pilot，不是正式成对统计；仍需多实例确认：

- 有失败链的实例是否稳定出现，以及完成后是否稳定跳转；
- C3 相对 B1 的 makespan/未完成率是否有预注册阈值收益；
- 碰撞、`boost::lock_error`、覆盖率、时延是否退化。