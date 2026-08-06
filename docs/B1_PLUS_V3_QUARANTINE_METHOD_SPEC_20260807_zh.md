# B1+ v3：失败确认目标隔离（Map-Epoch Goal Quarantine）

## 1. 定位

本文档是 PRCT-C2 项目当前主方法的 v3 说明。v2 的“指数退避等待”已经在
open_plan_office / 3 UAV / 5 m / 180 s / n=5 成对实验中完成首轮统计：B1+ 相对 B1
没有带来 makespan 收益，反而多出 2.33 s 均值、3.24 s 中位数，因此不再把“越失败越等越久”
作为主方法。

v3 保留 v2 已确认的机制基础：C2 执行层稳定存在同一目标重复触发局部 A*
open_set_exhausted 的问题，且 peer takeover 无端到端收益。v3 把主贡献改为
“失败确认后的地图版本条件隔离”，不再使用时间退避。

## 2. 一句话主张

在 C2 固定三图、固定 LiDAR 探索任务、固定通信、固定局部规划器和固定 LKH/ACVRP 后端下，
对同一失败目标连续达到确认阈值后，在当前 frontier map epoch 内不再重新选中该目标；
当存在其他合法候选时直接切换，当候选全部隔离时回退到原始 C2 的下一个合法选择，
从而降低重复 A* 失败、失败链长度、等待开销和超时时尾，同时不牺牲覆盖率、安全性与在线计算预算。

## 3. 与 B1、B1+ v2 的区别

| 参数 | B0 | B1 | B1+ v3 |
|---|---|---|---|
| prct_enable_retry_suppression | false | true | true |
| prct_backoff_enabled | false | false | true |
| 失败抑制规则 | 无 | 固定 5 s 冷却 | 当前 map epoch 内持续隔离 |
| 重新尝试条件 | 无 | 冷却到期后可重试 | 仅 map epoch 变化、目标消失或成功回执后释放 |
| 候选过滤 | 无 | 有 | 有 |
| 全候选隔离回退 | 无 | 原始 C2 最近候选 | 原始 C2 最近候选，并记录 prct_all_cooled_fallback |
| peer takeover | 关闭 | 关闭 | 关闭 |

prct_backoff_initial_s / max_s / factor 保留为审计参数，但 v3 不再使用它们。

## 4. 算法状态

隔离键：

frontier_id + 0.1 m 取整目标坐标 + map_version + owner_id

状态转移：

1. 局部 A* 对同一目标返回 open_set_exhausted。
2. 累计 repeat_count；达到 prct_repeat_threshold 前只记录，不抑制。
3. 同一目标后续 A* 成功时清零 repeat_count 与隔离状态。
4. 达到阈值后，在当前 map epoch 内将目标从候选集合过滤。
5. map epoch 变化时清除旧 epoch 隔离状态，允许重新评估。
6. 若当前任务候选全部被隔离，回退到原始 C2 最近的合法目标选择，并记录回退事件。
7. 每次注册、跳过、释放和回退均写入 telemetry JSONL。

## 5. 与已判废方向的关系

- ECRTA 执行时间残差校准：机制审计未通过，不进入主方法。
- PRCT peer takeover：实测无端到端收益，不进入主方法。
- RL / QD / CARTA / LLM / VLM：默认不进入主方法。
- v2 指数退避：n=5 无收益，判废。

## 6. 实现变更

- c2_exploration_manager.cpp：
  - isPrctTargetCooled() 在 prct_backoff_enabled=true 时按 map epoch 永久隔离；
  - prctFailureCooldownS() 在隔离模式下返回极大值，隔离模式不再依赖定时器；
  - prct_retry_suppression_register 增加 quarantine_enabled 和 backoff_s=-1；
  - updateFrontierStruct() 在 map epoch 变化时清理旧 epoch 的隔离记录；
  - 全候选隔离时增加 prct_all_cooled_fallback 遥测与原始 C2 回退。
- c2_exploration_manager.h：说明 prct_backoff_enabled 的 v3 语义。
- run_b1plus_batch.sh：B1/B1+ 隔离说明更新为 B1+ v3 map-epoch quarantine。

## 7. 预注册判据

正式实验仍使用成对 repeated instance：

- 主矩阵：三图 x 2/3/4 UAV x 5 m，每个配置至少 5 组 pilot、目标 10-20 组；
- 补充矩阵：10 m、15 m、无限通信；
- 成对比较 B0/B1/B1+ v3；
- 主指标：FINISH 率、makespan、RMST、失败链长度、A* 失败次数、等待开销；
- 安全指标：覆盖率、总路径、碰撞、LKH 失败、在线时延 p50/p95；
- B1+ v3 相对 B1 的预注册阈值：makespan 成对中位数改善 >= 10%，
  或未完成率改善 >= 20 个百分点，同时尾部 p90/RMST 和安全指标不系统退化。

## 8. 诚实边界

- v2 的 n=5 结果不能用于证明 v3，也不能直接投稿。
- 三张固定地图允许的最强结论是“固定 benchmark 上的执行层失败抑制”，不能声称跨场景泛化。
- 若 v3 相对 B1 仍无收益，则主方法降级为“重复失败抑制 + 固定冷却”，保留负结果。
