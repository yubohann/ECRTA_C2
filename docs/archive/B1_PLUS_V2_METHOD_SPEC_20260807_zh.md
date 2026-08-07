# B1+ v2：失败链迟滞抑制与自适应冷却

## 1. 定位

本文档是 PRCT-C2 项目当前主方法的最新版本说明。C3/peer takeover 已在多轮机制审计中显示为 0 次触发，不能作为主贡献；ECRTA 的执行时间残差校准也未通过机制审计。因此，当前主方法聚焦于 C2 执行层最稳定、可量化的问题：同一目标反复触发 A* `open_set_exhausted` 所形成的失败链。

B1+ v2 不修改 C2 的三张地图、LiDAR 探索任务、传感器模型、无人机动力学、通信协议、LKH/ACVRP 后端、终止条件或评价指标。它只在方法工作副本中增加执行层状态和候选目标过滤。

## 2. 一句话主张

在 C2 固定 benchmark 下，对同一失败目标使用“阈值确认 + 自适应冷却 + 候选切换”，能够降低重复 A* 失败、失败链长度和超时时尾，同时不牺牲覆盖率、路径规划安全与在线计算预算。

## 3. B1 与 B1+ 的隔离

新增参数 `prct_backoff_enabled`：

| 参数 | B0 | B1 | B1+ |
|---|---|---|---|
| `prct_enable_retry_suppression` | false | true | true |
| `prct_backoff_enabled` | false | false | true |
| 冷却时长 | 无 | 固定 `prct_cooldown_s` | 自适应 `prct_backoff_*` |
| 候选过滤 | 无 | 有 | 有 |
| peer takeover | 关闭 | 关闭 | 关闭 |

B1 使用固定 5 s 冷却，用于检验“失败目标抑制”本身的价值。B1+ 使用失败链自适应冷却，默认序列为 5 s / 5 s / 5 s / 10 s / 20 s / 30 s，用于检验“越长的失败链是否越应该远离该目标”。

## 4. 算法状态

冷却键：

`frontier_id + 0.1 m 取整目标坐标 + map_version + owner_id`

状态转移：

1. 局部 A* 对同一目标返回 `open_set_exhausted`。
2. 累计 `repeat_count`；小于阈值时不抑制，只记录。
3. 同一目标后续 A* 成功时清零 `repeat_count` 与冷却，因此只有连续失败才构成失败链。
4. 达到阈值后进入冷却，冷却期内从候选目标中过滤。
5. 冷却到期、地图版本变化、目标消失或目标集合变化时解除。
6. 若当前任务候选全部被过滤，回退到 C2 原始的下一个合法目标选择，不允许空转。
7. 每次注册记录 `backoff_s`、`backoff_enabled`、`map_version`、`cooldown_until_wall_s`；成功重置记录 `prct_failure_chain_reset`。

## 5. 本次实现变更

- `c2_exploration_manager.h/cpp`：新增 `prct_backoff_enabled_`，`prctFailureCooldownS()` 在关闭时返回固定冷却，开启时使用指数退避；新增 `registerPrctSuccess()`，成功规划同一目标后清零失败链。
- 三张官方场景 launch：新增 `prct_backoff_enabled` arg 与 param。
- `run_scene_pilot.sh`：新增第 33 个参数，写入 manifest、launch 参数审计和 `prct_check.tsv`。
- `run_b1plus_batch.sh`：B1 固定冷却、B1+ 自适应冷却，不再把两个方法混同。
- 遥测：`prct_retry_suppression_register` 增加 `backoff_enabled`。

## 6. 已有证据与诚实边界

B1+ v1 的 n=2 结果不能用于判断方法：

- 当时 B1 与 B1+ 都传入了相同参数，二者没有显式隔离。
- 已停止 v1 批处理，避免继续消耗资源。
- 现有结果只能作为背景：B1 固定抑制在压力实例中显著降低失败次数；B1+ 是否优于 B1 尚未验证。

本版本必须先完成：

1. 编译成功。
2. 同一压力实例上的 B0/B1/B1+ pilot，确认参数隔离生效。
3. 至少 5 组成对 repeated instance 后，再做统计判断。

## 7. 相关方法参考

方法设计参考了近年开源探索系统中的 blocked-goal 处理，而不是机械堆叠模块：

- DAIB-Explorer：连续 N 次 blocked 后才确认并切换目标，强调避免瞬时地图噪声造成误判。
- CURE1 / fast multi-robot exploration：关注执行层失败、退化目标和长期记忆。

B1+ v2 与这些工作的差异是：不改变 C2 的分配后端，只把“同一目标重复失败”识别为执行层事件，用可审计的冷却状态避免反复消耗规划预算。

## 8. 下一步

1. `catkin_make -j2`。
2. 120-180 s pilot：B1 应显示 `backoff_enabled=0`，B1+ 应显示 `backoff_enabled=1`，且两者 `prct_check.tsv` 全部 match。
3. 审计 telemetry：`prct_retry_suppression_skip`、`backoff_s`、`prct_candidate_filter`、`prct_all_cooled_fallback`。
4. 有效 batch：open_plan_office / 3 UAV / 5 m / 180 s，至少 5 组成对。
5. 若 B1+ 无收益，只保留“重复失败抑制 + 固定冷却”作为主方法，不把失败包装成成功。
