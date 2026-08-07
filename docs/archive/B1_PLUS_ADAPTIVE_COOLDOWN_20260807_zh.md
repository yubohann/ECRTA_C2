# B1+：失败链自适应指数退避（Adaptive Failure-Chain Backoff）

## 1. 为什么从 C3 切到 B1+

当前已完成的三轮正式交错实验给出的证据很一致：

- B0 的 open_set_exhausted 重复 A* 失败会形成失败链，少数实例会卡到超时，例如 180s 压力 batch 中实例 4 出现 343 次失败、最终 2/3 完成；
- B1 固定冷却已经显著改善：同一压力 batch 中 A* 失败从 B0 的 351 次降到 12 次，FINISH 率从 93.3% 升到 100%；
- C3 v8.1 中 takeover_sent/executed 始终为 0，不能把收益归因给 peer takeover；
- C3 相对 B1 的成对中位差在 open3 为 +4.76 s、open2 为 -14.31 s，但 takeover 均为 0，因此 peer takeover 没有端到端机制证据。

结论不是“C3 的思路全错”，而是 takeover 的触发条件在当前三图中几乎不会发生。真正被数据支持的问题在 owner 自己的重复失败链上。B1+ 保留 C2 原始分配和轨迹规划，只把“同一失败目标被反复 A* 重试”这一执行层缺陷改成自适应抑制。

## 2. 主方法定义

B1+ 在 C2 的固定三图、LiDAR 探索任务、通信、动力学、LKH/ACVRP、终止条件和评价指标都不变的前提下，增加一个执行层状态：

- 当局部 A* 对同一目标返回 open_set_exhausted 时，记录目标冷却键；
- 冷却键为 `frontier_id + 0.1 m 取整的目标坐标 + map_version + owner_id`；
- 当重复失败次数达到阈值后，该目标进入冷却；
- 冷却时长不再固定，而是按重复失败次数指数退避：

`cooldown_s = min(prct_backoff_max_s, prct_backoff_initial_s * prct_backoff_factor^(repeat_count - prct_repeat_threshold))`

默认参数为：

| 参数 | 默认值 | 含义 |
|---|---:|---|
| prct_repeat_threshold | 3 | 同一目标连续失败多少次后进入冷却 |
| prct_backoff_initial_s | 5.0 | 基础冷却时长 |
| prct_backoff_factor | 2.0 | 失败链指数放大倍数 |
| prct_backoff_max_s | 30.0 | 冷却时长上限 |

默认产生的冷却序列为：5 s、5 s、5 s、10 s、20 s、30 s。也就是说，短暂失败不会被过度惩罚，连续失败链越长，owner 越应该先把任务交还目标选择层，而不是在同一不可达目标上继续消耗规划时间。

## 3. 与 B1、C3 的区别

| 方法 | 重复失败抑制 | 冷却方式 | takeover | 主贡献 |
|---|---|---|---|---|
| B0 | 无 | 无 | 无 | 原 C2 基线 |
| B1 | 有 | 固定 5 s | 无 | 重复失败抑制 |
| B1+ | 有 | 按失败链指数退避 | 仅在冷却后仍失败的实验性后备 | 自适应失败链冷却 |
| C3 v8.1 | 有 | 固定冷却 | 有，实测 0 触发 | 不再作为主贡献 |

B1+ 的机制收益来源不是“等待更久”，而是：

1. 不把规划预算浪费在已被同一局部地图反复判为不可达的目标上；
2. 对失败链增长采用可解释、可审计的保守策略；
3. 冷却到期、地图版本变化、目标消失或有新可达性证据时自动解除；
4. 若冷却导致当前候选全部被抑制，回退到原始 C2 的下一个合法目标，不允许空转。

## 4. 实现与审计点

B1+ 只增加执行层遥测和冷却状态，不改 C2 的任务分解、ACVRP/LKH、局部轨迹生成、动力学或评价定义。

新增遥测字段至少包括：

- frontier_id、goal_x/y/z；
- map_version、owner_id；
- repeat_count、backoff_s、cooldown_until_wall_s；
- 是否因冷却跳过该目标；
- 是否发生安全回退到原始 C2。

每轮正式实验必须保留：

- config_snapshot；
- launch.log；
- rosbag；
- telemetry jsonl；
- failures.jsonl；
- metrics.json；
- summary.json。

## 5. 下一轮实验设计

在 open_plan_office / 3 UAV / 5 m / 180 s 压力实例上做 B0/B1/B1+ 成对比较，目标至少 5 对，然后扩展到 cubicle_office、octa_maze 和 2/4 UAV。

预注册门槛：

- B1+ 相对 B1 的 A* 失败次数和失败链长度下降；
- 不空转、不丢任务、不因冷却跳过最终仍可达的目标；
- makespan 不系统性恶化；
- 同一压力实例下 FINISH 率不低于 B1；
- 覆盖率、总路径、碰撞、不可行轨迹、LKH 失败和在线时延无系统退化。

peer takeover 只在 B1+ 冷却到上限后仍出现持续失败链时作为事件触发后备。若下一轮仍为 0 触发，论文主贡献只保留 B1+，不把 takeover 写入主消融。
