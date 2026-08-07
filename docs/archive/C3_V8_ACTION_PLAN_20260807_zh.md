# C3 v8: 已移交完成即失效，阻断 owner 重复攻击

## 1. 结论先写清楚

C3 v7 已修复两个问题：ACCEPTED 不再被当成终态回执；同一目标超过 `c3_max_takeover_attempts` 后回退原 C2。

但 v7 pilot 暴露了第三个根因：

- `c3_open3_v7_002`：Drone 1 全程攻击同一 `frontier_id=4`；前两次 takeover 都返回 COMPLETED，之后仍继续尝试同一目标，累计 `c3_takeover_exhausted=112`，最终 2/3 FINISH。
- `c3_open3_v7_004` 同样出现 2/3 FINISH 和 100+ exhausted。

这说明问题不是“peer 不能接管”，而是 owner 没有把“peer 已完成该 frontier”当作该任务的终态，仍然反复把它选回来。

## 2. v8 修改：takeover-completed invalidation

新机制：owner 收到本机发起的 takeover 的 `COMPLETED` 回执后，登记 `c3_takeover_completed_until_wall_s_[frontier_id]`，冷却默认 120 s。

- 目标过滤：`isPrctTakeoverCooled()` 现在同时检查 keyed cooldown 与 frontier-level completed invalidation；只要该 frontier 已被 peer 完成，owner 不再选它。
- 遥测：新增 `c3_takeover_completed_invalidate` 事件，记录 frontier、map version、cooldown 和到期时间。
- 参数：新增 `c3_takeover_completed_cooldown_s`，launch 默认 `120.0`，pilot 脚本第 29 个参数。
- 语义：`COMPLETED` 只标记该 frontier 已完成；不会清空所有失败统计，不会修改原始任务分配、LKH、地图或评价定义。

这样设计的原因：在 C2 固定地图探索中，frontier 被 peer 实际执行到目标后，owner 重复规划同一目标只能产生 `open_set_exhausted` 和空转；把它短时间移出候选集合是执行层去重，不是改变原算法。

## 3. 已修改文件

- `c2_exploration_manager.h`：新增 `isC3TakeoverCompleted()`、`c3_takeover_completed_cooldown_s_`、`c3_takeover_completed_until_wall_s_`。
- `c2_exploration_manager.cpp`：读取新参数；回执回调登记 COMPLETED；`isPrctTakeoverCooled()` 先查 completed 失效。
- `open_plan_office.launch` / `cubicle_office.launch` / `octa_maze.launch`：新增 arg/param。
- `scripts/run_scene_pilot.sh`：第 29 个参数 `c3_takeover_completed_cooldown_s`，并记录到 manifest 与 c3_check。

## 4. 验证目标

本轮 pilot 只验证机制，不写论文结论：

1. takeover 返回 COMPLETED 后，同一 frontier 不再被 owner 反复选择。
2. `c3_takeover_exhausted` 不再出现 100+。
3. `c3_takeover_completed_invalidate` 事件出现且数量与 COMPLETED 一致。
4. FINISH 率不因新增失效标记下降；碰撞、`boost::lock_error`、超时仍保留在失败审计中。

通过标准：至少 4 个 open_plan_office/3 UAV/5 m pilot 中，出现失败链的实例不再重复攻击已完成目标；若仍 2/3 或 1/3，继续修状态机，不用调参硬凑。

## 5. 仍未改变

- 不修改 C2 三图、LiDAR、动力学、通信、LKH/ACVRP、终止条件、指标。
- 不修改 `upstream/c2_explorer_official`。
- OpenGL 4.6 -> 3.3 仅是 WSLg 兼容补丁。
- ECRTA 时间残差校准、PRCT peer-takeover-as-main-method 均不作为主方法；v8 只保留证书、回执、边际代价、trust 与执行层去重的有用部分。

## 6. 下一步

1. 跑 4 个同协议 pilot。
2. 用 `audit_peer_handoff_active.py` 统计 takeover/completed/exhausted。
3. 若通过，进入 B0/B1/C3 成对统计；若失败，按日志修状态机。

## 7. 预注册门槛不变

- C3 相对 B1 成对 makespan 中位改善 >= 10%，或未完成率改善 >= 20 个百分点；
- 覆盖率、总路径、碰撞、LKH 失败、在线时延不得系统性退化；
- 失败、超时、碰撞、ABORT、exhausted 全部进入分母。